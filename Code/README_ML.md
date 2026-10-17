# PlanKO IMU Dataset Collection & Machine Learning Suite

ระบบเก็บข้อมูล Dataset และฝึกโมเดล AI จำแนกท่าทาง (Action & Movement Phase Classification) ที่ถูกสแกนและนำเข้าอย่างสมบูรณ์จากโครงการต้นแบบ `D:\Arduino Ble 33\arduio-33-ble` สู่ระบบ PlanKO ESP32-C3

---

## 1. โครงสร้างไฟล์และเครื่องมือ ML (File Structure)

```text
Code/
├── config/
│   └── pose_labels.json          # Taxonomy ท่าทาง (Body parts, Mount sites, Phases, Actions)
├── models/
│   ├── pose_model/               # โมเดล Random Forest / SVM ที่ฝึกสำเร็จพร้อมใช้งาน
│   │   ├── model_bundle.joblib   # Serialized Model Pipeline
│   │   └── evaluation.json       # ผลประเมินความแม่นยำ F1, Precision, Recall
│   └── pose_model_wrist_...      # โมเดลทดสอบท่าข้อมือ 3-Class
├── tools/
│   ├── capture.py                # บันทึก Dataset สดผ่าน Serial Port
│   ├── imu_features.py           # สกัดคุณลักษณะ (Feature Extraction) หน้าต่างแบบเลื่อน (Causal Window 20Hz)
│   ├── train_model.py            # สคริปต์ฝึกโมเดล (Random Forest & RBF-SVM พร้อม Group-KFold)
│   ├── serve_model.py            # Local Inference API Server (Flask Port 8765)
│   ├── merge.py                  # สคริปต์รวมและจัด Time-alignment หลายไฟล์ CSV
│   └── view_imu.py               # ตัวจำลองและตรวจสอบข้อมูล IMU
├── whist/                        # โฟลเดอร์เก็บชุดข้อมูล CSV ตัวอย่าง (up.csv, down.csv, nomal.csv)
├── WEB/
│   └── index.html                # หน้าเว็บ 3D Realtime Monitor + Dataset CSV Recorder + Live AI Suite
├── model_test.html               # หน้าเว็บทดสอบโมเดล AI (Replay CSV & Live Inference)
└── requirements-ml.txt           # รายการไลบรารี Python สำหรับ Machine Learning
```

---

## 2. วิธีการติดตั้งสภาพแวดล้อม Python (Installation)

แนะนำให้ติดตั้งใน Virtual Environment:

```powershell
# ติดตั้งไลบรารีสำหรับ Machine Learning
pip install -r requirements-ml.txt
```

*(ไลบรารีหลัก: `numpy>=1.24`, `pandas>=2.0`, `scikit-learn>=1.3`, `joblib>=1.3`, `Flask>=3.0`)*

---

## 3. วิธีการบันทึก Dataset (3 ช่องทาง)

### ช่องทางที่ 1: บันทึกผ่านหน้าเว็บมอนิเตอร์ (สะดวกที่สุด)
1. เปิดไฟล์ `WEB/index.html` ในเบราว์เซอร์
2. เลื่อนลงมาที่แท็บ **"📂 บันทึกชุดข้อมูล Dataset (CSV Recorder)"**
3. เลือก:
   * **ตำแหน่งติดเซนเซอร์ (Mount Site)**: เช่น `neck` (ลำคอ), `lumbar` (หลังส่วนล่าง), `wrist` (ข้อมือ)
   * **ส่วนของร่างกาย (Body Part)**: เช่น ลำคอ, หลัง/เอว, ข้อมือ
   * **ท่าทาง (Action Label)**: เช่น `neck_flexion` (ก้มคอ), `neck_extension` (เงยคอ), `wrist_up`
   * กรอก `Subject ID`, `Session ID`, `Trial ID`
4. ติ๊กเปิด **"ใช้ตัวช่วยนำจังหวะอัตโนมัติ (Guided Phase Timer)"**
5. กดปุ่ม **🔴 เริ่มบันทึก (Start Recording)**
   * ระบบจะมีเสียง Beep นำจังหวะ: `neutral (3s)` → `approaching (3s)` → `holding (5s)` → `returning (3s)` → `neutral (2s)`
   * เมื่อเสร็จสิ้น เบราว์เซอร์จะ **ดาวน์โหลดไฟล์ CSV มาตรฐาน** ให้โดยอัตโนมัติทันที

### ช่องทางที่ 2: บันทึกผ่านหน้าเว็บ Gateway ESP32 (Wi-Fi)
* เชื่อมต่อ Wi-Fi หรือเข้าผ่าน IP ของ Gateway เช่น `http://192.168.1.4/`
* หน้าเว็บ Gateway จะมีระบบบันทึกแบบ Multi-Node แยก CSV ตาม Node 1 - 4

### ช่องทางที่ 3: บันทึกผ่าน Serial Port บนคอมพิวเตอร์ (CLI)
```powershell
python tools/capture.py --port COM7 --output imu_output `
  --mount-site neck --body-part neck --side center `
  --subject-id P01 --session-id S01 --trial-id trial-01 `
  --action-label neck_flexion
```

---

## 4. โครงสร้าง Feature ที่สกัดสำหรับ AI (`tools/imu_features.py`)

อัลกอริทึมสกัดคุณลักษณะใช้ **Causal Sliding Window (0.5s - 1.0s @ 20Hz)** โดยคำนวณ:
1. **สถิติของ 6 แกนดิบ (Accel X,Y,Z / Gyro X,Y,Z)**:
   * `mean` (ค่าเฉลี่ย)
   * `std` (ความเบี่ยงเบนมาตรฐาน)
   * `rms` (Root Mean Square)
   * `range` (ผลต่าง Max - Min)
   * `slope_per_s` (ความชันเชิงเส้นของการเคลื่อนไหว)
2. **ขนาดเวกเตอร์ลัพธ์ (Vector Magnitude)**:
   * `accel_magnitude_mean`, `accel_magnitude_std`, `accel_magnitude_rms`
   * `gyro_magnitude_mean`, `gyro_magnitude_std`, `gyro_magnitude_rms`
3. **Mount-Site & Body-Part Gating**:
   * One-hot encode ป้องกันไม่ให้แกนเซนเซอร์จากต่างตำแหน่งปะปนกัน
4. **Calibrated Relative Angles**:
   * ดึงมุมสัมพัทธ์ `relative_roll_deg`, `relative_pitch_deg` เมื่อมีการ Calibrate เซนเซอร์

---

## 5. วิธีการเทรนโมเดล (Model Training)

สั่งรันคำสั่งเทรนโมเดลด้วย `train_model.py`:

```powershell
# ตัวอย่าง: เทรนจากไฟล์ CSV ในโฟลเดอร์ whist/
python tools/train_model.py whist/*.csv --window 1.0 --output models/pose_model

# ตัวอย่าง: เทรนจากชุดข้อมูลที่บันทึกมาใหม่
python tools/train_model.py imu_output/*.csv --window 1.0 --output models/my_new_model
```

* ตัวฝึกโมเดลจะทดสอบทั้ง **Random Forest** และ **RBF-SVM**
* ประเมินผลแบบ **Group-separated K-Fold (StratifiedGroupKFold)** ตาม `session_id` หรือ `subject_id` ป้องกัน Data Leakage
* บันทึกโมเดลที่เลือกไว้ที่ `models/pose_model/model_bundle.joblib`
* รายงานผลความแม่นยำ per-class Precision, Recall, F1-Score ใน `models/pose_model/evaluation.json`

---

## 6. วิธีการเปิด Live Inference Server (ทดสอบ AI สด)

เปิด Local Inference Server ด้วยคำสั่ง:

```powershell
python tools/serve_model.py --model models/pose_model/model_bundle.joblib --port 8765
```

เมื่อเซิร์ฟเวอร์รันบนพอร์ต 8765:
* เปิด `WEB/index.html` แล้วคลิกแท็บ **"🧠 ทดสอบโมเดล AI (Live Model Inference)"** แล้วกดปุ่ม **"▶ เริ่มตรวจจับสด"**
* หรือเปิด `model_test.html` ในเบราว์เซอร์เพื่อดูรายละเอียดกราฟและ Timeline การทำนายท่าทางแบบ Real-time
