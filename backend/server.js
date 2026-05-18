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

// ================= LẮNG NGHE MQTT VÀ ĐỒNG BỘ CHUẨN VÀO DATABASE =================
client.on('message', async (topic, message) => {
    if (topic === 'DACS3/esp32_to_app') {
        try {
            const payload = JSON.parse(message.toString());
            const deviceId = payload.id;

            if (!deviceId || deviceId === 'esp32_startup') return;

            // 1. Tính toán trạng thái thực tế từ Payload MQTT
            let isPoweredOn = true;
            let isAuto = false;

            if (payload.isOnline !== undefined) {
                isPoweredOn = payload.isOnline; // Bắt sự kiện Node rớt mạng từ Gateway
            }

            if (payload.state === 'AUTO') {
                isAuto = true;
                isPoweredOn = true; // Auto thì thiết bị vẫn được hiểu là đang hoạt động
            } else if (payload.state === 'ON' || payload.relay_state === 1) {
                isAuto = false;
                isPoweredOn = true;
            } else if (payload.state === 'OFF' || payload.relay_state === 0) {
                isAuto = false;
                isPoweredOn = false; // Tắt (hoặc thu giàn phơi vào)
            }

            // 2. GHI ĐÈ TRẠNG THÁI VÀO BẢNG DEVICES ĐỂ ĐỒNG BỘ VỚI APP
            if (deviceId === 'Esp32_Node_DACS3') {
                // Node Trung Tâm luôn sống (trừ khi rớt mạng), không có mode AUTO
                await pool.query(`
                    UPDATE devices 
                    SET is_online = $1, is_auto = false, last_updated = CURRENT_TIMESTAMP
                    WHERE id = $2
                `, [payload.isOnline !== false, deviceId]);

                // ĐẶC BIỆT: Bản tin của Node có chứa state của Giàn phơi
                if (payload.state !== undefined) {
                    await pool.query(`
                        UPDATE devices 
                        SET is_online = $1, is_auto = $2, last_updated = CURRENT_TIMESTAMP
                        WHERE id = 'device_dryer'
                    `, [isPoweredOn, isAuto]);
                    console.log(`💾 [DB Sync] Đã cập nhật Giàn phơi ('device_dryer') -> Bật/Tắt: ${isPoweredOn}, Auto: ${isAuto}`);
                }
            } else {
                // Các thiết bị ngoại vi khác (Gateway, Fan, Dryer tự ACK)
                await pool.query(`
                    UPDATE devices 
                    SET is_online = $1, is_auto = $2, last_updated = CURRENT_TIMESTAMP
                    WHERE id = $3
                `, [isPoweredOn, isAuto, deviceId]);
                console.log(`💾 [DB Sync] Cập nhật ${deviceId} -> Bật/Tắt: ${isPoweredOn}, Auto: ${isAuto}`);
            }

            // 3. Ghi dữ liệu Telemetry (Môi trường) nếu có
            if (payload.temp !== undefined) {
                if (shouldPersistTelemetry(deviceId)) {
                    await pool.query(`
                        INSERT INTO telemetry (device_id, temperature, humidity, soil_moisture, rain_detected, relay_state)
                        VALUES ($1, $2, $3, $4, $5, $6)
                    `, [
                        deviceId, 
                        payload.temp, 
                        payload.hum, 
                        payload.soil || 0, 
                        payload.rain !== undefined ? payload.rain : 0, 
                        isPoweredOn ? 1 : 0
                    ]);
                    console.log(`📊 TELEMETRY: Đã lưu dữ liệu môi trường từ [${deviceId}]`);
                }
            }
        } catch (error) {
            console.error("❌ Lỗi xử lý dữ liệu MQTT:", error);
        }
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

const PORT = 3000;
app.listen(PORT, async () => {
    console.log(`\n🚀 Backend Server đang chạy tại port: ${PORT}`);
    console.log(`📊 Telemetry DB: tối đa 1 bản ghi / ${TELEMETRY_SAVE_INTERVAL_MS / 60000} phút / device_id`);

    try {
        const res = await pool.query('SELECT NOW()');
        console.log('✅ Đã kết nối thành công PostgreSQL lúc:', res.rows[0].now);
        await initDB();
        await initUsersTable();
    } catch (err) {
        console.error('❌ Lỗi kết nối DB:', err.stack);
    }
});