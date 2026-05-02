require('dotenv').config(); // 👈 Thêm dòng này lên đỉnh file để đọc file .env

const express = require('express');
const mqtt = require('mqtt');
const { Pool } = require('pg');
const bcrypt = require('bcrypt');
const jwt = require('jsonwebtoken');

// JWT config
const JWT_SECRET = 'your-super-secret-key-change-in-production';
const SALT_ROUNDS = 10; 

const app = express();
app.use(express.json());

// =========================================================================
// 🗄️ 1. KẾT NỐI VÀ KHỞI TẠO DATABASE (POSTGRESQL TRÊN SUPABASE)
// =========================================================================
// 👈 Gọi đường link từ file .env ra thay vì viết cứng
const DB_URL = process.env.DATABASE_URL;

const pool = new Pool({
    connectionString: DB_URL,
});

// Hàm tự động tạo bảng nếu chưa có
const initDB = async () => {
    try {
        await pool.query(`
            -- Bảng lưu danh sách và trạng thái thiết bị
            CREATE TABLE IF NOT EXISTS devices (
                id VARCHAR(50) PRIMARY KEY,
                name VARCHAR(100),
                type VARCHAR(50),
                is_online BOOLEAN DEFAULT false,
                last_updated TIMESTAMP DEFAULT CURRENT_TIMESTAMP
            );

            -- Bảng lưu lịch sử dữ liệu cảm biến (Telemetry)
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
        console.log("✅ Đã khởi tạo cấu trúc Bảng (Schema) thành công!");

        // Tạo sẵn 2 thiết bị mẫu vào DB nếu bảng đang trống
        await pool.query(`
            INSERT INTO devices (id, name, type, is_online)
            VALUES
                ('device_001', 'Đèn phòng khách', 'LIGHT', false),
                ('device_002', 'Máy bơm vườn', 'PUMP', true)
            ON CONFLICT (id) DO NOTHING;
        `);
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
        console.log("✅ Đã khởi tạo bảng users thành công!");
    } catch (err) {
        console.error("❌ Lỗi khi khởi tạo bảng users:", err);
    }
};

// =========================================================================
// 🔐 AUTH MIDDLEWARE & HELPERS
// =========================================================================
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

// =========================================================================
// 🌐 2. KẾT NỐI MQTT (HIVEMQ BROKER)
// =========================================================================
const MQTT_BROKER = 'mqtt://broker.emqx.io'; 
const client = mqtt.connect(MQTT_BROKER);

client.on('connect', () => {
    console.log('✅ Backend đã kết nối EMQX Broker!');
    client.subscribe('DACS3/esp32_to_app');
});

// =========================================================================
// 🔄 3. ĐỒNG BỘ REAL-TIME TỪ ESP32 VÀO POSTGRESQL
// =========================================================================
client.on('message', async (topic, message) => {
    if (topic === 'DACS3/esp32_to_app') {
        try {
            const payload = JSON.parse(message.toString());
            console.log("📥 MQTT NHẬN TỪ ESP32:", payload);
            
            const isPoweredOn = (payload.state === "ON" || payload.relay_state === 1);
            const deviceId = payload.id || "device_002";

            // 3.1. Cập nhật trạng thái thiết bị
            await pool.query(`
                INSERT INTO devices (id, name, type, is_online, last_updated)
                VALUES ($1, $2, $3, $4, CURRENT_TIMESTAMP)
                ON CONFLICT (id) 
                DO UPDATE SET is_online = EXCLUDED.is_online, last_updated = CURRENT_TIMESTAMP;
            `, [deviceId, "Thiết bị ESP32", "NODE", isPoweredOn]);

            // 3.2. Lưu trữ dữ liệu cảm biến
            if (payload.temp !== undefined) {
                await pool.query(`
                    INSERT INTO telemetry (device_id, temperature, humidity, soil_moisture, rain_detected, relay_state)
                    VALUES ($1, $2, $3, $4, $5, $6)
                `, [deviceId, payload.temp, payload.hum, payload.soil, payload.rain, isPoweredOn ? 1 : 0]);
                console.log(`💾 ĐÃ LƯU DB: Cập nhật cảm biến cho [${deviceId}]`);
            } else {
                console.log(`💾 ĐÃ LƯU DB: Cập nhật trạng thái [${isPoweredOn ? 'ON' : 'OFF'}] cho [${deviceId}]`);
            }

        } catch (error) {
            console.error("❌ Lỗi xử lý dữ liệu MQTT / Database:", error);
        }
    }
});

// =========================================================================
// 📱 4. API CHO APP GỌI LÊN LẤY DỮ LIỆU
// =========================================================================

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
        console.error('Register error:', err);
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
        console.error('Login error:', err);
        res.status(500).json({ error: 'Server error' });
    }
});

app.get('/devices', async (req, res) => {
    try {
        const result = await pool.query('SELECT * FROM devices ORDER BY id ASC');
        console.log("📤 App vừa gọi API lấy danh sách thiết bị");
        res.json(result.rows);
    } catch (err) {
        console.error("Lỗi lấy danh sách thiết bị", err);
        res.status(500).json({ error: "Lỗi Server" });
    }
});

app.get('/telemetry/:deviceId', async (req, res) => {
    try {
        const { deviceId } = req.params;
        const result = await pool.query(
            'SELECT * FROM telemetry WHERE device_id = $1 ORDER BY created_at DESC LIMIT 20',
            [deviceId]
        );
        res.json(result.rows);
    } catch (err) {
        console.error("Lỗi lấy lịch sử", err);
        res.status(500).json({ error: "Lỗi Server" });
    }
});

// =========================================================================
// 🚀 5. KHỞI CHẠY SERVER
// =========================================================================
const PORT = 3000;
app.listen(PORT, async () => {
    console.log(`\n🚀 Backend Server đang chạy tại port: ${PORT}`);
    
    // Test kết nối DB ngay khi chạy server
    try {
        const res = await pool.query('SELECT NOW()');
        console.log('✅ Đã kết nối thành công với PostgreSQL (Supabase) lúc:', res.rows[0].now); // 👈 Đã sửa thành Supabase
        await initDB(); 
        await initUsersTable(); 
    } catch (err) {
        console.error('❌ Lỗi kết nối DB Supabase:', err.stack); // 👈 Đã sửa thành Supabase
    }
});