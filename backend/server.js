const express = require('express');
const mqtt = require('mqtt');
const { Pool } = require('pg'); 

const app = express();
app.use(express.json());

// =========================================================================
// 🗄️ 1. KẾT NỐI VÀ KHỞI TẠO DATABASE (POSTGRESQL TRÊN RENDER)
// =========================================================================
// ⚠️ NHỚ DÁN LINK EXTERNAL URL CỦA BẠN VÀO ĐÂY (Giữ nguyên ?sslmode=require ở cuối)
const DB_URL = "postgresql://smarthome_admin:dhhhPDhRzSkaO6NxeLAx5sxsKP9XNrju@dpg-d6t20rma2pns738h4tk0-a.singapore-postgres.render.com/smarthome_v72a?sslmode=require";

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
        
        // Tạo sẵn 2 thiết bị mẫu vào DB nếu bảng đang trống (giống mock data cũ của bạn)
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

// =========================================================================
// 🌐 2. KẾT NỐI MQTT (HIVEMQ BROKER)
// =========================================================================
const MQTT_BROKER = 'mqtt://broker.hivemq.com'; 
const client = mqtt.connect(MQTT_BROKER);

client.on('connect', () => {
    console.log('✅ Backend đã kết nối HiveMQ Broker!');
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
            
            // Giả sử payload từ ESP32 có dạng: 
            // { id: "device_002", state: "ON", temp: 30.5, hum: 60, soil: 40, rain: 0 }
            const isPoweredOn = (payload.state === "ON" || payload.relay_state === 1);
            const deviceId = payload.id || "device_002"; // Fallback nếu ESP gửi thiếu ID

            // 3.1. Cập nhật trạng thái thiết bị (Upsert: Có thì update, chưa có thì insert)
            await pool.query(`
                INSERT INTO devices (id, name, type, is_online, last_updated)
                VALUES ($1, $2, $3, $4, CURRENT_TIMESTAMP)
                ON CONFLICT (id) 
                DO UPDATE SET is_online = EXCLUDED.is_online, last_updated = CURRENT_TIMESTAMP;
            `, [deviceId, "Thiết bị ESP32", "NODE", isPoweredOn]);

            // 3.2. Lưu trữ dữ liệu cảm biến vào bảng telemetry (chỉ lưu nếu có gửi kèm nhiệt độ)
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

// Lấy danh sách thiết bị và trạng thái hiện tại
app.get('/devices', async (req, res) => {
    try {
        const result = await pool.query('SELECT * FROM devices ORDER BY id ASC');
        console.log("📤 App vừa gọi API lấy danh sách thiết bị");
        res.json(result.rows); // Trả về mảng dữ liệu thật từ DB
    } catch (err) {
        console.error("Lỗi lấy danh sách thiết bị", err);
        res.status(500).json({ error: "Lỗi Server" });
    }
});

// (MỚI) API Lấy lịch sử cảm biến để vẽ biểu đồ trên App
app.get('/telemetry/:deviceId', async (req, res) => {
    try {
        const { deviceId } = req.params;
        // Lấy 20 dòng dữ liệu gần nhất của thiết bị
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
        console.log('✅ Đã kết nối thành công với PostgreSQL (Render) lúc:', res.rows[0].now);
        await initDB(); // Chạy hàm tạo bảng
    } catch (err) {
        console.error('❌ Lỗi kết nối DB Render:', err.stack);
    }
});