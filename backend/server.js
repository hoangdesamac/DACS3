require('dotenv').config();

const express = require('express');
const mqtt = require('mqtt');
const { Pool } = require('pg');
const bcrypt = require('bcrypt');
const jwt = require('jsonwebtoken');

const JWT_SECRET = 'your-super-secret-key-change-in-production';
const SALT_ROUNDS = 10;

/** Chỉ ghi bảng telemetry tối đa 1 lần / khoảng thời gian này cho mỗi device_id (ms). */
const TELEMETRY_SAVE_INTERVAL_MS = 5 * 60 * 1000; // 5 phút

const app = express();
app.use(express.json());

const DB_URL = process.env.DATABASE_URL;

const pool = new Pool({
    connectionString: DB_URL,
});

/** Map<deviceId, lastSavedTimestampMs> */
const lastTelemetrySaveByDevice = new Map();

function shouldPersistTelemetry(deviceId) {
    const now = Date.now();
    const last = lastTelemetrySaveByDevice.get(deviceId) ?? 0;
    if (now - last >= TELEMETRY_SAVE_INTERVAL_MS) {
        lastTelemetrySaveByDevice.set(deviceId, now);
        return true;
    }
    return false;
}

const initDB = async () => {
    try {
        await pool.query(`
            CREATE TABLE IF NOT EXISTS devices (
                id VARCHAR(50) PRIMARY KEY,
                name VARCHAR(100),
                type VARCHAR(50),
                is_online BOOLEAN DEFAULT false,
                is_auto BOOLEAN DEFAULT false,
                last_updated TIMESTAMP DEFAULT CURRENT_TIMESTAMP
            );

            CREATE TABLE IF NOT EXISTS telemetry (
                id SERIAL PRIMARY KEY,
                device_id VARCHAR(50) REFERENCES devices(id),
                temperature REAL,
                humidity REAL,
                soil_moisture REAL,
                rain_detected SMALLINT,
                relay_state SMALLINT,
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
            );
        `);

        // Tự động migration phòng trường hợp bảng cũ
        try {
            await pool.query(`ALTER TABLE devices ADD COLUMN is_auto BOOLEAN DEFAULT false;`);
        } catch (e) {}

        // Khởi tạo dữ liệu CHUẨN - Không rác
        await pool.query(`
            INSERT INTO devices (id, name, type, is_online, is_auto)
            VALUES
                ('device_001', 'Relay Gateway', 'RELAY', false, false),
                ('Esp32_Node_DACS3', 'ESP32 Node Trung Tâm', 'NODE', true, false),
                ('device_fan', 'Quạt phòng ngủ', 'FAN', false, false),
                ('device_dryer', 'Giàn phơi đồ', 'DRYER', false, false)
            ON CONFLICT (id) DO NOTHING;
        `);
        console.log("✅ Đã khởi tạo cấu trúc Bảng (Schema) & Dữ liệu thiết bị gốc thành công!");
    } catch (err) {
        console.error("❌ Lỗi khi khởi tạo DB:", err);
    }
};

const initUsersTable = async () => {
    try {
        await pool.query(`
            CREATE TABLE IF NOT EXISTS users (
                id SERIAL PRIMARY KEY,
                email VARCHAR(255) UNIQUE NOT NULL,
                password_hash VARCHAR(255) NOT NULL,
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
            );
        `);
    } catch (err) {}
};

const initAiContextTable = async () => {
    try {
        await pool.query(`
            CREATE TABLE IF NOT EXISTS ai_context (
                id SERIAL PRIMARY KEY,
                user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
                session_id VARCHAR(100) NOT NULL,
                summary VARCHAR(500),
                messages_json TEXT NOT NULL,
                updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                UNIQUE (user_id, session_id)
            );

            CREATE TABLE IF NOT EXISTS ai_context_sessions (
                id BIGSERIAL PRIMARY KEY,
                user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
                session_id VARCHAR(100) NOT NULL,
                summary VARCHAR(500),
                updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                UNIQUE (user_id, session_id)
            );

            CREATE TABLE IF NOT EXISTS ai_context_messages (
                id BIGSERIAL PRIMARY KEY,
                session_ref_id BIGINT NOT NULL REFERENCES ai_context_sessions(id) ON DELETE CASCADE,
                role VARCHAR(20) NOT NULL,
                content TEXT NOT NULL,
                message_order INTEGER NOT NULL,
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                UNIQUE (session_ref_id, message_order)
            );
        `);

        const legacyRows = await pool.query(`
            SELECT user_id, session_id, summary, messages_json, updated_at
            FROM ai_context
        `);
        for (const row of legacyRows.rows) {
            const sessionResult = await pool.query(
                `
                INSERT INTO ai_context_sessions (user_id, session_id, summary, updated_at)
                VALUES ($1, $2, $3, $4)
                ON CONFLICT (user_id, session_id)
                DO UPDATE SET summary = EXCLUDED.summary, updated_at = EXCLUDED.updated_at
                RETURNING id
                `,
                [row.user_id, row.session_id, row.summary || null, row.updated_at]
            );
            const sessionRefId = sessionResult.rows[0].id;
            const parsed = JSON.parse(row.messages_json || "[]");
            await pool.query(`DELETE FROM ai_context_messages WHERE session_ref_id = $1`, [sessionRefId]);
            for (let index = 0; index < parsed.length; index++) {
                const message = parsed[index] || {};
                const role = String(message.role || "").trim();
                const content = String(message.content || "").trim();
                if (!role || !content) continue;
                await pool.query(
                    `
                    INSERT INTO ai_context_messages (session_ref_id, role, content, message_order)
                    VALUES ($1, $2, $3, $4)
                    `,
                    [sessionRefId, role, content, index]
                );
            }
        }
    } catch (err) {
        console.error("❌ Lỗi khi khởi tạo bảng ai_context:", err);
    }
};

const authenticateToken = (req, res, next) => {
    const authHeader = req.headers['authorization'];
    const token = authHeader && authHeader.split(' ')[1];
    if (!token) return res.status(401).json({ error: 'Token required' });

    jwt.verify(token, JWT_SECRET, (err, user) => {
        if (err) return res.status(403).json({ error: 'Invalid token' });
        req.user = user;
        next();
    });
};

const MQTT_BROKER = 'mqtt://broker.emqx.io';
const client = mqtt.connect(MQTT_BROKER);

client.on('connect', () => {
    console.log('✅ Backend đã kết nối EMQX Broker!');
    client.subscribe('DACS3/esp32_to_app');
});

// ================= LẮNG NGHE MQTT VÀ ĐỒNG BỘ CHUẨN (CHỐNG SPAM DB) =================
client.on('message', async (topic, message) => {
    if (topic !== 'DACS3/esp32_to_app') return;

    try {
        const payload = JSON.parse(message.toString());
        const deviceId = payload.id;

        // Bỏ qua tin nhắn rác hoặc tin nhắn khởi động
        if (!deviceId || deviceId === 'esp32_startup') return;

        // 1. Phân tích trạng thái từ gói tin MQTT
        let newOnline = true;
        let newAuto = false;

        if (payload.isOnline !== undefined) {
            newOnline = payload.isOnline;
        }

        if (payload.state === 'AUTO') {
            newAuto = true;
            newOnline = true;
        } else if (payload.state === 'ON' || payload.relay_state === 1) {
            newAuto = false;
            newOnline = true;
        } else if (payload.state === 'OFF' || payload.relay_state === 0) {
            newAuto = false;
            newOnline = false;
        }

        // 2. HÀM CẬP NHẬT THÔNG MINH: Chỉ Update nếu thực sự có thay đổi
        const syncDevice = async (targetId, isOnline, isAuto) => {
            try {
                // Lấy trạng thái hiện tại trong DB
                const current = await pool.query(
                    'SELECT is_online, is_auto FROM devices WHERE id = $1',
                    [targetId]
                );

                if (current.rows.length > 0) {
                    const old = current.rows[0];
                    // CHỈ UPDATE KHI CÓ SỰ THAY ĐỔI
                    if (old.is_online !== isOnline || old.is_auto !== isAuto) {
                        await pool.query(
                            `UPDATE devices SET is_online = $1, is_auto = $2, last_updated = CURRENT_TIMESTAMP WHERE id = $3`,
                            [isOnline, isAuto, targetId]
                        );
                        console.log(`💾 [DB Sync] Đã cập nhật ${targetId} -> Online: ${isOnline}, Auto: ${isAuto}`);
                    }
                }
            } catch (err) {
                console.error(`❌ Lỗi đồng bộ thiết bị ${targetId}:`, err);
            }
        };

        // 3. Phân luồng xử lý theo ID
        if (deviceId === 'Esp32_Node_DACS3') {
            // Cập nhật trạng thái cho chính con Node Trung Tâm
            await syncDevice(deviceId, payload.isOnline !== false, false);

            // Xử lý riêng cho Giàn phơi (Dryer) được kẹp trong gói tin của Node
            if (payload.state !== undefined) {
                await syncDevice('device_dryer', newOnline, newAuto);
            }
        } else {
            // Xử lý cho các thiết bị khác (Fan, Gateway...)
            await syncDevice(deviceId, newOnline, newAuto);
        }

        // 4. Ghi dữ liệu Telemetry (Môi trường) - Giữ nguyên cơ chế 5 phút/lần của bạn
        if (payload.temp !== undefined && shouldPersistTelemetry(deviceId)) {
            await pool.query(
                `INSERT INTO telemetry (device_id, temperature, humidity, soil_moisture, rain_detected, relay_state)
                 VALUES ($1, $2, $3, $4, $5, $6)`,
                [
                    deviceId, 
                    payload.temp, 
                    payload.hum, 
                    payload.soil || 0, 
                    payload.rain !== undefined ? payload.rain : 0, 
                    newOnline ? 1 : 0
                ]
            );
            console.log(`📊 TELEMETRY: Đã lưu dữ liệu môi trường từ [${deviceId}]`);
        }

    } catch (error) {
        console.error("❌ Lỗi xử lý dữ liệu MQTT:", error);
    }
});

// ================= API ROUTES =================

app.post('/api/auth/register', async (req, res) => {
    const { email, password } = req.body;
    if (!email || !password) return res.status(400).json({ error: 'Email and password required' });
    if (password.length < 6) return res.status(400).json({ error: 'Password must be at least 6 characters' });

    try {
        const existing = await pool.query('SELECT id FROM users WHERE email = $1', [email]);
        if (existing.rows.length > 0) return res.status(400).json({ error: 'Email already exists' });

        const passwordHash = await bcrypt.hash(password, SALT_ROUNDS);
        const result = await pool.query(
            'INSERT INTO users (email, password_hash) VALUES ($1, $2) RETURNING id',
            [email, passwordHash]
        );
        res.status(201).json({ userId: result.rows[0].id, message: 'User created' });
    } catch (err) {
        res.status(500).json({ error: 'Server error' });
    }
});

app.post('/api/auth/login', async (req, res) => {
    const { email, password } = req.body;
    if (!email || !password) return res.status(400).json({ error: 'Email and password required' });

    try {
        const result = await pool.query('SELECT id, password_hash FROM users WHERE email = $1', [email]);
        if (result.rows.length === 0) return res.status(401).json({ error: 'Invalid credentials' });

        const valid = await bcrypt.compare(password, result.rows[0].password_hash);
        if (!valid) return res.status(401).json({ error: 'Invalid credentials' });

        const token = jwt.sign({ userId: result.rows[0].id, email }, JWT_SECRET, { expiresIn: '7d' });
        res.json({ token, userId: result.rows[0].id });
    } catch (err) {
        res.status(500).json({ error: 'Server error' });
    }
});

app.post('/api/auth/change-password', authenticateToken, async (req, res) => {
    const userId = req.user?.userId;
    const { currentPassword, newPassword } = req.body;

    if (!currentPassword || !newPassword) {
        return res.status(400).json({ error: 'Current password and new password are required' });
    }
    if (newPassword.length < 6) {
        return res.status(400).json({ error: 'New password must be at least 6 characters' });
    }

    try {
        const result = await pool.query('SELECT password_hash FROM users WHERE id = $1', [userId]);
        if (result.rows.length === 0) {
            return res.status(404).json({ error: 'User not found' });
        }

        const currentHash = result.rows[0].password_hash;
        const isValidCurrentPassword = await bcrypt.compare(currentPassword, currentHash);
        if (!isValidCurrentPassword) {
            return res.status(401).json({ error: 'Current password is incorrect' });
        }

        const newPasswordHash = await bcrypt.hash(newPassword, SALT_ROUNDS);
        await pool.query(
            'UPDATE users SET password_hash = $1 WHERE id = $2',
            [newPasswordHash, userId],
        );

        return res.json({ message: 'Password changed successfully' });
    } catch (err) {
        return res.status(500).json({ error: 'Server error' });
    }
});

app.get('/api/devices', async (req, res) => {
    try {
        const result = await pool.query('SELECT * FROM devices ORDER BY id ASC');
        res.json(result.rows);
    } catch (err) {
        res.status(500).json({ error: "Lỗi Server" });
    }
});

app.get('/api/telemetry/:deviceId', async (req, res) => {
    try {
        const { deviceId } = req.params;
        const result = await pool.query(
            'SELECT * FROM telemetry WHERE device_id = $1 ORDER BY created_at DESC LIMIT 20',
            [deviceId]
        );
        res.json(result.rows);
    } catch (err) {
        res.status(500).json({ error: "Lỗi Server" });
    }
});

app.put('/api/devices/:id/auto_mode', async (req, res) => {
    try {
        const deviceId = req.params.id;
        const isAuto = req.body.is_auto !== undefined ? req.body.is_auto : req.body.isAuto;

        await pool.query(
            'UPDATE devices SET is_auto = $1 WHERE id = $2',
            [isAuto, deviceId]
        );
        
        console.log(`🌐 API (Từ App): Cập nhật chế độ AUTO = ${isAuto} cho thiết bị ${deviceId}`);
        res.json({ success: true, message: "Cập nhật Auto Mode thành công" });
    } catch (err) {
        res.status(500).json({ error: "Lỗi Server" });
    }
});

app.post('/api/devices/toggle', async (req, res) => {
    try {
        const id = req.body.id;
        const isOnline = req.body.is_online !== undefined ? req.body.is_online : req.body.isOnline;

        // Bật thủ công thì phải tắt AUTO đi
        await pool.query(
            'UPDATE devices SET is_online = $1, is_auto = false WHERE id = $2',
            [isOnline, id]
        );
        
        console.log(`🌐 API (Từ App): Chuyển thiết bị ${id} thành ${isOnline ? 'ON' : 'OFF'} (Tắt Auto)`);
        res.json({ success: true, message: "Toggle thành công" });
    } catch (err) {
        res.status(500).json({ error: "Lỗi Server" });
    }
});

app.post('/api/ai-context', authenticateToken, async (req, res) => {
    try {
        const userId = req.user.userId;
        const { sessionId, summary, messagesJson } = req.body;
        if (!sessionId || !messagesJson) {
            return res.status(400).json({ error: "sessionId and messagesJson required" });
        }

        const parsed = JSON.parse(messagesJson || "[]");
        const sessionResult = await pool.query(
            `
            INSERT INTO ai_context_sessions (user_id, session_id, summary, updated_at)
            VALUES ($1, $2, $3, CURRENT_TIMESTAMP)
            ON CONFLICT (user_id, session_id)
            DO UPDATE SET summary = EXCLUDED.summary, updated_at = CURRENT_TIMESTAMP
            RETURNING id
            `,
            [userId, sessionId, summary || null]
        );
        const sessionRefId = sessionResult.rows[0].id;

        await pool.query(`DELETE FROM ai_context_messages WHERE session_ref_id = $1`, [sessionRefId]);
        for (let index = 0; index < parsed.length; index++) {
            const message = parsed[index] || {};
            const role = String(message.role || "").trim();
            const content = String(message.content || "").trim();
            if (!role || !content) continue;
            await pool.query(
                `
                INSERT INTO ai_context_messages (session_ref_id, role, content, message_order)
                VALUES ($1, $2, $3, $4)
                `,
                [sessionRefId, role, content, index]
            );
        }

        res.json({ success: true });
    } catch (err) {
        console.error("❌ save /api/ai-context failed:", err);
        res.status(500).json({ error: "Lỗi Server" });
    }
});

app.get('/api/ai-context', authenticateToken, async (req, res) => {
    try {
        const userId = req.user.userId;
        const result = await pool.query(
            `
            SELECT session_id, summary, updated_at
            FROM ai_context_sessions
            WHERE user_id = $1
            ORDER BY updated_at DESC
            `,
            [userId]
        );
        res.json(result.rows.map(row => ({ ...row, messages_json: "[]" })));
    } catch (err) {
        res.status(500).json({ error: "Lỗi Server" });
    }
});

app.get('/api/ai-context/:sessionId', authenticateToken, async (req, res) => {
    try {
        const userId = req.user.userId;
        const { sessionId } = req.params;
        const result = await pool.query(
            `
            SELECT id, session_id, summary, updated_at
            FROM ai_context_sessions
            WHERE user_id = $1 AND session_id = $2
            LIMIT 1
            `,
            [userId, sessionId]
        );
        if (result.rows.length === 0) return res.status(404).json({ error: "Not found" });
        const session = result.rows[0];
        const messages = await pool.query(
            `
            SELECT role, content
            FROM ai_context_messages
            WHERE session_ref_id = $1
            ORDER BY message_order ASC
            `,
            [session.id]
        );
        res.json({
            session_id: session.session_id,
            summary: session.summary,
            updated_at: session.updated_at,
            messages_json: JSON.stringify(messages.rows),
        });
    } catch (err) {
        res.status(500).json({ error: "Lỗi Server" });
    }
});

app.delete('/api/ai-context/:sessionId', authenticateToken, async (req, res) => {
    try {
        const userId = req.user.userId;
        const { sessionId } = req.params;
        await pool.query(
            `DELETE FROM ai_context_sessions WHERE user_id = $1 AND session_id = $2`,
            [userId, sessionId]
        );
        res.json({ success: true });
    } catch (err) {
        res.status(500).json({ error: "Lỗi Server" });
    }
});

const PORT = 3000;
app.listen(PORT, async () => {
    console.log(`\n🚀 Backend Server đang chạy tại port: ${PORT}`);
    console.log(`📊 Telemetry DB: tối đa 1 bản ghi / ${TELEMETRY_SAVE_INTERVAL_MS / 60000} phút / device_id`);

    try {
        const res = await pool.query('SELECT NOW()');
        console.log('✅ Đã kết nối thành công PostgreSQL lúc:', res.rows[0].now);
        await initDB();
        await initUsersTable();
        await initAiContextTable();
    } catch (err) {
        console.error('❌ Lỗi kết nối DB:', err.stack);
    }
});