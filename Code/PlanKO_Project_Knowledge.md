# PlanKO — Master Project Knowledge Base (สำหรับ Gemini & ChatGPT)

> **คำสั่งสำหรับ AI (System Instruction สำหรับ Gemini / ChatGPT):**
> เอกสารนี้คือข้อมูลอ้างอิงจริง 100% จากซอร์สโค้ดและชุดข้อมูล (Dataset) ของโปรเจกต์ **PlanKO (Smart Plank Training & Posture AI System)** เวลาตอบคำถาม สรุปรายงาน ทำสไลด์ หรือสร้างอินโฟกราฟิก **ห้ามเดาชื่อคอลัมน์หรือตัวเลขเอง** ให้ยึดตามชื่อตัวแปร โครงสร้าง CSV 43 คอลัมน์ และตัวเลขสถิติในเอกสารนี้เท่านั้น

---

## 1. ภาพรวมระบบและสถาปัตยกรรม (System Architecture)

**PlanKO** คือระบบตรวจจับและวิเคราะห์ท่าทางการทำแพลงก์ (Plank Posture Classification & Phase Tracking) แบบเรียลไทม์ด้วยเซ็นเซอร์วัดความเร่งและความเร็วเชิงมุม (IMU) หลายจุดบนร่างกายร่วมกับโมเดล Machine Learning

```
[IMU Node #1: หลังบน (upper_back)] ──(BLE 20Hz)──┐
[IMU Node #2: หลังล่าง (lumbar)]   ──(BLE 20Hz)──┼──> [ESP32 Gateway] ──(Wi-Fi MQTT)──> [Mosquitto Broker] ──(WebSocket)──> [Web Recorder & AI Server]
[IMU Node #3: ข้อมือ (wrist)]      ──(BLE 20Hz)──┘
```

### 1.1 ฮาร์ดแวร์และอุปกรณ์ (Hardware & Roles)
1. **IMU Nodes (Node #1, #2, #3)**:
   - บอร์ด: **ESP32-C3 SuperMini** (`esp32-c3-devkitm-1`)
   - เซ็นเซอร์: **MPU-6050 (6-DOF IMU)** เชื่อมต่อผ่าน I2C (`SDA = GPIO 8`, `SCL = GPIO 9`, ความถี่ I2C `100 kHz`)
   - ย่านการวัดของ MPU-6050:
     - Accelerometer: `±4g` (`8192 LSB/g`, แปลงเป็นหน่วย SI: `1g = 9.80665 m/s²`)
     - Gyroscope: `±500 deg/s` (`65.5 LSB/(deg/s)`, แปลงเป็นหน่วย SI: `rad/s`)
   - การคำนวณมุมเอียงบนบอร์ด: ใช้ **Complementary Filter** (`96% Gyroscope + 4% Accelerometer`) คำนวณค่า `Pitch`, `Roll`, `Yaw` (หน่วยองศา)
   - ความถี่การสุ่มสัญญาณ (Sampling Rate): **20 Hz** (ส่งข้อมูลทุกๆ `50 ms`)
   - การส่งข้อมูล: ส่งผ่าน **Bluetooth Low Energy (BLE GATT Notification)** ในแพ็กเก็ตไบนารี `CombinedImuPacket` ขนาด `33 bytes` ต่อตัวอย่าง (ใช้ ATT MTU = 64)
   - รหัสประจำตัวเซ็นเซอร์จริง (`sensor_uid` / MAC Address):
     - **Node #1 (`PlanKO_IMU_1`)**: `ac276e5c3112` — ติดตั้งที่ **หลังส่วนบน (`mount_site = upper_back`, `body_part = back`)**
     - **Node #2 (`PlanKO_IMU_2`)**: `ac276e5c2d6a` — ติดตั้งที่ **หลังส่วนล่าง/เอว (`mount_site = lumbar`, `body_part = lumbar`)**
     - **Node #3 (`PlanKO_IMU_3`)**: `ac276e5c0f7a` — ติดตั้งที่ **หลังมือ/ข้อมือ (`mount_site = wrist`, `body_part = wrist`)**

2. **Gateway (`ROLE_GATEWAY`)**:
   - มี 2 โหมดใน `platformio.ini`:
     - **`gateway` (โหมดปกติ)**: รับเฉพาะข้อมูล BLE จาก IMU Node ทั้ง 3 ตัว (ปิดการอ่านค่าแรงกด RFP)
     - **`gateway_rfp605`**: รับข้อมูล BLE จาก IMU Node 3 ตัว + อ่านเซ็นเซอร์วัดแรงกดแบบฟิล์มบาง **RFP-605** ผ่านชิปมัลติเพล็กเซอร์ **CD74HC4067** (`ADC = GPIO 32`, `S0 = 25, S1 = 26, S2 = 27, S3 = 14`, `ACTIVE_MASK = 0x003F`)
   - ส่งข้อมูลรวมของทุก Node ขึ้น **MQTT Broker (Mosquitto)** ที่ Topic `planko/imu` ด้วยความถี่ **20 Hz** (ใช้ Schema `planko-imu-v2`, MQTT Buffer `8192 bytes`)

3. **Web Dashboard & Dataset Recorder (`WEB/index.html`)**:
   - เชื่อมต่อกับ Mosquitto Broker ผ่าน **MQTT over WebSockets (Port 9001, Path `/mqtt`)**
   - แสดงผลโมเดล 3D แบบเรียลไทม์, แสดงสถานะแบตเตอรี่/ความถี่ของแต่ละ Node และบันทึกข้อมูลลงไฟล์ **CSV แยกตาม Node** พร้อมไฟล์สรุป **`_manifest.json`**

---

## 2. โครงสร้างไฟล์ CSV ครบทั้ง 43 คอลัมน์ (Data Dictionary)

ทุกไฟล์ CSV ที่บันทึกจากหน้าเว็บมี **43 คอลัมน์** เรียงตามลำดับดังนี้ (ห้ามสลับลำดับ):

```csv
gateway_id,gateway_boot_id,mqtt_seq,node_id,sensor_uid,mount_site,body_part,firmware_body_part,side,subject_id,session_id,trial_id,action_label,phase_label,phase_annotation_source,phase_annotation_reviewed,host_time_iso,host_time_s,host_elapsed_ms,boot_ms,device_time_s,boot_id,seq,stream_epoch,gap_before,missing_samples_before,accel_unit,gyro_unit,accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z,roll_deg,pitch_deg,yaw_deg,calibrated,relative_orientation_valid,calibration_id,calibration_method_version,valid,firmware_status
```

### ตารางอธิบายความหมายทั้ง 43 คอลัมน์ พร้อมค่าตัวอย่างจริง

| ลำดับ | ชื่อคอลัมน์ (Header) | ตัวอย่างค่า (Node 1) | ตัวอย่างค่า (Node 2) | ความหมายและหน่วย |
| :---: | :--- | :--- | :--- | :--- |
| **1** | `gateway_id` | `PlanKO_GW_2E5C3314` | `PlanKO_GW_5C6E27AC` | รหัสเครื่อง Gateway ตัวแม่ข่าย (สร้างจาก MAC Address ของ Gateway) |
| **2** | `gateway_boot_id` | `2073968700` | `2737441619` | รหัสสุ่มประจำรอบการเปิดเครื่องของ Gateway |
| **3** | `mqtt_seq` | `2512` | `4215` | เลขลำดับแพ็กเก็ต MQTT ที่ Gateway ส่งออก |
| **4** | `node_id` | `1` | `2` | หมายเลขของเซ็นเซอร์ (`1`, `2`, หรือ `3`) |
| **5** | `sensor_uid` | `ac276e5c3112` | `ac276e5c2d6a` | MAC Address ของบอร์ดเซ็นเซอร์ IMU Node ตัวนั้น |
| **6** | `mount_site` | `upper_back` | `lumbar` | ตำแหน่งที่ติดตั้งบนร่างกาย (`upper_back`, `lumbar`, `wrist`) |
| **7** | `body_part` | `back` | `lumbar` | หมวดส่วนของร่างกายที่ใช้จัดกลุ่มเทรนโมเดล AI |
| **8** | `firmware_body_part` | `back` | `back` | ชื่อส่วนของร่างกายตามที่ตั้งไว้ใน Firmware ของ Gateway |
| **9** | `side` | `unknown` | `unknown` | ด้านซ้าย/ขวา (`left`, `right`, หรือ `unknown` สำหรับกึ่งกลางตัว) |
| **10** | `subject_id` | `P01` | `P01` | รหัสผู้เข้าร่วมการทดลอง (เช่น `P01`, `P02`) |
| **11** | `session_id` | `S01` | `S01` | รหัสเซสชันการเก็บข้อมูล (เช่น `S01`–`S04`) |
| **12** | `trial_id` | `trial-01` | `trial-01` | รอบการทดลองย่อย |
| **13** | `action_label` | `case_neutral` | `case_neutral` | **ป้ายกำกับท่าทาง (Target Class สำหรับเทรน AI)** เช่น `case_neutral`, `elbow_neutral`, `hand_neutral` |
| **14** | `phase_label` | `neutral` | `neutral` | ช่วงจังหวะของท่า (`neutral`, `approaching`, `holding`, `returning`) |
| **15** | `phase_annotation_source` | `manual_live` | `manual_live` | ที่มาของป้าย Phase (`manual_live` = กดเลือกสด, `guided_timer_cue` = ตัวจับเวลา) |
| **16** | `phase_annotation_reviewed` | `false` | `false` | สถานะการตรวจทานขอบเขต Phase ย้อนหลัง (`true` / `false`) |
| **17** | `host_time_iso` | `2026-11-11T10:25:38.408Z` | `2026-11-14T09:35:34.192Z` | วัน-เวลาที่คอมพิวเตอร์รับข้อมูล (UTC ISO 8601; เวลาไทย = +7 ชั่วโมง) |
| **18** | `host_time_s` | `1794392738.408` | `1794648934.192` | เวลาคอมพิวเตอร์ในหน่วยวินาที (Unix Timestamp) |
| **19** | `host_elapsed_ms` | `15551` | `3224` | เวลาที่ผ่านไปนับจากกดปุ่มเริ่มบันทึก (มิลลิวินาที) |
| **20** | `boot_ms` | `1090750` | `2319` | เวลา `millis()` ภายในบอร์ด IMU Node นับตั้งแต่เปิดเครื่อง (เพิ่มทีละ ~50 ms) |
| **21** | `device_time_s` | `1090.75` | `2.319` | เวลาภายในบอร์ด IMU Node แปลงเป็นวินาที (`boot_ms / 1000`) |
| **22** | `boot_id` | `3178123805` | `2257346488` | รหัสสุ่มของการเชื่อมต่อ BLE รอบนั้น (เปลี่ยนเมื่อ BLE หลุดแล้วต่อใหม่) |
| **23** | `seq` | `21152` | `15` | เลขลำดับแพ็กเก็ตจากบอร์ด IMU Node (เพิ่มทีละ 1 ทุกๆ 50 ms) |
| **24** | `stream_epoch` | `PlanKO_GW_...:3178123805` | `PlanKO_GW_...:2257346488` | รหัสรวม `gateway_id:gateway_boot_id:boot_id` ใช้แยกสตรีมต่อเนื่อง |
| **25** | `gap_before` | `false` | `false` | `true` หากมีแพ็กเก็ตขาดหายก่อนหน้าแถวนี้, `false` หากต่อเนื่องปกติ |
| **26** | `missing_samples_before` | `0` | `0` | จำนวนตัวอย่างที่ขาดหายไปก่อนหน้าแถวนี้ |
| **27** | `accel_unit` | `m/s^2` | `m/s^2` | หน่วยของความเร่ง (`m/s^2`) |
| **28** | `gyro_unit` | `rad/s` | `rad/s` | หน่วยของความเร็วเชิงมุม (`rad/s`) |
| **29** | `accel_x` | `0.25` | `0.96` | ความเร่งแกน X (`m/s²`) |
| **30** | `accel_y` | `-0.01` | `-0.66` | ความเร่งแกน Y (`m/s²`) |
| **31** | `accel_z` | `9.63` | `10.04` | ความเร่งแกน Z (`m/s²` — รับแรงโน้มถ่วงโลก ~9.81 m/s² เมื่อวางขนานพื้น) |
| **32** | `gyro_x` | `0.019985` | `-0.034107` | ความเร็วการหมุนรอบแกน X (`rad/s`) |
| **33** | `gyro_y` | `-0.011191` | `0.099657` | ความเร็วการหมุนรอบแกน Y (`rad/s`) |
| **34** | `gyro_z` | `-0.029577` | `-0.065283` | ความเร็วการหมุนรอบแกน Z (`rad/s`) |
| **35** | `roll_deg` | `-1.35` | `-3.05` | มุมเอียงซ้าย-ขวา (Roll, หน่วยองศา) |
| **36** | `pitch_deg` | `-2.4` | `3.25` | มุมก้ม-เงย (Pitch, หน่วยองศา) |
| **37** | `yaw_deg` | `116.33` | `-2.8` | มุมหันรอบแกนตั้ง (Yaw, หน่วยองศา) |
| **38** | `calibrated` | `true` | `true` | เซ็นเซอร์ผ่านการหักค่า Bias Offset ตอนเปิดเครื่องแล้ว (`true`) |
| **39** | `relative_orientation_valid` | `false` | `false` | สถานะการใช้มุมสัมพัทธ์ข้ามบอร์ด (`false` = ใช้ฟีเจอร์ 6 แกนมาตรฐาน) |
| **40** | `calibration_id` | *(ว่าง)* | *(ว่าง)* | รหัสโปรไฟล์การทำ Relative Calibration |
| **41** | `calibration_method_version` | `0` | `0` | เวอร์ชันของ Relative Calibration (`0` = ไม่ได้ใช้) |
| **42** | `valid` | `true` | `true` | ข้อมูลสมบูรณ์และพร้อมนำไปสกัดฟีเจอร์เทรน AI (`true`) |
| **43** | `firmware_status` | `real_ble` | `real_ble` | แหล่งที่มาของข้อมูล (`real_ble` = ข้อมูลจริงจากบอร์ดผ่าน BLE) |

---

## 3. กระบวนการสกัดฟีเจอร์และเทรนโมเดล AI (Machine Learning Pipeline)

1. **การแบ่งหน้าต่างเวลา (`tools/imu_features.py`)**:
   - ใช้ Sliding Window ขนาด **`1.0 วินาที`** (ที่ความถี่ `20 Hz` = ~20 ตัวอย่างต่อหน้าต่าง)
   - หากมีข้อมูลขาดหายเกิน **`0.15 วินาที`** (`gap_reset_seconds`) หรือเปลี่ยน `stream_epoch` ระบบจะตัดหน้าต่างเวลานั้นทิ้งเพื่อไม่ให้ข้อมูลข้ามช่วงมาต่อกันผิดเพี้ยน
   - สกัดค่าทางสถิติในแต่ละหน้าต่างเวลาสำหรับทั้ง 6 แกน (`accel_x, y, z`, `gyro_x, y, z` และขนาดเวกเตอร์รวม): ค่าเฉลี่ย (`mean`), ส่วนเบี่ยงเบนมาตรฐาน (`std`), ค่าต่ำสุด (`min`), ค่าสูงสุด (`max`), พิสัย (`range`), พลังงาน (`rms`), และความชันการเปลี่ยนแปลง (`slope_per_s`) พร้อม One-hot encoding ของ `mount_site`, `body_part`, `side`

2. **อัลกอริทึมที่ใช้เทรนเปรียบเทียบ (`tools/train_model.py`)**:
   - **SVM (RBF Kernel)**: `StandardScaler() + SVC(C=2.0, kernel="rbf", class_weight="balanced", probability=True)` -> **ชนะการคัดเลือกเป็นโมเดลหลัก (`pose-svm-rbf-v1`)**
   - **Random Forest**: `RandomForestClassifier(n_estimators=300, max_depth=10, min_samples_leaf=2, class_weight="balanced_subsample")`

---

## 4. สถิติผลการเทรนโมเดลครบทุกรอบ (อัปเดตถึงชุดข้อมูล `2026-11-14`)

### 4.1 สรุปผลแยกตามวันที่เก็บข้อมูล (Per-Date Breakdown)
| รอบวันที่ (Date) | จำนวนไฟล์ (Windows) | ตำแหน่ง Node (`sites`) | ท่าที่มีในรอบนั้น (`actions`) | Accuracy (`Subject CV`) | Accuracy (`Trial CV`) | หมายเหตุ |
| :--- | :---: | :--- | :--- | :---: | :---: | :--- |
| **`2026-10-25`** | 10 ไฟล์ (`158` win) | `neck`, `lumbar` (2 Nodes) | `case_extension`, `case_flexion`, `elbow_extension`, `elbow_neutral`, `hand_neutral` | วัดผลไม่ได้ *(ท่าละ 1 Trial)* | **0.00%** | ข้อมูล 2 Nodes รุ่นแรก มีท่าละ 1 รอบ |
| **`2026-11-01`** *(ใน `multinode/`)* | 18 ไฟล์ (`542` win) | `neck`, `lumbar` (2 Nodes) | `elbow_neutral`, `elbow_flexion`, `elbow_extension`, `hand_flexion`, `case_extension` | **13.33%** (RF) | **13.47%** (SVM) | ข้อมูล 2 Nodes รุ่นทดสอบ |
| **`2026-11-05`** | 28 ไฟล์ (`1,417` win) | `upper_back`, `lumbar`, `wrist` (**3 Nodes**) | `elbow_neutral`, `hand_neutral` | **66.56%** (SVM) | **69.87%** (RF) / **69.16%** (SVM) | เริ่มใช้ **3 Nodes** เต็มรูปแบบ |
| **`2026-11-11`** | 7 ไฟล์ (`95` win) | `upper_back`, `lumbar`, `wrist` (**3 Nodes**) | `case_neutral` | — *(มี 1 ท่า)* | — *(มี 1 ท่า)* | เก็บเพิ่มของ `P01` |
| **`2026-11-12`** | 9 ไฟล์ (`896` win) | `upper_back`, `lumbar`, `wrist` (**3 Nodes**) | `case_neutral` (`S01–S03`) | — *(มี 1 ท่า)* | — *(มี 1 ท่า)* | เก็บเพิ่มของ `P01` |
| **`2026-11-13`** | 12 ไฟล์ (`914` win) | `upper_back`, `lumbar`, `wrist` (**3 Nodes**) | `case_neutral` (`S01–S04`) | — *(มี 1 ท่า)* | — *(มี 1 ท่า)* | เก็บเพิ่มของ `P01` |
| **`2026-11-14`** | 9 ไฟล์ (`904` win) | `upper_back`, `lumbar`, `wrist` (**3 Nodes**) | `case_neutral` (`S01–S03`) | — *(มี 1 ท่า)* | — *(มี 1 ท่า)* | เก็บเพิ่มของ `P01` (สัญญาณนิ่งมาก ~1,040 แถว/ไฟล์) |

### 4.2 พัฒนาการของ Accuracy ในชุดข้อมูล 3 Nodes (Cumulative Progression)
| ครั้งที่เทรน (ข้อมูล 3 Nodes สะสม) | จำนวนไฟล์ (Windows) | Accuracy (SVM `Trial CV`) | Accuracy (RF `Trial CV`) | Macro F1 (SVM `Trial CV`) | Accuracy (SVM `Subject CV`) |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **ครั้งที่ 1** (`2026-11-05`) | 28 ไฟล์ (`1,417` win) | **69.16%** | **69.87%** | **68.99%** | **66.56%** |
| **ครั้งที่ 2** (`+ 2026-11-11`) | 35 ไฟล์ (`1,512` win) | **68.45%** | **67.20%** | **52.10%** | **56.52%** |
| **ครั้งที่ 3** (`+ 2026-11-12`) | 44 ไฟล์ (`2,408` win) | **72.97%** | **69.68%** | **72.64%** | **60.19%** |
| **ครั้งที่ 4** (`+ 2026-11-13`) | 56 ไฟล์ (`3,322` win) | **80.70%** | **77.97%** | **78.72%** | **59.87%** |
| **ครั้งที่ 5 (ล่าสุด `+ 2026-11-14`)** | **65 ไฟล์ (`4,226` win)** | **80.62%** | **80.15%** | **76.79%** | **60.83%** |

*(หมายเหตุ: ถ้ารวมไฟล์เก่า 2 Nodes ของวันที่ `2026-10-25` อีก 10 ไฟล์ เป็น `75 ไฟล์ / 4,384 windows` จะได้ SVM Trial CV = **`77.21%`**, Subject CV = **`55.07%`**)*

### 4.3 ความแม่นยำแยกตามตำแหน่งเซ็นเซอร์ (Per-Node Accuracy จาก `4,226` windows)
| ตำแหน่งเซ็นเซอร์ (`mount_site`) | จำนวนข้อมูลที่สมบูรณ์ (Windows) | Accuracy (SVM-RBF) | วิเคราะห์ผล |
| :--- | :---: | :---: | :--- |
| **Node #1: หลังส่วนบน (`upper_back`)** | `1,461` windows | **95.55%** | แม่นยำสูงสุด แยกท่าแพลงก์ได้ชัดเจนมาก |
| **Node #3: ข้อมือ (`wrist`)** | `1,413` windows | **92.07%** | แม่นยำสูงมากในการแยกการวางแขน/ข้อมือ |
| **Node #2: หลังส่วนล่าง (`lumbar`)** | `1,352` windows | **52.51%** | ต่ำกว่าจุดอื่นเพราะ: (1) ในช่วงแรกบอร์ด Node 2 มีสัญญาณขาด/รีบูตบ่อยที่สุด ทำให้เหลือหน้าต่างข้อมูลน้อยที่สุด (หายไป 109 windows เทียบกับ Node 1) และ (2) มุมเอวของท่า `elbow_neutral` กับ `case_neutral` ขนานพื้นเหมือนกัน ทำให้ทายสลับกันเองระหว่างท่าปกติสองชื่อนี้ |
| **รวมเฉลี่ยทั้ง 3 Nodes** | **`4,226` windows** | **80.62%** | ค่าเฉลี่ยถ่วงน้ำหนักของทั้ง 3 ตำแหน่ง |

---

## 5. รายละเอียดส่วนประกอบอื่นๆ ทั้งหมดในโปรเจกต์ (Complete Subsystems Overview)

นอกจากส่วนของการเทรน AI แล้ว โปรเจกต์ **PlanKO** ยังประกอบด้วยระบบย่อยทั้งหมดดังนี้:

1. **ระบบวัดชีพจรและออกซิเจนในเลือด (`MAX30102` บน Node #1)**:
   - เชื่อมต่อผ่านบัส I2C เดียวกับ MPU-6050 (`SDA = 8, SCL = 9`, Address `0x57`)
   - อ่านค่าแสง `Red` และ `IR`, ตรวจจับการวางนิ้ว (`currentIr >= 50000`) และคำนวณค่า **Heart Rate (BPM)** และ **SpO2 (%)**
   - ส่งผ่าน BLE Notification ในแพ็กเก็ต `BioPacket` (`20 bytes`, Marker `0xB4`) ทุกๆ `250 ms` (`4 Hz`) และ Gateway ส่งต่อขึ้น MQTT Topic `planko/bio`

2. **ระบบวัดแรงดันแบตเตอรี่ของแต่ละ Node (`BatteryPacket`)**:
   - อ่านแรงดันผ่านวงจรแบ่งแรงดันที่ขา `GPIO 4` (`BATTERY_ADC_PIN = 4`, แรงดันแบต Li-Po `3300 mV = 0%` ถึง `4200 mV = 100%`)
   - ส่งผ่าน BLE ในแพ็กเก็ต `BatteryPacket` (`5 bytes`, Marker `0xD4`) ทุกๆ `5,000 ms` (`5 วินาที`) และแสดงผลกราฟแบตเตอรี่ของแต่ละ Node บนหน้าเว็บ `WEB/index.html`

3. **ระบบแผ่นวัดแรงกด `RFP-605` 16 ช่องสัญญาณผ่านชิป `CD74HC4067` (`rfp605_sandbox.cpp`)**:
   - ใช้ในโหมด `rfp605_sandbox` และ `gateway_rfp605` (บอร์ด `esp32dev`)
   - ใช้ขาควบคุมมัลติเพล็กเซอร์ `S0 = GPIO 25, S1 = GPIO 26, S2 = GPIO 27, S3 = GPIO 14` และอ่านค่า Analog ที่ขา `SIG = GPIO 32` (`ACTIVE_MASK = 0x003F` คือเปิดใช้งานช่อง `C0..C5`)
   - มีการทำ Auto-baseline Calibration ตอนเปิดเครื่อง (`20 samples`) และตรวจจับสถานะการกด/ปล่อยด้วย Hysteresis (`PRESS_DELTA = 100`, `RELEASE_DELTA = 50`) แล้วส่งขึ้น MQTT Topic `planko/pressure`

4. **ระบบแผ่นรองแพลงก์ 16 ปุ่ม (`L1–L8`, `R1–R8`) และ Serial/MQTT Simulator**:
   - รองรับการกดปุ่มจากแผ่นรองหรือจำลองการกดผ่าน Serial Monitor (`พิมพ์ L1..R8`) หรือสั่งผ่าน MQTT Command Topic (`planko/gateway/<clientId>/cmd`) และส่งอีเวนต์ออกทาง Topic `planko/pad`
   - มีหน้าเว็บ `remote.html` สำหรับจำลองและควบคุมปุ่มแผ่นรองแพลงก์ระยะไกล

5. **เว็บเซิร์ฟเวอร์ในตัวบอร์ด Gateway (`dataset_page.h` บน Port 80)**:
   - บอร์ด Gateway เปิด HTTP Server ที่ `http://<Gateway-IP>/`
   - มี API `/api/nodes` (ดูสถานะ Node และแบตเตอรี่), `/api/node-config` (เปลี่ยนชื่อและตำแหน่งติดตั้ง `mount_site`/`body_part`/`side` บันทึกลงหน่วยความจำ NVS ของ ESP32), และ `/samples` (ดึงข้อมูลตัวอย่าง IMU ล่าสุดผ่าน HTTP)

6. **ระบบติดตามจังหวะแพลงก์ 5 ขั้นตอน (`tools/plank_phase.py` & `prepare_plank_5stage_dataset.py`)**:
   - วิเคราะห์การเปลี่ยนแปลงของท่าทางตามเวลา แบ่งเป็น 5 ช่วง: `1. normal_start` -> `2. approaching_error` -> `3. incorrect_posture_hold` -> `4. recovery_to_normal` -> `5. normal_end`

7. **เซิร์ฟเวอร์ทำนายผล AI เรียลไทม์ (`tools/serve_model.py` บน Port 8765)**:
   - โหลดไฟล์ `model_bundle.joblib` และเปิด API `/api/health`, `/api/predict-window` ให้หน้าเว็บ `WEB/index.html` เรียกใช้งานเพื่อแสดงผลท่าทางที่ทำนายได้ (`action`), ช่วงจังหวะ (`stage`), และความมั่นใจ (`score %`) แบบสดๆ

---

## 6. ซอร์สโค้ดทั้งหมดของโปรเจกต์ครบทุกไฟล์ (Complete Project Source Code — 42 Files)

### 6.1 ไฟล์ `README.md` — เอกสารภาพรวมโปรเจกต์หลัก (Root README) (158 บรรทัด)
````markdown
# PlanKO Gateway and IMU Nodes

PlatformIO project for an ESP32-C3 Gateway and up to four ESP32-C3 IMU Nodes.
The Gateway receives sensor samples over BLE, keeps the MQTT stream for the
existing app, and serves a local dataset recorder.

## Firmware roles

Open `Code/` with PlatformIO and select the matching environment:

- `gateway`: BLE Central, Wi-Fi Station, MQTT publisher, pad inputs, and the
  dataset recorder at `http://192.168.1.4/`. It publishes dashboard telemetry
  through Mosquitto and receives targeted pad commands over MQTT.
- `imu_node_1` through `imu_node_4`: BLE Peripheral with a real MPU-6050 sensor.
  Connect SDA to GPIO 8 and SCL to GPIO 9. A missing sensor is reported as
  unavailable; the Node does not generate mock measurements.

The Gateway can connect to four Nodes at once. The recorder shows each Node's
connection and sensor identity. Configure its display name and body location in
the page; the defaults are Node 1 at the neck and Node 2 at the back/lumbar.

Open the root `index.html` dashboard in a browser on the broker PC. It connects
to the local WebSocket listener at `ws://127.0.0.1:9001/mqtt`, subscribes to
`planko/imu` and `planko/pad`, and filters messages by Gateway ID. Pad buttons
publish targeted commands on the Gateway's `commandTopic`; the resulting pad
event is published back on `planko/pad`. These are simulated pad events and do
not electrically activate a GPIO input.

## Dataset capture

### Node battery telemetry (ESP32-C3 Super Mini, Nodes 1–4)

All four Node environments use GPIO4 (`BATTERY_ADC_PIN`, overrideable by a
build flag) for a 1S Li-ion/LiPo battery voltage divider:

```text
TP4056 OUT+ ---- 10k ohm ----+---- GPIO4
                           |
                        10k ohm
                           |
TP4056 OUT- ----------------+---- Node GND
```

An optional 100 nF ceramic capacitor goes between GPIO4 and OUT-/GND.
The existing OUT+ to Node 5V power wire is a separate branch; never connect
OUT+ directly to GPIO4. This measurement circuit does not regulate the Node's
power supply. Battery operation through the board's 5V input depends on its
onboard regulator and may stop before the cell is empty.

Nodes average 16 calibrated ADC millivolt readings, multiply by two, and send
a separate 5-byte battery BLE notification every 5 seconds and on subscription.
Battery reporting does not require a detected IMU. The percentage is a coarse
linear estimate (3.3V = 0%, 4.2V = 100%), not measured remaining capacity;
charging and load affect it. Readings outside 2.5–4.5V are marked unknown.

Flash the updated Gateway as well as each Node. The Gateway shows the battery
on its local Node cards and Serial Monitor, and includes `battery_valid`,
`battery_percent`, and `battery_mv` in `/api/nodes` and MQTT `planko/imu`.
Values become null when disconnected, invalid, or older than 15 seconds.
Existing IMU packet formats and sample sequence counters are unchanged.
The root `index.html` redirects to `remote.html`; that dashboard shows a
battery label only for connected Nodes with valid battery data. It hides the
label on Node disconnect, missing/expired data, Gateway change, or stream timeout.

1. Connect the Gateway to the Wi-Fi network that uses the `192.168.1.x`
   subnet, then open `http://192.168.1.4/` on a device on that network.
2. Connect one or more Nodes and set the subject, session, trial, action, and
   each Node's body location.
3. Start capture. The page records real raw accelerometer and gyroscope samples
   from all connected Nodes into separate in-browser datasets. The guided phase
   sequence can be edited; manual phase buttons are available when guided mode
   is off.
4. Stop capture, review the phase boundaries and review checkboxes, then confirm
   to create one CSV download link per Node.

Capture data remains in the browser page's memory until the CSV files are
downloaded. The Gateway holds 256 samples per Node (about 12.8 seconds at 20 Hz)
in a rolling buffer for the page.

## MQTT

The data path is **Node N -> BLE -> Gateway -> local Mosquitto -> Subscriber**.
Only the Gateway connects over MQTT; Nodes retain their BLE peripheral role.
Start the local Docker broker with `Code/tools/start_mosquitto.ps1`. It listens
on the PC's LAN IPv4 address at port 1883 for the Gateway and port 9001 with
WebSockets for the dashboard. The PC address is DHCP assigned: update
`MQTT_BROKER` in `Code/src/main.cpp` to the Wi-Fi address printed by the script,
then rebuild and upload the Gateway if that address changes. When opened from
`file://` on the broker PC, the root dashboard defaults to
`ws://127.0.0.1:9001/mqtt`; a dashboard on another device can use
`?broker=<PC-LAN-IP>`. Unique client IDs let the Gateway and dashboard connect
at the same time.

The consolidated `planko/imu` stream targets 20 Hz (50 ms), with every packet
containing all connected nodes. Schema `planko-imu-v2` retains the existing
angles/accel and adds gyro in rad/s, explicit SI units, sensor identity, node
boot epoch, mount site/body part/side, validity and gateway MQTT sequence.
Accelerometer and gyro channels come from one paired BLE sample in the
Gateway history; no sensor values are fabricated. Pressure and biometric
topics remain at 4 Hz; pad and status topics remain event based.

### MQTT subscriber and model

```powershell
cd Code
python -m pip install -r requirements-ml.txt
python tools/serve_model.py --port 8765
```

Open **http://127.0.0.1:8765/monitor** (or `Code/WEB/index.html`) for the MQTT
subscriber. The top-level hardware remote also uses the Mosquitto WSS endpoint
described above.
Choose the exact Node ID and matching mount site/body part/side. Filter Gateway
ID to isolate your device on the public topic. Configure the actual mounting
context through the existing Gateway recorder at `http://192.168.1.4/`;
changing only the subscriber selection does not reconfigure a Node.

The monitor feeds **received MQTT samples** to `/api/predict-window` on the
local inference server; it no longer fetches sensor data from `/api/live`.
This local POST is computation after subscription, not an alternate sensor
transport. Recording and inference share the same six-axis SI sample adapter;
Python training and inference both call `imu_features.window_features`.
Missing gyro, invalid samples, gaps, reboots and context changes do not become
zero-filled training samples or windows that cross discontinuities. Display
pause keeps MQTT reception and sample accounting running.

The current default model contains **40 features**, a 1-second window at 20 Hz,
comprising 30 per-axis statistics, 6 magnitude statistics, 3 site/body/side
context flags and 1 relative-orientation validity flag,
and only the `wrist/wrist/unknown` context with `wrist_down`, `wrist_neutral`,
`wrist_up`. It has no trained phase classifier, so phase may remain `unknown`.
Neck/back require a separately trained compatible model; software alignment
does not make this wrist model support those sites. The model is unchanged,
and its evaluation does not establish independent held-out accuracy.

Tests (from project root):

```powershell
node --preserve-symlinks --preserve-symlinks-main Code/test/test_imu_protocol.cjs
python -B Code/test/test_model_contract.py -v
```

Existing HTTP recorder and legacy model testbench remain available separately.
See `Code/SUBSCRIBER_PROMPT.md` for the integration contract.

## Hardware and setup

- ESP32-C3 DevKitM-1 / SuperMini for the Gateway and each Node
- MPU-6050 on each Node (I2C address `0x68`, SDA GPIO 8, SCL GPIO 9)
- VS Code with PlatformIO
- Wi-Fi access point on the `192.168.1.x` subnet; reserve `192.168.1.4` for the
  Gateway to avoid an address conflict

Wi-Fi credentials and MQTT broker settings are configured in
`Code/src/main.cpp`. Build and upload each board using its PlatformIO role.
Gateway upload/monitor is **COM5**; Node 1 upload/monitor is **COM8**. Environments
`gateway`, `imu_node_1` through `imu_node_4` and their role flags are retained.
````

### 6.2 ไฟล์ `index.html` — หน้า Redirect หลักของโปรเจกต์ (12 บรรทัด)
```html
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta http-equiv="refresh" content="0; url=remote.html" />
  <title>PlanKO Hardware Remote</title>
</head>
<body>
  <p>Redirecting to <a href="remote.html">PlanKO Hardware Remote Dashboard</a>...</p>
</body>
</html>
```

### 6.3 ไฟล์ `remote.html` — หน้าเว็บ Remote Control & Smart Mat Pad Simulator (1523 บรรทัด)
```html
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>PlanKO Hardware Remote & IMU Telemetry</title>
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;500;600;700;800&family=JetBrains+Mono:wght@400;500;600&display=swap" rel="stylesheet">
  <script src="https://cdnjs.cloudflare.com/ajax/libs/paho-mqtt/1.0.1/mqttws31.min.js"></script>
  <style>
    :root {
      --bg-dark: #080c14;
      --bg-card: rgba(16, 23, 38, 0.75);
      --bg-card-hover: rgba(22, 32, 54, 0.85);
      --border-color: rgba(255, 255, 255, 0.08);
      --border-glow: rgba(56, 189, 248, 0.25);
      
      --accent-cyan: #06b6d4;
      --accent-blue: #3b82f6;
      --accent-purple: #8b5cf6;
      --accent-emerald: #10b981;
      --accent-amber: #f59e0b;
      --accent-rose: #f43f5e;
      
      --text-main: #f8fafc;
      --text-muted: #94a3b8;
      --text-dim: #64748b;
      
      --glow-cyan: 0 0 25px rgba(6, 182, 212, 0.35);
      --glow-purple: 0 0 25px rgba(139, 92, 246, 0.35);
      --glow-emerald: 0 0 20px rgba(16, 185, 129, 0.35);
      --glow-pad: 0 0 20px rgba(244, 63, 94, 0.6);
    }

    * {
      box-sizing: border-box;
      margin: 0;
      padding: 0;
      user-select: none;
      -webkit-user-select: none;
    }

    body {
      font-family: 'Outfit', -apple-system, BlinkMacSystemFont, sans-serif;
      background-color: var(--bg-dark);
      background-image: 
        radial-gradient(circle at 15% 15%, rgba(59, 130, 246, 0.12) 0%, transparent 40%),
        radial-gradient(circle at 85% 85%, rgba(139, 92, 246, 0.12) 0%, transparent 45%),
        linear-gradient(180deg, rgba(8, 12, 20, 0.85) 0%, #080c14 100%);
      color: var(--text-main);
      min-height: 100vh;
      overflow-x: hidden;
      padding-bottom: 40px;
    }

    /* Top Navigation Bar */
    header {
      backdrop-filter: blur(16px);
      -webkit-backdrop-filter: blur(16px);
      background: rgba(8, 12, 20, 0.8);
      border-bottom: 1px solid var(--border-color);
      position: sticky;
      top: 0;
      z-index: 100;
      padding: 14px 24px;
      display: flex;
      flex-wrap: wrap;
      align-items: center;
      justify-content: space-between;
      gap: 16px;
    }

    .brand {
      display: flex;
      align-items: center;
      gap: 12px;
    }

    .brand-logo {
      width: 38px;
      height: 38px;
      border-radius: 10px;
      background: linear-gradient(135deg, var(--accent-cyan), var(--accent-purple));
      display: flex;
      align-items: center;
      justify-content: center;
      font-weight: 800;
      font-size: 20px;
      color: white;
      box-shadow: var(--glow-cyan);
    }

    .brand-title h1 {
      font-size: 1.25rem;
      font-weight: 700;
      letter-spacing: -0.02em;
      background: linear-gradient(to right, #ffffff, #94a3b8);
      -webkit-background-clip: text;
      -webkit-text-fill-color: transparent;
    }

    .brand-title p {
      font-size: 0.75rem;
      color: var(--text-muted);
      font-family: 'JetBrains Mono', monospace;
    }

    /* Connection Bar */
    .connection-panel {
      display: flex;
      align-items: center;
      flex-wrap: wrap;
      gap: 10px;
      background: rgba(15, 23, 42, 0.6);
      border: 1px solid var(--border-color);
      padding: 6px 12px;
      border-radius: 30px;
    }

    .ws-input-wrap {
      display: flex;
      align-items: center;
      gap: 8px;
    }

    .ws-input-wrap input {
      background: rgba(0, 0, 0, 0.4);
      border: 1px solid rgba(255, 255, 255, 0.1);
      color: #38bdf8;
      font-family: 'JetBrains Mono', monospace;
      font-size: 0.85rem;
      padding: 6px 12px;
      border-radius: 20px;
      outline: none;
      width: 210px;
      transition: all 0.2s ease;
    }

    .ws-input-wrap input:focus {
      border-color: var(--accent-cyan);
      box-shadow: 0 0 10px rgba(6, 182, 212, 0.3);
    }

    .btn {
      font-family: 'Outfit', sans-serif;
      font-weight: 600;
      font-size: 0.85rem;
      padding: 6px 16px;
      border-radius: 20px;
      border: none;
      cursor: pointer;
      display: inline-flex;
      align-items: center;
      gap: 6px;
      transition: all 0.2s ease;
    }

    .btn-primary {
      background: linear-gradient(135deg, var(--accent-cyan), var(--accent-blue));
      color: white;
      box-shadow: 0 2px 10px rgba(6, 182, 212, 0.3);
    }

    .btn-primary:hover {
      transform: translateY(-1px);
      box-shadow: 0 4px 15px rgba(6, 182, 212, 0.4);
    }

    .btn-danger {
      background: rgba(244, 63, 94, 0.2);
      border: 1px solid rgba(244, 63, 94, 0.4);
      color: #fda4af;
    }

    .btn-danger:hover {
      background: rgba(244, 63, 94, 0.3);
    }

    .btn-secondary {
      background: rgba(255, 255, 255, 0.08);
      color: var(--text-main);
      border: 1px solid var(--border-color);
    }

    .btn-secondary:hover {
      background: rgba(255, 255, 255, 0.15);
    }

    .status-badge {
      display: inline-flex;
      align-items: center;
      gap: 6px;
      padding: 5px 12px;
      border-radius: 20px;
      font-size: 0.8rem;
      font-weight: 600;
      background: rgba(0, 0, 0, 0.4);
      border: 1px solid var(--border-color);
    }

    .dot {
      width: 8px;
      height: 8px;
      border-radius: 50%;
    }

    .status-disconnected .dot {
      background: var(--accent-rose);
      box-shadow: 0 0 8px var(--accent-rose);
    }
    .status-connected .dot {
      background: var(--accent-emerald);
      box-shadow: 0 0 10px var(--accent-emerald);
      animation: pulse 1.8s infinite;
    }
    .status-connecting .dot {
      background: var(--accent-amber);
      box-shadow: 0 0 8px var(--accent-amber);
      animation: blink 1s infinite;
    }

    @keyframes pulse {
      0%, 100% { opacity: 1; transform: scale(1); }
      50% { opacity: 0.6; transform: scale(1.2); }
    }
    @keyframes blink {
      0%, 100% { opacity: 1; }
      50% { opacity: 0.3; }
    }

    /* Container Layout */
    .container {
      max-width: 1400px;
      margin: 24px auto;
      padding: 0 20px;
      display: flex;
      flex-direction: column;
      gap: 24px;
    }

    /* Quick Info Bar */
    .telemetry-stats-bar {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
      gap: 14px;
    }

    .stat-card {
      background: var(--bg-card);
      border: 1px solid var(--border-color);
      border-radius: 16px;
      padding: 14px 18px;
      backdrop-filter: blur(12px);
      display: flex;
      align-items: center;
      justify-content: space-between;
      transition: border-color 0.2s;
    }

    .stat-card:hover {
      border-color: var(--border-glow);
    }

    .stat-info p {
      font-size: 0.75rem;
      color: var(--text-muted);
      text-transform: uppercase;
      letter-spacing: 0.05em;
      margin-bottom: 4px;
    }

    .stat-info h3 {
      font-size: 1.3rem;
      font-weight: 700;
      font-family: 'JetBrains Mono', monospace;
      color: #fff;
    }

    .stat-icon {
      width: 40px;
      height: 40px;
      border-radius: 12px;
      background: rgba(255, 255, 255, 0.04);
      border: 1px solid var(--border-color);
      display: flex;
      align-items: center;
      justify-content: center;
      font-size: 1.2rem;
    }

    /* Main Section: Interactive Mat & 4 IMU Cards */
    .grid-main {
      display: grid;
      grid-template-columns: 1fr 1.35fr;
      gap: 24px;
    }

    @media (max-width: 1100px) {
      .grid-main {
        grid-template-columns: 1fr;
      }
    }

    /* Card Shell */
    .panel {
      background: var(--bg-card);
      border: 1px solid var(--border-color);
      border-radius: 20px;
      padding: 22px;
      backdrop-filter: blur(16px);
      box-shadow: 0 8px 32px rgba(0, 0, 0, 0.35);
      position: relative;
    }

    .panel-header {
      display: flex;
      align-items: center;
      justify-content: space-between;
      margin-bottom: 20px;
      padding-bottom: 12px;
      border-bottom: 1px solid rgba(255, 255, 255, 0.06);
    }

    .panel-title {
      font-size: 1.1rem;
      font-weight: 700;
      display: flex;
      align-items: center;
      gap: 10px;
    }

    .panel-title span {
      background: linear-gradient(135deg, var(--accent-cyan), var(--accent-blue));
      -webkit-background-clip: text;
      -webkit-text-fill-color: transparent;
    }

    /* Interactive Plank Mat Remote */
    .mat-wrapper {
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 16px;
    }

    .mat-container {
      width: 100%;
      background: radial-gradient(circle at 50% 50%, rgba(30, 41, 59, 0.6) 0%, rgba(15, 23, 42, 0.8) 100%);
      border: 2px solid rgba(255, 255, 255, 0.1);
      border-radius: 24px;
      padding: 24px 16px;
      display: grid;
      grid-template-columns: 1fr 1.1fr 1fr;
      gap: 16px;
      box-shadow: inset 0 0 30px rgba(0, 0, 0, 0.6);
      position: relative;
    }

    .mat-col {
      display: flex;
      flex-direction: column;
      gap: 10px;
    }

    .pad-btn {
      background: linear-gradient(180deg, rgba(30, 41, 59, 0.8) 0%, rgba(15, 23, 42, 0.95) 100%);
      border: 1px solid rgba(255, 255, 255, 0.12);
      border-radius: 12px;
      padding: 12px 10px;
      color: #e2e8f0;
      font-family: 'Outfit', sans-serif;
      font-weight: 700;
      font-size: 1rem;
      display: flex;
      align-items: center;
      justify-content: space-between;
      cursor: pointer;
      transition: all 0.15s cubic-bezier(0.4, 0, 0.2, 1);
      box-shadow: 0 4px 6px rgba(0, 0, 0, 0.3);
      position: relative;
      overflow: hidden;
    }

    .pad-btn::before {
      content: '';
      position: absolute;
      top: 0;
      left: 0;
      right: 0;
      height: 2px;
      background: linear-gradient(90deg, transparent, rgba(255, 255, 255, 0.2), transparent);
    }

    .pad-btn .shortcut {
      font-size: 0.7rem;
      font-family: 'JetBrains Mono', monospace;
      color: var(--text-dim);
      background: rgba(0, 0, 0, 0.3);
      padding: 2px 6px;
      border-radius: 6px;
      border: 1px solid rgba(255, 255, 255, 0.05);
    }

    .pad-btn:hover {
      transform: translateY(-2px);
      border-color: rgba(56, 189, 248, 0.5);
      box-shadow: 0 6px 14px rgba(56, 189, 248, 0.2);
      color: #38bdf8;
    }

    .pad-btn:active, .pad-btn.pressed {
      transform: scale(0.96);
      background: linear-gradient(135deg, #f43f5e 0%, #ec4899 100%) !important;
      border-color: #fda4af !important;
      color: #ffffff !important;
      box-shadow: 0 0 25px rgba(244, 63, 94, 0.8) !important;
    }

    .pad-btn.pressed .shortcut {
      color: rgba(255, 255, 255, 0.9);
      background: rgba(0, 0, 0, 0.25);
    }

    /* Plank Silhouette Center Area */
    .mat-center {
      border: 1px dashed rgba(255, 255, 255, 0.12);
      border-radius: 16px;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      padding: 16px 8px;
      gap: 12px;
      background: rgba(0, 0, 0, 0.2);
    }

    .mat-center svg {
      width: 80px;
      height: 140px;
      opacity: 0.35;
      filter: drop-shadow(0 0 10px rgba(56, 189, 248, 0.3));
      transition: all 0.3s ease;
    }

    .mat-center.active svg {
      opacity: 0.85;
      filter: drop-shadow(0 0 15px rgba(16, 185, 129, 0.6));
    }

    .mat-center p {
      font-size: 0.75rem;
      text-transform: uppercase;
      letter-spacing: 0.08em;
      color: var(--text-dim);
      font-weight: 600;
    }

    /* Mat Controls */
    .mat-controls {
      display: flex;
      gap: 10px;
      width: 100%;
    }

    .mat-controls button {
      flex: 1;
      justify-content: center;
    }

    /* 4 IMU Nodes Grid */
    .imu-grid {
      display: grid;
      grid-template-columns: repeat(2, 1fr);
      gap: 16px;
    }

    @media (max-width: 650px) {
      .imu-grid {
        grid-template-columns: 1fr;
      }
    }

    .node-card {
      background: rgba(15, 23, 42, 0.65);
      border: 1px solid var(--border-color);
      border-radius: 16px;
      padding: 16px;
      display: flex;
      flex-direction: column;
      gap: 12px;
      transition: all 0.25s ease;
      position: relative;
    }

    .node-card.connected {
      border-color: rgba(56, 189, 248, 0.3);
    }

    .node-card.offline {
      opacity: 0.6;
    }

    .node-header {
      display: flex;
      align-items: center;
      justify-content: space-between;
    }

    .node-id-badge {
      display: flex;
      align-items: center;
      gap: 8px;
    }

    .node-badge-num {
      width: 26px;
      height: 26px;
      border-radius: 8px;
      background: linear-gradient(135deg, var(--accent-blue), var(--accent-purple));
      color: white;
      font-weight: 700;
      font-size: 0.85rem;
      display: flex;
      align-items: center;
      justify-content: center;
    }

    .node-title {
      font-weight: 700;
      font-size: 0.95rem;
    }

    .node-source-tag {
      font-size: 0.68rem;
      font-weight: 600;
      font-family: 'JetBrains Mono', monospace;
      padding: 2px 8px;
      border-radius: 12px;
      background: rgba(255, 255, 255, 0.06);
      border: 1px solid rgba(255, 255, 255, 0.08);
      color: var(--text-muted);
    }

    .node-source-tag.mock {
      background: rgba(245, 158, 11, 0.15);
      color: #fde68a;
      border-color: rgba(245, 158, 11, 0.3);
    }

    .node-source-tag.real {
      background: rgba(16, 185, 129, 0.15);
      color: #a7f3d0;
      border-color: rgba(16, 185, 129, 0.3);
    }

    /* 3D Orientation Visualization Box */
    .cube-scene {
      width: 100%;
      height: 80px;
      perspective: 400px;
      display: flex;
      align-items: center;
      justify-content: center;
      background: rgba(0, 0, 0, 0.25);
      border-radius: 12px;
      border: 1px solid rgba(255, 255, 255, 0.04);
      overflow: hidden;
      position: relative;
    }

    .cube-board {
      width: 80px;
      height: 48px;
      background: linear-gradient(135deg, #1e293b, #0f172a);
      border: 1.5px solid #38bdf8;
      border-radius: 8px;
      box-shadow: 0 0 15px rgba(56, 189, 248, 0.4);
      display: flex;
      align-items: center;
      justify-content: center;
      color: #38bdf8;
      font-weight: 800;
      font-size: 0.75rem;
      letter-spacing: 0.05em;
      transform-style: preserve-3d;
      transition: transform 0.05s ease-out;
    }

    .cube-board::after {
      content: '▲ FRONT';
      font-size: 0.55rem;
      color: #f43f5e;
      position: absolute;
      top: 3px;
    }

    /* Angles & Gauges */
    .angles-grid {
      display: grid;
      grid-template-columns: repeat(3, 1fr);
      gap: 6px;
      background: rgba(0, 0, 0, 0.25);
      padding: 8px;
      border-radius: 10px;
      border: 1px solid rgba(255, 255, 255, 0.04);
    }

    .angle-item {
      text-align: center;
    }

    .angle-label {
      font-size: 0.68rem;
      color: var(--text-dim);
      font-weight: 600;
      text-transform: uppercase;
    }

    .angle-val {
      font-family: 'JetBrains Mono', monospace;
      font-size: 0.95rem;
      font-weight: 700;
      color: #f1f5f9;
    }

    /* Accelerometer bar */
    .accel-row {
      display: flex;
      justify-content: space-between;
      font-size: 0.72rem;
      color: var(--text-muted);
      font-family: 'JetBrains Mono', monospace;
      padding: 0 4px;
    }

    /* Sequence & stats footer */
    .node-footer {
      display: flex;
      align-items: center;
      justify-content: space-between;
      font-size: 0.72rem;
      color: var(--text-dim);
      border-top: 1px solid rgba(255, 255, 255, 0.05);
      padding-top: 8px;
    }

    /* Bottom: Console & Automation Toolbar */
    .console-panel {
      margin-top: 8px;
    }

    .console-header-controls {
      display: flex;
      align-items: center;
      gap: 8px;
    }

    .console-body {
      background: #050811;
      border: 1px solid rgba(255, 255, 255, 0.06);
      border-radius: 12px;
      height: 180px;
      overflow-y: auto;
      padding: 12px;
      font-family: 'JetBrains Mono', monospace;
      font-size: 0.78rem;
      display: flex;
      flex-direction: column;
      gap: 4px;
    }

    .log-line {
      display: flex;
      gap: 10px;
      line-height: 1.4;
      word-break: break-all;
    }

    .log-time {
      color: var(--text-dim);
      flex-shrink: 0;
    }

    .log-tag {
      padding: 1px 6px;
      border-radius: 4px;
      font-size: 0.68rem;
      font-weight: 600;
      flex-shrink: 0;
    }

    .tag-pad { background: rgba(244, 63, 94, 0.2); color: #fda4af; }
    .tag-imu { background: rgba(6, 182, 212, 0.2); color: #67e8f9; }
    .tag-sys { background: rgba(139, 92, 246, 0.2); color: #c4b5fd; }
    .tag-err { background: rgba(239, 68, 68, 0.25); color: #fca5a5; }

    .log-msg {
      color: #e2e8f0;
    }

    /* Toast Notification */
    .toast {
      position: fixed;
      bottom: 24px;
      right: 24px;
      background: rgba(15, 23, 42, 0.95);
      border: 1px solid var(--accent-cyan);
      box-shadow: var(--glow-cyan);
      border-radius: 12px;
      padding: 12px 18px;
      color: white;
      font-weight: 600;
      font-size: 0.9rem;
      z-index: 1000;
      display: flex;
      align-items: center;
      gap: 10px;
      transform: translateY(100px);
      opacity: 0;
      transition: all 0.3s cubic-bezier(0.16, 1, 0.3, 1);
    }

    .toast.show {
      transform: translateY(0);
      opacity: 1;
    }
  </style>
</head>
<body>

  <!-- Header -->
  <header>
    <div class="brand">
      <div class="brand-logo">P</div>
      <div class="brand-title">
        <h1>PlanKO Hardware Remote</h1>
        <p>ESP32 Gateway & IMU Sensor Control Panel</p>
      </div>
    </div>

    <!-- Connection Bar -->
    <div class="connection-panel">
      <div class="ws-input-wrap">
        <label for="mqttUrl" style="font-size:0.75rem; color:var(--text-muted);">MQTT Broker (WSS):</label>
        <input type="text" id="mqttUrl" value="wss://test.mosquitto.org:8081/mqtt" placeholder="wss://test.mosquitto.org:8081/mqtt" />
      </div>
      <div class="ws-input-wrap">
        <label for="gatewayFilter" style="font-size:0.75rem; color:var(--text-muted);">Gateway ID:</label>
        <input type="text" id="gatewayFilter" placeholder="Auto-select first Gateway" />
      </div>

      <button id="btnConnect" class="btn btn-primary" onclick="toggleConnect()">
        <span id="btnConnectText">Connect</span>
      </button>

      <div id="statusBadge" class="status-badge status-disconnected">
        <div class="dot"></div>
        <span id="statusText">DISCONNECTED</span>
      </div>
    </div>
  </header>

  <!-- Main Container -->
  <div class="container">

    <!-- Quick Stats HUD -->
    <div class="telemetry-stats-bar">
      <div class="stat-card">
        <div class="stat-info">
          <p>Gateway ID</p>
          <h3 id="statIp">---.---.---.---</h3>
        </div>
        <div class="stat-icon">🌐</div>
      </div>

      <div class="stat-card">
        <div class="stat-info">
          <p>Connected Nodes</p>
          <h3 id="statNodesCount">0 / 4</h3>
        </div>
        <div class="stat-icon">📡</div>
      </div>

      <div class="stat-card">
        <div class="stat-info">
          <p>Stream Age</p>
          <h3 id="statLatency">-- ms</h3>
        </div>
        <div class="stat-icon">⚡</div>
      </div>

      <div class="stat-card">
        <div class="stat-info">
          <p>Total Pad Events</p>
          <h3 id="statPadEvents">0</h3>
        </div>
        <div class="stat-icon">🎮</div>
      </div>

      <div class="stat-card">
        <div class="stat-info">
          <p>Uptime</p>
          <h3 id="statUptime">00:00</h3>
        </div>
        <div class="stat-icon">⏱️</div>
      </div>
    </div>

    <!-- Main Grid: Interactive Mat & 4 IMUs -->
    <div class="grid-main">

      <!-- LEFT: Interactive Touch Pad Remote -->
      <div class="panel">
        <div class="panel-header">
          <div class="panel-title">
            <span>🎮 Plank Touch Pad Remote</span>
          </div>
          <span style="font-size:0.75rem; color:var(--text-dim);">Click or use keyboard</span>
        </div>

        <div class="mat-wrapper">
          <div class="mat-container">
            <!-- Left Pads (L1 - L8) -->
            <div class="mat-col" id="colLeftPads">
              <!-- Dynamically generated or static L1-L8 -->
            </div>

            <!-- Center Silhouette -->
            <div class="mat-center" id="matCenter">
              <!-- Human Plank Silhouette SVG -->
              <svg viewBox="0 0 100 200" fill="none" xmlns="http://www.w3.org/2000/svg">
                <!-- Head -->
                <circle cx="50" cy="35" r="14" fill="#38bdf8" />
                <!-- Torso / Spine -->
                <path d="M42 55 H58 L54 135 H46 Z" fill="#38bdf8" />
                <!-- Arms / Elbows Planking -->
                <path d="M35 65 L22 95 L22 115" stroke="#38bdf8" stroke-width="6" stroke-linecap="round" stroke-linejoin="round" />
                <path d="M65 65 L78 95 L78 115" stroke="#38bdf8" stroke-width="6" stroke-linecap="round" stroke-linejoin="round" />
                <!-- Legs Planking -->
                <path d="M45 135 L40 185" stroke="#38bdf8" stroke-width="6" stroke-linecap="round" />
                <path d="M55 135 L60 185" stroke="#38bdf8" stroke-width="6" stroke-linecap="round" />
              </svg>
              <p id="centerMatStatus">PLANK MAT READY</p>
            </div>

            <!-- Right Pads (R1 - R8) -->
            <div class="mat-col" id="colRightPads">
              <!-- Dynamically generated R1-R8 -->
            </div>
          </div>

          <!-- Mat Automation Controls -->
          <div class="mat-controls">
            <button class="btn btn-secondary" onclick="simulateRhythmDemo()">
              🎵 Test Routine Demo
            </button>
            <button class="btn btn-secondary" onclick="clearLogs()">
              🗑️ Clear Console
            </button>
          </div>
        </div>
      </div>

      <!-- RIGHT: 4 IMU Sensor Telemetry Nodes -->
      <div class="panel">
        <div class="panel-header">
          <div class="panel-title">
            <span>📊 4-Node 6-DOF IMU Telemetry</span>
          </div>
          <button class="btn btn-secondary" style="font-size:0.75rem; padding:4px 12px;" onclick="sendCalibrateCmd()">
            🎯 Zero Display Angles
          </button>
        </div>

        <div class="imu-grid" id="imuGrid">
          <!-- Node Cards 1 to 4 generated here -->
        </div>
      </div>

    </div>

    <!-- Bottom: Live Event Console Log -->
    <div class="panel console-panel">
      <div class="panel-header">
        <div class="panel-title">
          <span>📜 Live Hardware & Network Console</span>
        </div>
        <div class="console-header-controls">
          <label style="font-size:0.75rem; color:var(--text-muted); display:flex; align-items:center; gap:6px;">
            <input type="checkbox" id="chkAutoScroll" checked> Auto-Scroll
          </label>
          <button class="btn btn-secondary" style="font-size:0.75rem; padding:4px 10px;" onclick="clearLogs()">Clear</button>
        </div>
      </div>

      <div class="console-body" id="consoleBody">
        <div class="log-line">
          <span class="log-time">[INIT]</span>
          <span class="log-tag tag-sys">SYS</span>
          <span class="log-msg">PlanKO Remote Dashboard ready. Connecting to Mosquitto for Gateway telemetry.</span>
        </div>
      </div>
    </div>

  </div>

  <!-- Toast Notification -->
  <div id="toast" class="toast">
    <span id="toastIcon">⚡</span>
    <span id="toastMsg">Pad Triggered!</span>
  </div>

  <script>
    // Configuration & State
    let mqttClient = null;
    let isConnected = false;
    let padEventCount = 0;
    let mqttReconnectTimer = null;
    let mqttGeneration = 0;
    let selectedGatewayId = '';
    let selectedCommandTopic = '';
    let lastStreamAt = 0;
    const MQTT_IMU_TOPIC = 'planko/imu';
    const MQTT_PAD_TOPIC = 'planko/pad';
    const LEGACY_PUBLIC_MQTT_URL = 'wss://test.mosquitto.org:8081/mqtt';
    const requestedBrokerHost = new URLSearchParams(location.search).get('broker');
    const defaultBrokerHost = requestedBrokerHost ||
      (location.protocol === 'file:' ? '127.0.0.1' : location.hostname || '127.0.0.1');
    const DEFAULT_MQTT_URL = `ws://${defaultBrokerHost}:9001/mqtt`;
    let rhythmInterval = null;

    // Node Telemetry State (Nodes 1-4)
    const nodes = {
      1: { connected: false, isMock: false, pitch: 0, roll: 0, yaw: 0, ax: 0, ay: 0, az: 9.8, seq: 0, dropped: 0, lastSeen: 0 },
      2: { connected: false, isMock: false, pitch: 0, roll: 0, yaw: 0, ax: 0, ay: 0, az: 9.8, seq: 0, dropped: 0, lastSeen: 0 },
      3: { connected: false, isMock: false, pitch: 0, roll: 0, yaw: 0, ax: 0, ay: 0, az: 9.8, seq: 0, dropped: 0, lastSeen: 0 },
      4: { connected: false, isMock: false, pitch: 0, roll: 0, yaw: 0, ax: 0, ay: 0, az: 9.8, seq: 0, dropped: 0, lastSeen: 0 },
    };
    const displayZero = {
      1: { pitch: 0, roll: 0, yaw: 0 }, 2: { pitch: 0, roll: 0, yaw: 0 },
      3: { pitch: 0, roll: 0, yaw: 0 }, 4: { pitch: 0, roll: 0, yaw: 0 }
    };
    const latestNodeData = {};

    // Pad Definitions with Keyboard Shortcuts
    const LEFT_PADS = [
      { id: 'L1', key: '1' },
      { id: 'L2', key: '2' },
      { id: 'L3', key: '3' },
      { id: 'L4', key: '4' },
      { id: 'L5', key: '5' },
      { id: 'L6', key: '6' },
      { id: 'L7', key: '7' },
      { id: 'L8', key: '8' },
    ];

    const RIGHT_PADS = [
      { id: 'R1', key: 'Q' },
      { id: 'R2', key: 'W' },
      { id: 'R3', key: 'E' },
      { id: 'R4', key: 'R' },
      { id: 'R5', key: 'T' },
      { id: 'R6', key: 'Y' },
      { id: 'R7', key: 'U' },
      { id: 'R8', key: 'I' },
    ];

    // Initialize UI on Load
    window.addEventListener('DOMContentLoaded', () => {
      const savedMqttUrl = localStorage.getItem('planko_mqtt_url');
      document.getElementById('mqttUrl').value =
        savedMqttUrl && savedMqttUrl !== LEGACY_PUBLIC_MQTT_URL
          ? savedMqttUrl : DEFAULT_MQTT_URL;
      document.getElementById('gatewayFilter').value = localStorage.getItem('planko_gateway_filter') || '';
      selectedGatewayId = document.getElementById('gatewayFilter').value.trim();

      renderPads();
      renderImuCards();
      setupKeyboardControls();
      connectMqtt();
      document.getElementById('gatewayFilter').addEventListener('change', () => {
        selectedGatewayId = document.getElementById('gatewayFilter').value.trim();
        selectedCommandTopic = '';
        lastStreamAt = 0;
        localStorage.setItem('planko_gateway_filter', selectedGatewayId);
        resetNodeStates();
      });
    });

    // Render Left & Right Pad Columns
    function renderPads() {
      const leftCol = document.getElementById('colLeftPads');
      const rightCol = document.getElementById('colRightPads');

      leftCol.innerHTML = LEFT_PADS.map(p => `
        <button id="pad_${p.id}" class="pad-btn" onclick="triggerPad('${p.id}')">
          <span>${p.id}</span>
          <span class="shortcut">${p.key}</span>
        </button>
      `).join('');

      rightCol.innerHTML = RIGHT_PADS.map(p => `
        <button id="pad_${p.id}" class="pad-btn" onclick="triggerPad('${p.id}')">
          <span>${p.id}</span>
          <span class="shortcut">${p.key}</span>
        </button>
      `).join('');
    }

    // Render 4 IMU Node Cards
    function renderImuCards() {
      const grid = document.getElementById('imuGrid');
      grid.innerHTML = [1, 2, 3, 4].map(id => `
        <div id="node_card_${id}" class="node-card offline">
          <div class="node-header">
            <div class="node-id-badge">
              <div class="node-badge-num">${id}</div>
              <div>
                <div class="node-title">PlanKO IMU #${id}</div>
              </div>
            </div>
            <span id="node_tag_${id}" class="node-source-tag">OFFLINE</span>
          </div>
          <div id="node_battery_${id}" class="node-source-tag" hidden></div>

          <!-- 3D Orientation Board -->
          <div class="cube-scene">
            <div id="cube_${id}" class="cube-board">IMU #${id}</div>
          </div>

          <!-- Angles Display -->
          <div class="angles-grid">
            <div class="angle-item">
              <div class="angle-label">Pitch</div>
              <div id="val_pitch_${id}" class="angle-val">0.0°</div>
            </div>
            <div class="angle-item">
              <div class="angle-label">Roll</div>
              <div id="val_roll_${id}" class="angle-val">0.0°</div>
            </div>
            <div class="angle-item">
              <div class="angle-label">Yaw</div>
              <div id="val_yaw_${id}" class="angle-val">0.0°</div>
            </div>
          </div>

          <!-- Accelerometer -->
          <div class="accel-row">
            <span>Ax: <b id="val_ax_${id}" style="color:#e2e8f0;">0.00</b></span>
            <span>Ay: <b id="val_ay_${id}" style="color:#e2e8f0;">0.00</b></span>
            <span>Az: <b id="val_az_${id}" style="color:#e2e8f0;">9.81</b></span>
          </div>

          <!-- Footer stats -->
          <div class="node-footer">
            <span id="val_seq_${id}">Seq: 0</span>
            <span id="val_drops_${id}">Drops: 0</span>
          </div>
        </div>
      `).join('');
    }

    // Mosquitto MQTT over WebSocket Secure (WSS)
    function toggleConnect() {
      if (isConnected) disconnectMqtt();
      else connectMqtt();
    }

    function connectMqtt() {
      clearTimeout(mqttReconnectTimer);
      mqttReconnectTimer = null;
      const generation = ++mqttGeneration;
      const previous = mqttClient;
      mqttClient = null;
      if (previous) {
        previous.onConnectionLost = () => {};
        try { previous.disconnect(); } catch (error) {}
      }

      const retry = (message) => {
        if (generation !== mqttGeneration) return;
        isConnected = false;
        lastStreamAt = 0;
        selectedCommandTopic = '';
        updateStatus('disconnected', `${message} · retrying`);
        document.getElementById('btnConnectText').innerText = 'Reconnect';
        document.getElementById('btnConnect').className = 'btn btn-primary';
        logMessage('ERR', `${message}; retrying in 4 seconds`);
        resetNodeStates();
        clearTimeout(mqttReconnectTimer);
        mqttReconnectTimer = setTimeout(connectMqtt, 4000);
      };

      try {
        let rawUrl = document.getElementById('mqttUrl').value.trim() || DEFAULT_MQTT_URL;
        if (!/^[a-z]+:\/\//i.test(rawUrl)) rawUrl = `wss://${rawUrl}`;
        const url = new URL(rawUrl);
        if (url.protocol !== 'wss:' && url.protocol !== 'ws:') {
          throw new Error('Enter the broker WebSocket endpoint using ws:// or wss://');
        }
        localStorage.setItem('planko_mqtt_url', url.href);
        document.getElementById('mqttUrl').value = url.href;
        if (typeof Paho === 'undefined') throw new Error('Paho MQTT client library did not load');

        updateStatus('connecting', `CONNECTING TO ${url.hostname}...`);
        logMessage('SYS', `Connecting to Mosquitto via ${url.href}`);
        const clientId = `PlanKO_App_${Date.now().toString(16)}_${Math.random().toString(16).slice(2, 8)}`;
        const client = new Paho.MQTT.Client(
          url.hostname,
          Number(url.port || (url.protocol === 'wss:' ? 443 : 80)),
          url.pathname || '/mqtt',
          clientId
        );
        mqttClient = client;
        client.onMessageArrived = (message) => {
          if (generation === mqttGeneration) handleMqttMessage(message);
        };
        client.onConnectionLost = (response) => {
          retry(`Mosquitto connection lost${response.errorMessage ? `: ${response.errorMessage}` : ''}`);
        };
        client.connect({
          useSSL: url.protocol === 'wss:', timeout: 10, keepAliveInterval: 30,
          cleanSession: true, mqttVersion: 4,
          onSuccess: () => {
            if (generation !== mqttGeneration) return;
            let pendingSubscriptions = 2;
            const subscribed = () => {
              if (generation !== mqttGeneration) return;
              pendingSubscriptions--;
              if (pendingSubscriptions !== 0) return;
              isConnected = true;
              document.getElementById('btnConnectText').innerText = 'Disconnect';
              document.getElementById('btnConnect').className = 'btn btn-danger';
              updateStatus('connecting', 'MOSQUITTO CONNECTED · WAITING FOR GATEWAY');
              logMessage('SYS', `Subscribed to ${MQTT_IMU_TOPIC} and ${MQTT_PAD_TOPIC}`);
            };
            client.subscribe(MQTT_IMU_TOPIC, {
              qos: 0, onSuccess: subscribed,
              onFailure: (error) => retry(`Subscribe ${MQTT_IMU_TOPIC} failed${error.errorMessage ? `: ${error.errorMessage}` : ''}`)
            });
            client.subscribe(MQTT_PAD_TOPIC, {
              qos: 0, onSuccess: subscribed,
              onFailure: (error) => retry(`Subscribe ${MQTT_PAD_TOPIC} failed${error.errorMessage ? `: ${error.errorMessage}` : ''}`)
            });
          },
          onFailure: (error) => retry(`Mosquitto connect failed${error.errorMessage ? `: ${error.errorMessage}` : ''}`)
        });
      } catch (error) {
        retry(`MQTT setup failed: ${error.message}`);
      }
    }

    function disconnectMqtt() {
      clearTimeout(mqttReconnectTimer);
      mqttReconnectTimer = null;
      mqttGeneration++;
      isConnected = false;
      lastStreamAt = 0;
      selectedCommandTopic = '';
      if (mqttClient) {
        mqttClient.onConnectionLost = () => {};
        try { mqttClient.disconnect(); } catch (error) {}
        mqttClient = null;
      }
      updateStatus('disconnected', 'DISCONNECTED');
      document.getElementById('btnConnectText').innerText = 'Connect';
      document.getElementById('btnConnect').className = 'btn btn-primary';
      resetNodeStates();
    }

    function updateStatus(state, text) {
      const badge = document.getElementById('statusBadge');
      badge.className = `status-badge status-${state}`;
      document.getElementById('statusText').innerText = text;
    }

    // Subscribe to the Gateway's IMU and pad topics through Mosquitto.
    function handleMqttMessage(message) {
      if (message.retained) return;
      try {
        const data = JSON.parse(message.payloadString);
        if (message.destinationName === MQTT_IMU_TOPIC && data.type === 'imu_stream') {
          const gatewayId = String(data.gateway?.clientId || '');
          const requestedGatewayId = document.getElementById('gatewayFilter').value.trim();
          if (!gatewayId || (requestedGatewayId && gatewayId !== requestedGatewayId)) return;
          if (!selectedGatewayId) {
            selectedGatewayId = gatewayId;
            document.getElementById('gatewayFilter').value = gatewayId;
          }
          if (gatewayId !== selectedGatewayId) return;
          selectedCommandTopic = data.gateway?.commandTopic || `planko/gateway/${gatewayId}/cmd`;
          lastStreamAt = Date.now();
          handleImuStream(data);
          return;
        }
        if (message.destinationName === MQTT_PAD_TOPIC && data.sensorId && data.value !== undefined) {
          const gatewayId = String(data.gatewayId || '');
          if (selectedGatewayId && gatewayId !== selectedGatewayId) return;
          handlePadEvent(data.sensorId);
        }
      } catch (error) {
        console.warn('Ignored malformed MQTT message', error);
      }
    }

    setInterval(() => {
      if (!isConnected) return;
      if (!lastStreamAt || Date.now() - lastStreamAt > 2500) {
        updateStatus('connecting', 'MOSQUITTO CONNECTED · GATEWAY WAITING');
        if (lastStreamAt) {
          resetNodeStates();
          lastStreamAt = 0;
          selectedCommandTopic = '';
        }
        return;
      }
      updateStatus('connected', 'MOSQUITTO + GATEWAY LIVE');
      document.getElementById('statLatency').innerText = `${Date.now() - lastStreamAt} ms`;
    }, 500);

    // Process IMU Stream Broadcast
    function handleImuStream(data) {
      if (data.gateway) {
        document.getElementById('statNodesCount').innerText = `${data.gateway.connectedNodes || 0} / 4`;
        document.getElementById('statIp').innerText = data.gateway.clientId || '—';
        const totalSeconds = Math.floor((Number(data.gateway.uptimeMs) || 0) / 1000);
        const hours = String(Math.floor(totalSeconds / 3600)).padStart(2, '0');
        const minutes = String(Math.floor((totalSeconds % 3600) / 60)).padStart(2, '0');
        const seconds = String(totalSeconds % 60).padStart(2, '0');
        document.getElementById('statUptime').innerText = `${hours}:${minutes}:${seconds}`;
      }

      const activeNodes = new Set();
      if (Array.isArray(data.nodes)) {
        data.nodes.forEach(n => {
          const id = n.nodeId;
          if (id >= 1 && id <= 4 && n.connected === true) {
            activeNodes.add(id);
            updateNodeCard(id, n);
          }
        });
      }

      // Instantly mark any node that didn't report in this packet as OFFLINE
      for (let i = 1; i <= 4; i++) {
        if (!activeNodes.has(i)) {
          nodes[i].connected = false;
          updateNodeBattery(i, null);
          const card = document.getElementById(`node_card_${i}`);
          const tag = document.getElementById(`node_tag_${i}`);
          if (card && card.classList.contains('connected')) {
            card.className = 'node-card offline';
            if (tag) {
              tag.className = 'node-source-tag';
              tag.innerText = 'OFFLINE';
            }
          }
        }
      }
    }

    // Update single node card UI
    function updateNodeBattery(id, data) {
      const battery = document.getElementById(`node_battery_${id}`);
      if (!battery) return;
      const show = data?.connected === true && data.battery_valid === true &&
        Number.isInteger(data.battery_percent) && data.battery_percent >= 0 &&
        data.battery_percent <= 100 && Number.isFinite(data.battery_mv) &&
        data.battery_mv >= 2500 && data.battery_mv <= 4500;
      battery.hidden = !show;
      battery.textContent = show
        ? `แบต ≈ ${data.battery_percent}% · ${(data.battery_mv / 1000).toFixed(2)} V`
        : '';
    }

    function updateNodeCard(id, data) {
      const card = document.getElementById(`node_card_${id}`);
      const tag = document.getElementById(`node_tag_${id}`);
      const cube = document.getElementById(`cube_${id}`);

      card.className = 'node-card connected';
      updateNodeBattery(id, data);

      if (data.isMock) {
        tag.className = 'node-source-tag mock';
        tag.innerText = 'MOCK SIM';
      } else {
        tag.className = 'node-source-tag real';
        tag.innerText = 'REAL IMU';
      }

      // Angles
      latestNodeData[id] = data;
      nodes[id].connected = true;
      nodes[id].pitch = Number(data.pitch) || 0;
      nodes[id].roll = Number(data.roll) || 0;
      nodes[id].yaw = Number(data.yaw) || 0;
      const pitch = nodes[id].pitch - displayZero[id].pitch;
      const roll = nodes[id].roll - displayZero[id].roll;
      const yaw = nodes[id].yaw - displayZero[id].yaw;
      const p = pitch.toFixed(1);
      const r = roll.toFixed(1);
      const y = yaw.toFixed(1);

      document.getElementById(`val_pitch_${id}`).innerText = `${p}°`;
      document.getElementById(`val_roll_${id}`).innerText = `${r}°`;
      document.getElementById(`val_yaw_${id}`).innerText = `${y}°`;

      // Accelerometer
      if (Array.isArray(data.accel) && data.accel.length >= 3) {
        document.getElementById(`val_ax_${id}`).innerText = data.accel[0].toFixed(2);
        document.getElementById(`val_ay_${id}`).innerText = data.accel[1].toFixed(2);
        document.getElementById(`val_az_${id}`).innerText = data.accel[2].toFixed(2);
      }

      // Stats
      document.getElementById(`val_seq_${id}`).innerText = `Seq: ${data.seq || 0}`;
      document.getElementById(`val_drops_${id}`).innerText = `Drops: ${data.dropped || 0}`;

      // 3D Visualizer Tilt: CSS Transform rotateX/Y/Z
      // In aero terms: Pitch = rotateX, Roll = rotateY/Z
      cube.style.transform = `rotateX(${-pitch}deg) rotateZ(${roll}deg) rotateY(${yaw}deg)`;
    }

    function resetNodeStates() {
      for (let i = 1; i <= 4; i++) {
        nodes[i].connected = false;
        updateNodeBattery(i, null);
        const card = document.getElementById(`node_card_${i}`);
        const tag = document.getElementById(`node_tag_${i}`);
        const cube = document.getElementById(`cube_${i}`);
        if (card) card.className = 'node-card offline';
        if (tag) {
          tag.className = 'node-source-tag';
          tag.innerText = 'OFFLINE';
        }
        if (cube) cube.style.transform = 'rotateX(0deg) rotateY(0deg) rotateZ(0deg)';
      }
      document.getElementById('statNodesCount').innerText = '0 / 4';
    }

    // Trigger Pad Event
    function triggerPad(sensorId) {
      if (!isConnected || !mqttClient || !selectedGatewayId || !selectedCommandTopic) {
        animatePadButton(sensorId);
        logMessage('SYS', `⚠️ Local preview only: ${sensorId}; waiting for a Gateway MQTT stream`);
        showToast('Gateway is not connected', '⚠️');
        return;
      }
      try {
        const message = new Paho.MQTT.Message(JSON.stringify({
          cmd: 'pad', pad: sensorId, gatewayId: selectedGatewayId
        }));
        message.destinationName = selectedCommandTopic;
        message.qos = 0;
        mqttClient.send(message);
        logMessage('PAD', `Published command for ${sensorId} to ${selectedCommandTopic}`);
        showToast(`Pad ${sensorId} Sent`, '⚡');
      } catch (error) {
        logMessage('ERR', `Could not publish pad command: ${error.message}`);
        showToast('Pad command failed', '⚠️');
      }
    }

    // Handle Pad Event received from Gateway
    function handlePadEvent(sensorId) {
      animatePadButton(sensorId);
      padEventCount++;
      document.getElementById('statPadEvents').innerText = padEventCount;
      logMessage('PAD', `Received Broadcast: ${sensorId}`);
      showToast(`Mat Broadcast: ${sensorId}`, '✨');
    }

    function animatePadButton(sensorId) {
      const btn = document.getElementById(`pad_${sensorId}`);
      const center = document.getElementById('matCenter');

      if (btn) {
        btn.classList.add('pressed');
        setTimeout(() => btn.classList.remove('pressed'), 250);
      }

      if (center) {
        center.classList.add('active');
        document.getElementById('centerMatStatus').innerText = `ACTIVE: ${sensorId}`;
        setTimeout(() => {
          center.classList.remove('active');
          document.getElementById('centerMatStatus').innerText = 'PLANK MAT READY';
        }, 300);
      }
    }

    // Keyboard Shortcuts
    function setupKeyboardControls() {
      window.addEventListener('keydown', (e) => {
        // Ignore if user is typing in the input box
        if (e.target.tagName === 'INPUT') return;

        const key = e.key.toUpperCase();

        // Check Left pads
        const lPad = LEFT_PADS.find(p => p.key === key);
        if (lPad) {
          e.preventDefault();
          triggerPad(lPad.id);
          return;
        }

        // Check Right pads
        const rPad = RIGHT_PADS.find(p => p.key === key);
        if (rPad) {
          e.preventDefault();
          triggerPad(rPad.id);
          return;
        }

        // Spacebar triggers L1
        if (e.code === 'Space') {
          e.preventDefault();
          triggerPad('L1');
        }
      });
    }

    // Send Calibrate Command
    function sendCalibrateCmd() {
      let count = 0;
      for (let id = 1; id <= 4; id++) {
        if (!nodes[id].connected) continue;
        displayZero[id] = {
          pitch: nodes[id].pitch,
          roll: nodes[id].roll,
          yaw: nodes[id].yaw
        };
        count++;
        if (latestNodeData[id]) updateNodeCard(id, latestNodeData[id]);
      }
      if (!count) {
        showToast('No connected IMU nodes', '⚠️');
        return;
      }
      logMessage('SYS', `Zeroed displayed orientation for ${count} connected node(s)`);
      showToast(`Zeroed ${count} node display(s)`, '🎯');
    }

    // Test Rhythm Demo Sequence
    function simulateRhythmDemo() {
      if (rhythmInterval) {
        clearInterval(rhythmInterval);
        rhythmInterval = null;
        logMessage('SYS', '⏹️ Rhythm Demo Stopped.');
        showToast('Rhythm Demo Stopped', '⏹️');
        return;
      }

      logMessage('SYS', '▶️ Starting Rhythm Routine Test...');
      showToast('Running Rhythm Routine...', '🎵');

      const sequence = ['L1', 'R1', 'L2', 'R2', 'L3', 'R3', 'L1', 'R1'];
      let step = 0;

      rhythmInterval = setInterval(() => {
        const pad = sequence[step % sequence.length];
        triggerPad(pad);
        step++;

        if (step >= 16) {
          clearInterval(rhythmInterval);
          rhythmInterval = null;
          logMessage('SYS', '✅ Routine Demo Completed.');
        }
      }, 500);
    }

    function logMessage(tag, msg) {
      const body = document.getElementById('consoleBody');
      const line = document.createElement('div');
      line.className = 'log-line';

      const d = new Date();
      const timeStr = d.toTimeString().split(' ')[0] + '.' + String(d.getMilliseconds()).padStart(3, '0');

      let tagClass = 'tag-sys';
      if (tag === 'PAD') tagClass = 'tag-pad';
      if (tag === 'IMU') tagClass = 'tag-imu';
      if (tag === 'ERR') tagClass = 'tag-err';

      line.innerHTML = `
        <span class="log-time">[${timeStr}]</span>
        <span class="log-tag ${tagClass}">${tag}</span>
        <span class="log-msg">${escapeHtml(msg)}</span>
      `;

      body.appendChild(line);

      if (document.getElementById('chkAutoScroll').checked) {
        body.scrollTop = body.scrollHeight;
      }
    }

    function clearLogs() {
      document.getElementById('consoleBody').innerHTML = '';
      logMessage('SYS', 'Console cleared.');
    }

    function showToast(msg, icon = '⚡') {
      const toast = document.getElementById('toast');
      document.getElementById('toastMsg').innerText = msg;
      document.getElementById('toastIcon').innerText = icon;
      toast.classList.add('show');
      setTimeout(() => toast.classList.remove('show'), 2200);
    }

    function escapeHtml(text) {
      const div = document.createElement('div');
      div.innerText = text;
      return div.innerHTML;
    }
  </script>
</body>
</html>
```

### 6.4 ไฟล์ `PlanKO_ESP32_Server/PlanKO_ESP32_Server.ino` — โค้ดต้นแบบ Arduino Sketch (.ino) (370 บรรทัด)
```cpp
/**
 * ==============================================================================
 *  Project: PlanKO - ESP32 Smart Plank Mat & Biometric WebSocket Server
 *  Platform: Arduino IDE / PlatformIO (C++)
 *  Target: ESP32 (WROOM-32 / DevKit V1) & ESP32-C3
 * ==============================================================================
 *  ไลบรารีที่ต้องติดตั้งใน Arduino IDE (ผ่าน Library Manager):
 *  1. "WebSockets" โดย Markus Sattler
 *  2. "ArduinoJson" โดย Benoît Blanchon
 *  3. (ออปชัน) "SparkFun MAX3010x Pulse and Proximity Sensor Library"
 * ==============================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>

// ==============================================================================
// 1. การตั้งค่า Wi-Fi (กรุณากรอกชื่อและรหัสผ่าน Wi-Fi 2.4GHz ของท่าน)
// ==============================================================================
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

// กำหนดพอร์ต WebSocket Server (ค่าเริ่มต้นตามโจทย์: 81)
const uint16_t WS_PORT = 81;

// ==============================================================================
// 2. การตั้งค่าเซ็นเซอร์วัดชีพจร MAX30102 (ออปชันเสริม)
// ==============================================================================
#define ENABLE_MAX30102 false

#if ENABLE_MAX30102
#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"

MAX30105 particleSensor;
const byte RATE_SIZE = 4;
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;
float beatsPerMinute = 0;
int beatAvg = 0;
unsigned long lastHeartTelemetryTime = 0;
const unsigned long HEART_TELEMETRY_INTERVAL = 1000;
#endif

// ==============================================================================
// 3. กำหนดขา GPIO สำหรับแผ่นรองแพลงก์ (L1 - L8, R1 - R8)
// ==============================================================================
struct PadConfig {
  const char* sensorId;
  int8_t pin;
};

#if defined(CONFIG_IDF_TARGET_ESP32C3)
// Pinout สำหรับ ESP32-C3
const PadConfig PAD_CONFIGS[] = {
  {"L1", 0},  {"L2", 1},  {"L3", 2},  {"L4", 3},
  {"L5", 4},  {"L6", 5},  {"L7", 6},  {"L8", 7},
  {"R1", 8},  {"R2", 9},  {"R3", 10}, {"R4", 18},
  {"R5", 19}, {"R6", 20}, {"R7", 21}, {"R8", -1}
};
#else
// Pinout แนะนำสำหรับ ESP32 มาตรฐาน (WROOM-32 / DevKit V1 30-38 ขา)
const PadConfig PAD_CONFIGS[] = {
  // ฝั่งซ้าย (Left Pads: L1 - L8)
  {"L1", 13},
  {"L2", 14},
  {"L3", 27},
  {"L4", 26},
  {"L5", 25},
  {"L6", 33},
  {"L7", 32},
  {"L8", 4},

  // ฝั่งขวา (Right Pads: R1 - R8)
  {"R1", 16},
  {"R2", 17},
  {"R3", 18},
  {"R4", 19},
  {"R5", 23},
  {"R6", 5},
  {"R7", 15},
  {"R8", 2}
};
#endif

const size_t TOTAL_PADS = sizeof(PAD_CONFIGS) / sizeof(PAD_CONFIGS[0]);

struct PadSensorState {
  const char* sensorId;
  int8_t pin;
  int lastRawState;
  int debouncedState;
  unsigned long lastDebounceTime;
};

PadSensorState sensors[TOTAL_PADS];
const unsigned long DEBOUNCE_DELAY_MS = 40;

WebSocketsServer webSocket = WebSocketsServer(WS_PORT);

// ==============================================================================
// 4. ฟังก์ชันส่งข้อมูล JSON ไปยัง WebSocket Client
// ==============================================================================
void sendPadPressEvent(const char* sensorId) {
  // โครงสร้าง JSON ที่ระบบ PlanKO รองรับ:
  // {
  //   "sensorId": "L1",
  //   "value": 1
  // }
  char jsonBuffer[64];
  snprintf(jsonBuffer, sizeof(jsonBuffer), "{\"sensorId\":\"%s\",\"value\":1}", sensorId);

  webSocket.broadcastTXT(jsonBuffer);

  Serial.printf("⚡ [PAD EVENT] %s -> %s (Clients: %u)\n", 
                sensorId, jsonBuffer, webSocket.connectedClients());
}

// ==============================================================================
// 5. WebSocket Event Callback
// ==============================================================================
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.printf("❌ [WS][%u] Client Disconnected\n", num);
      break;

    case WStype_CONNECTED: {
      IPAddress ip = webSocket.remoteIP(num);
      Serial.printf("✅ [WS][%u] Client Connected from %d.%d.%d.%d | URL: %s\n", 
                    num, ip[0], ip[1], ip[2], ip[3], payload);
      webSocket.sendTXT(num, "{\"status\":\"connected\",\"device\":\"PlanKO_ESP32\"}");
      break;
    }

    case WStype_TEXT:
      Serial.printf("📩 [WS][%u] Received: %s\n", num, payload);
      if (strcmp((char*)payload, "ping") == 0) {
        webSocket.sendTXT(num, "{\"type\":\"pong\"}");
      }
      break;

    case WStype_BIN:
    case WStype_ERROR:
    default:
      break;
  }
}

// ==============================================================================
// 6. ฟังก์ชันเชื่อมต่อ Wi-Fi และแสดง IP Address
// ==============================================================================
void connectWiFi() {
  Serial.println("\n==================================================");
  Serial.printf("🌐 [WIFI] Connecting to SSID: %s\n", WIFI_SSID);
  Serial.println("==================================================");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n🎉 [WIFI] Connected Successfully!");
    Serial.print("📡 ESP32 IP Address : ");
    Serial.println(WiFi.localIP());
    Serial.printf("🔌 WebSocket URL    : ws://%s:%u/\n", WiFi.localIP().toString().c_str(), WS_PORT);
    Serial.println("👉 นำ WebSocket URL ข้างต้นไปกรอกในแอป PlanKO");
    Serial.println("==================================================\n");
  } else {
    Serial.println("\n❌ [WIFI] Connection Failed!");
    Serial.println("👉 กรุณาตรวจสอบชื่อ Wi-Fi (SSID) และรหัสผ่านในโค้ด (รองรับ 2.4GHz เท่านั้น)");
  }
}

// ==============================================================================
// 7. ฟังก์ชันตรวจสอบการกดปุ่ม (Digital Input + Debounce)
// ==============================================================================
void checkPadSensors() {
  unsigned long now = millis();

  for (size_t i = 0; i < TOTAL_PADS; i++) {
    if (sensors[i].pin < 0) continue;

    int reading = digitalRead(sensors[i].pin);

    if (reading != sensors[i].lastRawState) {
      sensors[i].lastDebounceTime = now;
      sensors[i].lastRawState = reading;
    }

    if ((now - sensors[i].lastDebounceTime) > DEBOUNCE_DELAY_MS) {
      if (reading != sensors[i].debouncedState) {
        sensors[i].debouncedState = reading;

        // INPUT_PULLUP: LOW (0) หมายถึงปุ่มถูกกดลง
        if (sensors[i].debouncedState == LOW) {
          sendPadPressEvent(sensors[i].sensorId);
        }
      }
    }
  }
}

// ==============================================================================
// 8. Serial Monitor Simulator
// ==============================================================================
void handleSerialSimulator() {
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    input.toUpperCase();

    if (input.length() == 0) return;

    if (input == "HELP" || input == "?") {
      Serial.println("\n--- [PlanKO Serial Simulator] ---");
      Serial.println("พิมพ์รหัสปุ่มเพื่อจำลองการกด: L1 - L8, R1 - R8");
      Serial.println("พิมพ์ 'IP' เพื่อแสดง WebSocket URL");
      Serial.println("--------------------------------\n");
      return;
    }

    if (input == "IP") {
      Serial.printf("🔌 WebSocket URL: ws://%s:%u/\n", WiFi.localIP().toString().c_str(), WS_PORT);
      return;
    }

    bool found = false;
    for (size_t i = 0; i < TOTAL_PADS; i++) {
      if (input == sensors[i].sensorId) {
        Serial.printf("🎮 [SIMULATOR] Triggered: %s\n", sensors[i].sensorId);
        sendPadPressEvent(sensors[i].sensorId);
        found = true;
        break;
      }
    }

    if (!found) {
      Serial.printf("⚠️ ไม่พบเซ็นเซอร์ '%s' (พิมพ์ HELP เพื่อดูคำสั่ง)\n", input.c_str());
    }
  }
}

// ==============================================================================
// 9. ฟังก์ชันอ่านค่าและส่งข้อมูลชีพจร MAX30102 (ออปชันเสริม)
// ==============================================================================
#if ENABLE_MAX30102
void setupMAX30102() {
  Serial.println("❤️ [MAX30102] Initializing Heart Rate Sensor...");
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("❌ [MAX30102] Sensor not found! Please check wiring.");
    return;
  }
  particleSensor.setup();
  particleSensor.setPulseAmplitudeRed(0x0A);
  particleSensor.setPulseAmplitudeGreen(0);
  Serial.println("✅ [MAX30102] Sensor Ready!");
}

void processMAX30102() {
  long irValue = particleSensor.getIR();
  bool fingerDetected = (irValue > 50000);

  if (fingerDetected) {
    if (checkForBeat(irValue)) {
      long delta = millis() - lastBeat;
      lastBeat = millis();

      beatsPerMinute = 60 / (delta / 1000.0);

      if (beatsPerMinute < 255 && beatsPerMinute > 20) {
        rates[rateSpot++] = (byte)beatsPerMinute;
        rateSpot %= RATE_SIZE;

        beatAvg = 0;
        for (byte x = 0; x < RATE_SIZE; x++) beatAvg += rates[x];
        beatAvg /= RATE_SIZE;
      }
    }
  } else {
    beatsPerMinute = 0;
    beatAvg = 0;
  }

  if (millis() - lastHeartTelemetryTime > HEART_TELEMETRY_INTERVAL) {
    lastHeartTelemetryTime = millis();

    if (webSocket.connectedClients() > 0) {
      float spo2Approx = fingerDetected ? (97.0 + (random(0, 30) / 10.0)) : 0.0;
      char heartJson[128];
      snprintf(heartJson, sizeof(heartJson),
               "{\"type\":\"heartRate\",\"fingerDetected\":%s,\"bpm\":%.1f,\"avgBpm\":%d,\"spo2Approx\":%.1f}",
               fingerDetected ? "true" : "false", beatsPerMinute, beatAvg, spo2Approx);
      webSocket.broadcastTXT(heartJson);
    }
  }
}
#endif

// ==============================================================================
// 10. SETUP & LOOP
// ==============================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n");
  Serial.println("==================================================");
  Serial.println("🚀 PlanKO ESP32 WebSocket Controller Initializing");
  Serial.println("==================================================");

  for (size_t i = 0; i < TOTAL_PADS; i++) {
    sensors[i].sensorId = PAD_CONFIGS[i].sensorId;
    sensors[i].pin = PAD_CONFIGS[i].pin;
    sensors[i].lastRawState = HIGH;
    sensors[i].debouncedState = HIGH;
    sensors[i].lastDebounceTime = 0;

    if (sensors[i].pin >= 0) {
      pinMode(sensors[i].pin, INPUT_PULLUP);
      int initialReading = digitalRead(sensors[i].pin);
      sensors[i].lastRawState = initialReading;
      sensors[i].debouncedState = initialReading;
      Serial.printf("  ✓ Pad [%s] mapped to GPIO %d\n", sensors[i].sensorId, sensors[i].pin);
    } else {
      Serial.printf("  - Pad [%s] unassigned (Serial simulation only)\n", sensors[i].sensorId);
    }
  }

  connectWiFi();

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.printf("🚀 WebSocket Server Started on port %u\n", WS_PORT);

  #if ENABLE_MAX30102
  setupMAX30102();
  #endif

  Serial.println("\n💡 TIP: พิมพ์ 'L1' หรือ 'R1' ใน Serial Monitor เพื่อทดสอบส่ง Event ได้ทันที!");
}

void loop() {
  webSocket.loop();
  checkPadSensors();
  handleSerialSimulator();

  #if ENABLE_MAX30102
  processMAX30102();
  #endif

  static unsigned long lastWifiCheck = 0;
  if (WiFi.status() != WL_CONNECTED && millis() - lastWifiCheck > 10000) {
    lastWifiCheck = millis();
    Serial.println("⚠️ [WIFI] Connection lost! Reconnecting...");
    WiFi.reconnect();
  }
}
```

### 6.5 ไฟล์ `DATASET_MQTT.md` — คู่มือระบบ MQTT Dataset & Broker (94 บรรทัด)
````markdown
# Multi-node IMU capture at 20 Hz

Firmware is `src/main.cpp`. Each IMU Node samples at 20 Hz and sends to the
Gateway over BLE. The Gateway publishes one consolidated `planko/imu` message
every 50 ms, containing all connected Nodes (up to four), to
`test.mosquitto.org:1883`. Adding Nodes does not create an MQTT connection or
publish loop per Node. The browser subscribes with MQTT over
`wss://test.mosquitto.org:8081/mqtt`.

## Record

1. Use the firmware environments in `platformio.ini`: `gateway` and a distinct
   `imu_node_1` through `imu_node_4` per sensor. Connect the Gateway to Wi-Fi.
   Gateway uses DHCP for its IP, route and DNS; there is no fixed local address.
   The optional HTTP recorder's current address is printed in Serial Monitor.
2. Open `WEB/index.html`, or start `python tools/serve_model.py --port 8765` and
   open `http://127.0.0.1:8765/monitor`. The project-root `index.html` still opens
   the separate hardware remote; use the monitor for this MQTT dataset recorder.
3. The monitor pins the first Gateway ID it receives. Set your exact Gateway ID
   in the filter if another Gateway is selected. Configure actual sensor
   mounting locations on the Gateway's HTTP recorder before capture.
4. Configure each connected Node's actual mounting site, body part and side.
   Select all Nodes to record, then choose one shared Case/Action Label. The
   exact same `action_label` is written to every selected Node sample, so the
   label describes one case rather than a body-part-specific action. Fill
   Subject, Session and Trial IDs.
5. Start recording all selected Nodes together. The capture makes one CSV per
   sensor plus a manifest; samples from other Nodes are ignored. Node cards
   report `LIVE` or briefly `HOLD`, accepted sample rate, count and gaps.
6. Stop recording. Download each per-node CSV and the capture manifest, or use
   Download All (the browser may ask to permit multiple downloads). Download
   before closing or refreshing the page; capture is held in browser memory.

Locations and sensor UIDs come from the firmware, not the display selectors.
Legacy firmware `body_part=back` at `mount_site=lumbar` is exported as training
`body_part=lumbar`; the original value is in `firmware_body_part`. A
sensor/context change produces a separate CSV. Reboots remain visible as epoch
changes inside a stream. The shared `case_*` labels in `pose_labels.json` work
across Node body parts and are accepted by the training and inference pipeline.

## Training contract

CSV includes raw six-axis SI channels (acceleration in m/s², gyro in rad/s),
orientation, node and gateway identities, independent device clocks, shared
host arrival time, MQTT sequence, sensor sequence, boot epochs, subject,
session, trial, action and phase metadata. Missing/invalid axes and mock
samples are excluded rather than filled with zeros. Duplicate and out-of-order
samples are excluded independently per sensor. Missing sequence numbers,
reboots, invalid-sample recovery and time gaps are marked with `gap_before`.
Training reads this marker and resets windows at discontinuities.

Guided phase timers produce **unreviewed cues**. Their phase labels are excluded
from phase training until reviewed and annotated. Manual phase annotations
are marked reviewed only when explicitly confirmed. Action labels must still
be checked against what the participant actually did.

Keep each per-node CSV together with its capture manifest in
`dataset/multinode/`. This is the canonical training dataset folder. When no
CSV paths are passed, `tools/train_model.py` reads every `*.csv` in this folder.
Add each new recording's CSVs and manifest there before retraining. From `Code/`:

```powershell
python tools/train_model.py --window 1.0 --output models/new_model
```

The current training tool creates per-sensor windows from all supplied Node
CSVs and trains one action classifier with the shared case labels. It does not
join simultaneous Node samples into a single multi-sensor feature vector;
independent device clocks are not synchronized by publishing Nodes in the same
MQTT message. Keep whole trials/sessions/subjects together for held-out
evaluation. Existing model bundles are not retrained automatically by
recording.

The existing stream sends the latest available sample for each Node. Its
20 Hz publish target and QoS 0 do not guarantee that every original IMU sample
arrives. Observed missing samples are reported in the manifest, never
interpolated. Switching to Mosquitto does not remove MQTT or guarantee unlimited
capacity; use the recorded rates and gap counters to assess an actual capture.

## Automated validation

```powershell
node --preserve-symlinks --preserve-symlinks-main test/test_imu_protocol.cjs
node --preserve-symlinks --preserve-symlinks-main test/test_multinode_recorder.cjs
node --preserve-symlinks --preserve-symlinks-main test/test_multinode_page.cjs
python -B test/test_multinode_training.py -v
python -B test/test_model_contract.py -v
```

These use local synthetic samples and an in-process MQTT client double, including
two independent 20 Hz IMUs. They validate capture, page handlers, CSV parsing and
training windows. They do not establish live broker throughput, BLE reliability
or board firmware build/upload success.
````

### 6.6 ไฟล์ `README_ML.md` — คู่มือระบบ Machine Learning & Feature Pipeline (147 บรรทัด)
````markdown
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
3. เลือก Node หลังบนและหลังล่าง แล้วเลือก **Case/Action Label กลาง** เช่น `case_flexion` เพียงครั้งเดียว
   * ทุก Node ที่เลือกจะได้ `action_label` เดียวกัน; ไม่เปลี่ยน label ตามตำแหน่งหรือ Node
   * กรอก `Subject ID`, `Session ID`, `Trial ID`
4. กด **เริ่มบันทึก 2 Node พร้อมกัน**; เมื่อกดหยุด ระบบสร้าง CSV แยก sensor และ manifest
5. Guided Phase Timer เป็นตัวเลือกเสริมและปิดไว้ตามค่าเริ่มต้น; ถ้าเปิด ระบบจะกำหนด phase cue ให้ทั้งสอง Node พร้อมกัน

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

## 7. Experimental plank phase pilot (six-axis IMUs)

`tools/plank_phase.py` and `config/plank_model.json` add a separate rule-based phase pilot. It does not replace the existing wrist action classifier and it is not a trained posture model.

The pilot needs two live real IMUs in the same MQTT frame: an upper-trunk sensor at `chest` or `upper_back`, and a lower-trunk sensor at `lumbar` or `pelvis`. Configure those mount sites on the Gateway `/nodes` page first. The defaults (`neck` and `lumbar`) do not form a valid plank pair. It uses each MPU-6050's fused pitch and roll from accelerometer + gyroscope. It ignores yaw because this IMU has no magnetometer.

In **AI Live Model Inference**, start **Plank Phase Pilot**, select the two nodes, then hold a correct plank and press **Calibrate correct plank (2 s)**. Calibration records 40 paired MQTT samples and stores both per-node and relative pitch/roll baselines in memory until the local server restarts or the selected sensor pair changes. The monitor reports a paired phase and independent upper/lower Node phases, each with normalized angular deviation `E` and trend `V`. The states are `normal`, `phase1_approaching_error`, `phase2_incorrect_posture`, and `phase3_recovery_trend`; their boundaries use the proposed 0.6 / 0.7 / 1.0 hysteresis bands and persistence times in `plank_model.json`.

The initial 12-degree score scales in that file are placeholders marked `validated: false`; tune them against labeled PlanKO recordings before relying on the output. These phases are experimental additions and are not outputs from the 2022 paper. The current serialized model is a wrist-only prototype with no trained phase model, so the pilot reports rule-based status and never presents an ML confidence score.

The two-sensor pilot reports only a generic sagittal or roll-angle deviation. It cannot reliably name hip-high versus excessive-arch until sensor-axis direction and labeled thresholds are validated. Load-distribution classification is unavailable because the Gateway currently publishes one pressure channel rather than separate arm and foot force measurements. Lateral-balance classification needs bilateral IMUs and labeled trials. Calibration and phase state are held in server memory.

## 8. How to report accuracy as a percentage

`E` is a normalized deviation score and `V` is a trend. Neither is model accuracy or confidence. Accuracy is computed only after comparing predictions with human-reviewed ground-truth labels.

For a first PlanKO evaluation, record complete trials for all four phase labels and include a ground-truth label for the paired phase and for each Node if per-Node accuracy is required. Update the three reference-label selectors during capture and review labels against video. Keep every row from a trial together. For a personalized model, calibrate and tune thresholds on earlier sessions, then test on separate later trials. For performance across people, use subject-grouped cross-validation or leave-one-subject-out; never randomly split individual 20 Hz samples or overlapping windows, since that leaks nearly identical sensor data across train and test.

Report accuracy percent (`correct predictions / all predictions × 100`) together with balanced accuracy, macro-F1, per-phase precision/recall, and a confusion matrix. Unknown predictions count as incorrect; coverage is shown separately. The capture CSV records ground truth and prediction separately for the pair, upper Node, and lower Node. Run the evaluator only on held-out trial files, for example:

```powershell
python tools/evaluate_plank_predictions.py data\P02_S02_trial-03_plank_phase_predictions.csv data\P03_S01_trial-02_plank_phase_predictions.csv --output reports\plank_accuracy.json
```

Use the validation split to select thresholds; report the final percentage once on a held-out test set that was not used for tuning. For streaming use, report phase-transition delay and dropped-sample rate separately from classification accuracy. The existing `train_model.py` already reports grouped-fold Accuracy and macro-F1 for labeled single-sensor classifiers, but its current wrist recordings cannot produce a valid Plank Phase or per-Node accuracy report.
````

### 6.7 ไฟล์ `SUBSCRIBER_PROMPT.md` — ข้อกำหนดสัญญาณ MQTT สำหรับแอป Subscriber (Expo / Web) (16 บรรทัด)
```markdown
# Prompt: PlanKO subscriber integration

Keep the sensor transport exactly Node N -> BLE -> Gateway -> Mosquitto -> subscriber. Do not add Wi-Fi/MQTT to Nodes, replace BLE, or fetch sensor data over HTTP for MQTT inference. Preserve PlatformIO role flags; gateway is COM5 and imu_node_1 is COM8.

Use public test.mosquitto.org. Gateway/native clients use MQTT TCP 1883; browser uses wss://test.mosquitto.org:8081/mqtt. MQTT 3.1.1, QoS 0, unique subscriber client ID, clean session, keepalive 30 seconds. Subscribe to planko/imu on every successful connection and retry after disconnect using a single timer. Never fall back to EMQX.

The planko-imu-v2 JSON has type=imu_stream, units={accel:"m/s^2",gyro:"rad/s",orientation:"degrees"}, gateway={clientId,bootId,mqttSeq,uptimeMs,connectedNodes} and nodes=[{nodeId,sensor_uid,mount_site,body_part,side,boot_id,seq,ts,pitch,roll,yaw,accel:[x,y,z],gyro:[x,y,z],valid,calibrated,relative_orientation_valid,isMock,dropped}]. Nodes may be empty. ts is sensor milliseconds; gateway.mqttSeq is the MQTT sequence, while nodes[].seq is the BLE sample sequence. Do not mix these counters or gateways.

Use WEB/imu_protocol.js sampleFromMessage and SampleWindow for recording/inference. Require finite six-axis SI values; do not substitute zero for missing gyro. Match node identity, actual mounting context, side and boot epoch. Ignore duplicate samples and reset windows on missing sequence, time gaps, reboot or context changes. Firmware IMU bias calibration is not a relative-orientation calibration profile.

Read /api/health from the local model server for feature schema, feature columns, units, window_seconds, sampling_hz, gap_reset_seconds and supported contexts. POST received raw MQTT rows to /api/predict-window. Compute features only through tools/imu_features.py window_features, shared by training and inference. This POST is local prediction computation after MQTT subscription; sensor data must never be replaced by /api/live in this flow.

Current model is unchanged: 40 features, 1-second window, 20 Hz, wrist/wrist/unknown only, wrist_down/wrist_neutral/wrist_up, raw six axes, no phase model. Show unknown and a reason for unsupported sites; do not claim neck/back support. Show RX rate separately from render rate. Receive count/observed sequence gaps cannot establish complete send/receive equality without matching sender counters/manifest.

Keep the existing hardware remote and HTTP dataset recorder available, and retain existing upload environments. Validate with Code/test/test_imu_protocol.cjs and Code/test/test_model_contract.py. Report actual build/hardware test results separately from mocked or synthetic checks.
```

### 6.8 ไฟล์ `compose.yaml` — Docker Compose สำหรับรัน Mosquitto Broker (15 บรรทัด)
```yaml
services:
  mosquitto:
    image: eclipse-mosquitto:2
    container_name: planko-mosquitto
    restart: unless-stopped
    ports:
      - "1883:1883"
      - "9001:9001"
    volumes:
      - ./docker/mosquitto/mosquitto.conf:/mosquitto/config/mosquitto.conf:ro
      - mosquitto_data:/mosquitto/data

volumes:
  mosquitto_data:
```

### 6.9 ไฟล์ `docker/mosquitto/mosquitto.conf` — ไฟล์ตั้งค่า Mosquitto (Port 1883 TCP & Port 9001 WebSockets) (20 บรรทัด)
```ini
per_listener_settings true
persistence true
persistence_location /mosquitto/data/
log_dest stdout
connection_messages true
log_type error
log_type warning
log_type notice
log_type information
log_type subscribe
log_type unsubscribe

listener 1883 0.0.0.0
protocol mqtt
allow_anonymous true

listener 9001 0.0.0.0
protocol websockets
allow_anonymous true
```

### 6.10 ไฟล์ `requirements-ml.txt` — รายการไลบรารี Python ของโปรเจกต์ (6 บรรทัด)
```text
numpy>=1.24
pandas>=2.0
scikit-learn>=1.3
joblib>=1.3
Flask>=3.0
```

### 6.11 ไฟล์ `platformio.ini` — การตั้งค่าบอร์ดและ Environment ทั้งหมดใน PlatformIO (127 บรรทัด)
```ini
; PlatformIO Project Configuration File for PlanKO ESP32 Controller
; https://docs.platformio.org/page/projectconf.html

[platformio]
default_envs = gateway

[env]
platform = espressif32
board = esp32-c3-devkitm-1
framework = arduino
monitor_speed = 115200
upload_speed = 115200
monitor_dtr = 1
monitor_rts = 0
board_build.partitions = huge_app.csv
build_flags =
    -DARDUINO_USB_MODE=1
    -DARDUINO_USB_CDC_ON_BOOT=1
lib_deps =
    bblanchon/ArduinoJson @ ^7.0.4
    h2zero/NimBLE-Arduino @ ^1.4.2
    knolleary/PubSubClient @ ^2.8
    sparkfun/SparkFun MAX3010x Pulse and Proximity Sensor Library @ ^1.1.2

; ==============================================================================
; 1. GATEWAY (BLE Central + Wi-Fi, MQTT, Dataset HTTP Server)
; ==============================================================================
[env:gateway]
upload_port = COM8
monitor_port = COM8
monitor_filters = esp32_exception_decoder
build_src_filter =
    +<main.cpp>
build_src_flags =
    -Wformat=2
    -Werror=format
build_flags = 
    ${env.build_flags}
    -DROLE_GATEWAY

; ==============================================================================
; 2. IMU NODES 1 - 4 (BLE Peripheral + MPU-6050 IMU Sensor)
; ==============================================================================
[env:imu_node_1]
upload_port = COM5
monitor_port = COM5
build_flags =
    ${env.build_flags}
    -DROLE_NODE
    -DNODE_ID=1
    -DENABLE_NODE1_MAX30102=0
lib_ignore = SparkFun MAX3010x Pulse and Proximity Sensor Library

[env:imu_node_2]
upload_port = COM9
monitor_port = COM9
build_flags =
    ${env.build_flags}
    -DROLE_NODE
    -DNODE_ID=2

[env:imu_node_3]
upload_port = COM5
monitor_port = COM5
build_flags =
    ${env.build_flags}
    -DROLE_NODE
    -DNODE_ID=3

[env:imu_node_4]
build_flags =
    ${env.build_flags}
    -DROLE_NODE
    -DNODE_ID=4

; Standalone RFP605-only target; it does not run the Gateway or BLE scanner.
; Original sandbox wiring: SIG=32, S0=25, S1=26, S2=27, S3=14.
[env:rfp605_sandbox]
board = esp32dev
monitor_speed = 115200
monitor_filters = esp32_exception_decoder
build_src_filter =
    +<rfp605_sandbox.cpp>
lib_deps =
build_flags =
    -DRFP605_SANDBOX
    -DRFP605_ADC_PIN=32
    -DRFP605_MUX_S0_PIN=25
    -DRFP605_MUX_S1_PIN=26
    -DRFP605_MUX_S2_PIN=27
    -DRFP605_MUX_S3_PIN=14
    -DRFP605_ACTIVE_MASK=0x003F
    -DRFP605_CALIBRATION_SAMPLES=20
    -DRFP605_PRESS_DELTA_RAW=100
    -DRFP605_RELEASE_DELTA_RAW=50
    -DRFP605_MAX_PRESS_DELTA_RAW=300
    -DRFP605_MAX_RELEASE_DELTA_RAW=150
    -DRFP605_DEBUG_SCAN=1

; Gateway + RFP605 on the original ESP32 sandbox board and wiring.
[env:gateway_rfp605]
upload_port = COM9
monitor_port = COM9
board = esp32dev
monitor_filters = esp32_exception_decoder
build_src_filter =
    +<main.cpp>
    +<rfp605_sandbox.cpp>
build_src_flags =
    -Wformat=2
    -Werror=format
build_flags =
    -DROLE_GATEWAY
    -DRFP605_GATEWAY_INTEGRATION
    -DRFP605_ADC_PIN=32
    -DRFP605_MUX_S0_PIN=25
    -DRFP605_MUX_S1_PIN=26
    -DRFP605_MUX_S2_PIN=27
    -DRFP605_MUX_S3_PIN=14
    -DRFP605_ACTIVE_MASK=0x003F
    -DRFP605_CALIBRATION_SAMPLES=20
    -DRFP605_PRESS_DELTA_RAW=100
    -DRFP605_RELEASE_DELTA_RAW=50
    -DRFP605_MAX_PRESS_DELTA_RAW=300
    -DRFP605_MAX_RELEASE_DELTA_RAW=150
    -DRFP605_DEBUG_SCAN=1
```

### 6.12 ไฟล์ `src/common_types.h` — โครงสร้างแพ็กเก็ต BLE (ImuPacket, RawImuPacket, CombinedImuPacket, BioPacket, BatteryPacket) (127 บรรทัด)
```cpp
#ifndef COMMON_TYPES_H
#define COMMON_TYPES_H

#include <Arduino.h>

// BLE Service & Characteristic UUIDs
#define PLANKO_SERVICE_UUID        "4E6F6465-0001-1000-8000-00805F9B34FB"
#define PLANKO_CHARACTERISTIC_UUID "4E6F6465-0002-1000-8000-00805F9B34FB"

// Compact telemetry packet retained for the existing MQTT stream.
// It fits the default 23-byte ATT MTU without negotiation.
struct __attribute__((packed)) ImuPacket {
    uint8_t  nodeId;       // Node ID (1 - 4)
    uint16_t sequence;     // Packet Sequence number (for packet loss tracking)
    uint32_t timestamp;    // millis() on node
    int16_t  pitch;        // Pitch in degrees * 100
    int16_t  roll;         // Roll in degrees * 100
    int16_t  yaw;          // Yaw in degrees * 100
    int16_t  ax;           // Accel X (m/s^2 * 100)
    int16_t  ay;           // Accel Y (m/s^2 * 100)
    int16_t  az;           // Accel Z (m/s^2 * 100)
    uint8_t  status;       // 1 = Calibrated/OK, 0 = Uncalibrated/Error
};

// Legacy second packet, retained so a node can fall back for gateways that
// negotiate only the default ATT MTU of 23 bytes.
#define RAW_IMU_PACKET_MARKER 0xA4
#define RAW_IMU_PACKET_MARKER_MASK 0xFE
struct __attribute__((packed)) RawImuPacket {
    uint8_t  nodeId;
    uint16_t sequence;
    uint32_t timestamp;
    int16_t  ax;            // MPU-6050 raw accelerometer register value
    int16_t  ay;
    int16_t  az;
    int16_t  gx;            // MPU-6050 raw gyroscope register value
    int16_t  gy;
    int16_t  gz;
    uint8_t  marker;        // Marker high bits; low bit carries calibrated status
};

// Single BLE notification carrying the live values and raw six-axis sample.
// It requires ATT MTU >= 36, negotiated by both the node and gateway.
#define COMBINED_IMU_PACKET_MARKER 0xC4
struct __attribute__((packed)) CombinedImuPacket {
    uint8_t  nodeId;
    uint16_t sequence;
    uint32_t timestamp;
    int16_t  pitch;
    int16_t  roll;
    int16_t  yaw;
    int16_t  ax;
    int16_t  ay;
    int16_t  az;
    int16_t  rawAx;
    int16_t  rawAy;
    int16_t  rawAz;
    int16_t  rawGx;
    int16_t  rawGy;
    int16_t  rawGz;
    uint8_t  status;
    uint8_t  marker;
};

// Biomarker packet (Heart Rate, SpO2, Red/IR PPG) for MAX30102 on Node 1
#define BIO_PACKET_MARKER 0xB4
#define BIO_PACKET_MARKER_MASK 0xFE
struct __attribute__((packed)) BioPacket {
    uint8_t  nodeId;       // Node ID (1)
    uint16_t sequence;     // Packet Sequence number
    uint32_t timestamp;    // millis() on node
    uint16_t bpm;          // Heart rate (BPM * 10, e.g. 720 = 72.0 BPM)
    uint8_t  spo2;         // SpO2 percentage (e.g. 98 = 98%)
    uint8_t  finger;       // 1 = finger detected, 0 = no finger
    uint32_t red;          // Raw Red LED reading
    uint32_t ir;           // Raw IR LED reading
    uint8_t  marker;       // BIO_PACKET_MARKER (0xB4)
};

// Independent low-rate notification; fits even the default ATT MTU.
#define BATTERY_PACKET_MARKER 0xD4
#define BATTERY_PERCENT_UNKNOWN 255
struct __attribute__((packed)) BatteryPacket {
    uint8_t nodeId;
    uint16_t millivolts;
    uint8_t percent;       // 0..100 estimate, or 255 for invalid voltage
    uint8_t marker;
};
static_assert(sizeof(BatteryPacket) == 5, "BatteryPacket must remain 5 bytes");

static_assert(sizeof(ImuPacket) == 20, "ImuPacket must remain 20 bytes");
static_assert(sizeof(RawImuPacket) == 20, "RawImuPacket must remain 20 bytes");
static_assert(sizeof(CombinedImuPacket) == 33,
              "CombinedImuPacket must remain 33 bytes");
static_assert(sizeof(BioPacket) == 20, "BioPacket must remain 20 bytes");

// Telemetry state stored on Gateway for each node
struct NodeTelemetry {
    volatile bool connected; // volatile: written by BLE callback task, read by main loop
    uint16_t lastSequence;
    uint32_t totalDropped;
    uint32_t lastTimestamp;
    float    pitch;
    float    roll;
    float    yaw;
    float    ax;
    float    ay;
    float    az;
    float    gx;
    float    gy;
    float    gz;
    uint8_t  status;
    float    bpm;
    uint8_t  spo2;
    bool     finger;
    uint32_t rawRed;
    uint32_t rawIr;
    uint32_t lastRawReceivedMillis;
    uint16_t batteryMillivolts;
    uint8_t batteryPercent;
    bool batteryReceived;
    uint32_t lastBatteryReceivedMillis;
    volatile unsigned long lastReceivedMillis; // volatile: written by BLE callback task, read by main loop
};

#endif // COMMON_TYPES_H
```

### 6.13 ไฟล์ `src/mpu6050_reader.h` — ไดรเวอร์อ่านค่า MPU-6050 6 แกน, I2C Recovery และ Complementary Filter (292 บรรทัด)
```cpp
#ifndef MPU6050_READER_H
#define MPU6050_READER_H

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

class Mpu6050Reader {
public:
    float pitch = 0.0f;
    float roll = 0.0f;
    float yaw = 0.0f;

    float ax = 0.0f;
    float ay = 0.0f;
    float az = 0.0f;

    float gx = 0.0f;
    float gy = 0.0f;
    float gz = 0.0f;

    int16_t rawAx = 0;
    int16_t rawAy = 0;
    int16_t rawAz = 0;
    int16_t rawGx = 0;
    int16_t rawGy = 0;
    int16_t rawGz = 0;

    bool isDetected = false;
    bool isCalibrated = false;

    // Routine to unwedge I2C bus if a slave hung holding SDA LOW
    static void recoverI2CBus(int sdaPin, int sclPin) {
        pinMode(sdaPin, INPUT_PULLUP);
        pinMode(sclPin, OUTPUT);
        // Pulse SCL 9 times to allow slave device to finish byte transmission
        for (int i = 0; i < 9; i++) {
            digitalWrite(sclPin, LOW);
            delayMicroseconds(5);
            digitalWrite(sclPin, HIGH);
            delayMicroseconds(5);
        }
        // Send STOP condition
        pinMode(sdaPin, OUTPUT);
        digitalWrite(sdaPin, LOW);
        delayMicroseconds(5);
        digitalWrite(sclPin, HIGH);
        delayMicroseconds(5);
        digitalWrite(sdaPin, HIGH);
        delayMicroseconds(5);
        pinMode(sdaPin, INPUT_PULLUP);
        pinMode(sclPin, INPUT_PULLUP);
    }

    bool begin(int sdaPin = 8, int sclPin = 9) {
        _sdaPin = sdaPin;
        _sclPin = sclPin;

        recoverI2CBus(_sdaPin, _sclPin);

        Wire.end();
        delay(10);
        // Use 100kHz Standard Mode for rock-solid signal integrity over jumper wires
        Wire.begin(_sdaPin, _sclPin, 100000);
        Wire.setTimeOut(100);
        delay(40);

        // Auto-detect I2C address: 0x68 (AD0=GND) or 0x69 (AD0=3.3V)
        uint8_t foundAddr = 0;
        Wire.beginTransmission(0x68);
        if (Wire.endTransmission() == 0) {
            foundAddr = 0x68;
        } else {
            Wire.beginTransmission(0x69);
            if (Wire.endTransmission() == 0) {
                foundAddr = 0x69;
            }
        }

        if (foundAddr == 0) {
            isDetected = false;
            return false;
        }
        _mpuAddr = foundAddr;

        // Check MPU-6050 WhoAmI register (0x75)
        Wire.beginTransmission(_mpuAddr);
        Wire.write(0x75);
        if (Wire.endTransmission(false) != 0) {
            isDetected = false;
            return false;
        }

        if (Wire.requestFrom((uint8_t)_mpuAddr, (uint8_t)1) > 0) {
            uint8_t whoAmI = Wire.read();
            Serial.printf("🔍 [MPU6050] Found at 0x%02X, WHO_AM_I = 0x%02X\n", _mpuAddr, whoAmI);
        }

        // 1. Reset MPU-6050 (write 0x80 to PWR_MGMT_1, reg 0x6B)
        writeRegister(0x6B, 0x80);
        delay(60);

        // 2. Wake up MPU-6050 and set clock source to PLL with X Gyro (reg 0x6B = 0x01)
        writeRegister(0x6B, 0x01);
        delay(30);

        // 3. Set DLPF (Digital Low Pass Filter) to ~42Hz (reg 0x1A = 0x03)
        writeRegister(0x1A, 0x03);

        // 4. Configure Gyro Full Scale to +/- 500 deg/s (FS_SEL = 1, reg 0x1B = 0x08)
        writeRegister(0x1B, 0x08);

        // 5. Configure Accel Full Scale to +/- 4g (AFS_SEL = 1, reg 0x1C = 0x08)
        writeRegister(0x1C, 0x08);
        delay(20);

        isDetected = true;
        consecutiveErrors = 0;
        calibrate(80);
        lastFilterMicros = micros();
        return true;
    }

    void calibrate(int samples = 80) {
        if (!isDetected) return;

        Serial.println("⚖️ [MPU6050] Calibrating IMU sensor (Keep still on flat surface)...");
        long gxSum = 0, gySum = 0, gzSum = 0;
        long axSum = 0, aySum = 0, azSum = 0;
        int validSamples = 0;

        for (int i = 0; i < samples; i++) {
            int16_t rAx, rAy, rAz, rTemp, rGx, rGy, rGz;
            if (readRawData(rAx, rAy, rAz, rTemp, rGx, rGy, rGz)) {
                axSum += rAx;
                aySum += rAy;
                azSum += rAz;
                gxSum += rGx;
                gySum += rGy;
                gzSum += rGz;
                validSamples++;
            }
            delay(10);
        }

        if (validSamples > 20) {
            gxOffset = (float)gxSum / (validSamples * 65.5f);
            gyOffset = (float)gySum / (validSamples * 65.5f);
            gzOffset = (float)gzSum / (validSamples * 65.5f);

            axOffset = ((float)axSum / validSamples) / 8192.0f;
            ayOffset = ((float)aySum / validSamples) / 8192.0f;
            azOffset = (((float)azSum / validSamples) / 8192.0f) - 1.0f;

            isCalibrated = true;
            Serial.printf("✅ [MPU6050] Calibrated (%d samples): GyroBias[%.2f, %.2f, %.2f] deg/s | AccelBias[%.2f, %.2f, %.2f] g\n",
                          validSamples, gxOffset, gyOffset, gzOffset, axOffset, ayOffset, azOffset);
        } else {
            isCalibrated = false;
            Serial.printf("⚠️ [MPU6050] Calibration incomplete (only %d valid samples). Offsets cleared.\n", validSamples);
        }
    }

    bool update() {
        unsigned long now = millis();

        // If sensor was not detected, retry periodically every 2 seconds
        if (!isDetected) {
            if (now - lastRetryMillis >= 2000) {
                lastRetryMillis = now;
                // Quick address probe
                bool detected = false;
                Wire.beginTransmission(0x68);
                if (Wire.endTransmission() == 0) detected = true;
                if (!detected) {
                    Wire.beginTransmission(0x69);
                    if (Wire.endTransmission() == 0) detected = true;
                }
                if (detected) {
                    Serial.println("🎉 [MPU6050] Sensor detected! Initializing...");
                    begin(_sdaPin, _sclPin);
                }
            }
            return false;
        }

        int16_t rAx, rAy, rAz, rTemp, rGx, rGy, rGz;
        if (!readRawData(rAx, rAy, rAz, rTemp, rGx, rGy, rGz)) {
            return false;
        }

        this->rawAx = rAx;
        this->rawAy = rAy;
        this->rawAz = rAz;
        this->rawGx = rGx;
        this->rawGy = rGy;
        this->rawGz = rGz;

        // Convert raw accel to m/s^2 (AFS_SEL=1 -> 8192 LSB/g, 1g = 9.80665 m/s^2)
        ax = ((float)rAx / 8192.0f - axOffset) * 9.80665f;
        ay = ((float)rAy / 8192.0f - ayOffset) * 9.80665f;
        az = ((float)rAz / 8192.0f - azOffset) * 9.80665f;

        // Convert raw gyro to deg/s (FS_SEL=1 -> 65.5 LSB/(deg/s))
        gx = ((float)rGx / 65.5f) - gxOffset;
        gy = ((float)rGy / 65.5f) - gyOffset;
        gz = ((float)rGz / 65.5f) - gzOffset;

        // Suppress residual gyro bias/noise after startup calibration. This is
        // especially important for yaw, which has no accelerometer reference.
        const float gyroDeadbandDps = 0.5f;
        if (fabsf(gx) < gyroDeadbandDps) gx = 0.0f;
        if (fabsf(gy) < gyroDeadbandDps) gy = 0.0f;
        if (fabsf(gz) < gyroDeadbandDps) gz = 0.0f;

        // Compute delta time
        unsigned long currentMicros = micros();
        float dt = (currentMicros - lastFilterMicros) / 1000000.0f;
        if (dt <= 0.0f || dt > 0.5f) dt = 0.05f; // 20Hz (50ms interval)
        lastFilterMicros = currentMicros;

        // Calculate pitch and roll from accelerometer (in degrees)
        float accPitch = atan2(-ax, sqrt(ay * ay + az * az)) * 180.0f / M_PI;
        float accRoll  = atan2(ay, az) * 180.0f / M_PI;

        // MPU6050 axes: X gyro rotates roll; Y gyro rotates pitch.
        // Complementary Filter: 96% gyroscope integration + 4% accelerometer reference.
        pitch = 0.96f * (pitch + gy * dt) + 0.04f * accPitch;
        roll  = 0.96f * (roll + gx * dt) + 0.04f * accRoll;
        yaw  += gz * dt;

        if (yaw > 180.0f) yaw -= 360.0f;
        if (yaw < -180.0f) yaw += 360.0f;

        return true;
    }

private:
    uint8_t _mpuAddr = 0x68;
    int _sdaPin = 8;
    int _sclPin = 9;
    uint8_t consecutiveErrors = 0;
    static const uint8_t MAX_CONSECUTIVE_ERRORS = 15;

    float gxOffset = 0, gyOffset = 0, gzOffset = 0;
    float axOffset = 0, ayOffset = 0, azOffset = 0;
    unsigned long lastFilterMicros = 0;
    unsigned long lastRetryMillis = 0;

    bool writeRegister(uint8_t reg, uint8_t val) {
        Wire.beginTransmission(_mpuAddr);
        Wire.write(reg);
        Wire.write(val);
        return (Wire.endTransmission() == 0);
    }

    bool readRawData(int16_t &rawAx, int16_t &rawAy, int16_t &rawAz,
                     int16_t &rawTemp, int16_t &rawGx, int16_t &rawGy, int16_t &rawGz) {
        Wire.beginTransmission(_mpuAddr);
        Wire.write(0x3B);
        if (Wire.endTransmission(false) != 0) {
            consecutiveErrors++;
            if (consecutiveErrors >= MAX_CONSECUTIVE_ERRORS) {
                isDetected = false;
                Serial.printf("❌ [MPU6050] Bus write error (streak: %d)\n", consecutiveErrors);
            }
            return false;
        }

        if (Wire.requestFrom((uint8_t)_mpuAddr, (uint8_t)14) < 14) {
            consecutiveErrors++;
            if (consecutiveErrors >= MAX_CONSECUTIVE_ERRORS) {
                isDetected = false;
                Serial.printf("❌ [MPU6050] Bus read error (streak: %d)\n", consecutiveErrors);
            }
            return false;
        }

        consecutiveErrors = 0;
        rawAx   = (Wire.read() << 8) | Wire.read();
        rawAy   = (Wire.read() << 8) | Wire.read();
        rawAz   = (Wire.read() << 8) | Wire.read();
        rawTemp = (Wire.read() << 8) | Wire.read();
        rawGx   = (Wire.read() << 8) | Wire.read();
        rawGy   = (Wire.read() << 8) | Wire.read();
        rawGz   = (Wire.read() << 8) | Wire.read();
        return true;
    }
};

#endif // MPU6050_READER_H
```

### 6.14 ไฟล์ `src/rfp605_sensor.h` — Header สำหรับเซ็นเซอร์วัดแรงกด RFP-605 ผ่าน CD74HC4067 (23 บรรทัด)
```cpp
#pragma once

#include <Arduino.h>

struct Rfp605Sample {
  uint8_t channel;
  uint32_t sequence;
  uint32_t sampleMs;
  uint16_t baselineRaw;
  uint16_t pressDeltaRaw;
  uint16_t deviationRaw;
  uint16_t pressureRaw;
  uint16_t voltageMv;
  bool pressed;
};

void rfp605Begin();
void rfp605Start();
size_t rfp605Update(Rfp605Sample *samples, size_t capacity);
size_t rfp605ReadQueuedSamples(Rfp605Sample *samples, size_t capacity);
uint16_t rfp605PressedMask();
uint32_t rfp605DroppedSamples();
```

### 6.15 ไฟล์ `src/rfp605_sandbox.cpp` — โค้ดสแกนเซ็นเซอร์วัดแรงกด RFP-605 16 ช่องสัญญาณ (CD74HC4067 Mux) (459 บรรทัด)
```cpp
#include <Arduino.h>
#include "rfp605_sensor.h"
#if defined(RFP605_GATEWAY_INTEGRATION)
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <freertos/queue.h>
#endif

#if defined(RFP605_SANDBOX) || defined(RFP605_GATEWAY_INTEGRATION)

#ifndef RFP605_ADC_PIN
#define RFP605_ADC_PIN 34
#endif
#ifndef RFP605_MUX_S0_PIN
#define RFP605_MUX_S0_PIN 25
#endif
#ifndef RFP605_MUX_S1_PIN
#define RFP605_MUX_S1_PIN 26
#endif
#ifndef RFP605_MUX_S2_PIN
#define RFP605_MUX_S2_PIN 27
#endif
#ifndef RFP605_MUX_S3_PIN
#define RFP605_MUX_S3_PIN 14
#endif
// Bit N enables mux channel Cn. For C0-C5: 0x003F.
#ifndef RFP605_ACTIVE_MASK
#define RFP605_ACTIVE_MASK 0x003F
#endif
#ifndef RFP605_CALIBRATION_SAMPLES
#define RFP605_CALIBRATION_SAMPLES 15
#endif
#ifndef RFP605_PRESS_DELTA_RAW
#define RFP605_PRESS_DELTA_RAW 300
#endif
#ifndef RFP605_RELEASE_DELTA_RAW
#define RFP605_RELEASE_DELTA_RAW 150
#endif
#ifndef RFP605_MAX_PRESS_DELTA_RAW
#define RFP605_MAX_PRESS_DELTA_RAW 500
#endif
#ifndef RFP605_MAX_RELEASE_DELTA_RAW
#define RFP605_MAX_RELEASE_DELTA_RAW 250
#endif
#ifndef RFP605_DEBUG_SCAN
#define RFP605_DEBUG_SCAN 0
#endif

static constexpr uint8_t ADC_PIN = RFP605_ADC_PIN;
static constexpr uint8_t MUX_S0_PIN = RFP605_MUX_S0_PIN;
static constexpr uint8_t MUX_S1_PIN = RFP605_MUX_S1_PIN;
static constexpr uint8_t MUX_S2_PIN = RFP605_MUX_S2_PIN;
static constexpr uint8_t MUX_S3_PIN = RFP605_MUX_S3_PIN;
static constexpr uint16_t ACTIVE_CHANNEL_MASK =
    static_cast<uint16_t>(RFP605_ACTIVE_MASK);

static constexpr uint8_t CHANNEL_COUNT = 16;
static constexpr uint32_t SCAN_INTERVAL_MS = 50; // 20 Hz target per channel
static constexpr uint8_t ADC_AVERAGE_COUNT = 4;
static constexpr uint8_t ADC_DISCARD_COUNT = 4;
static constexpr uint8_t PRESS_CONFIRM_SAMPLES = 1;
static constexpr uint8_t RELEASE_CONFIRM_SAMPLES = 2;
static constexpr uint16_t MUX_SETTLE_US = 200;

static const char *const SENSOR_IDS[CHANNEL_COUNT] = {
    "RFP605_C00", "RFP605_C01", "RFP605_C02", "RFP605_C03",
    "RFP605_C04", "RFP605_C05", "RFP605_C06", "RFP605_C07",
    "RFP605_C08", "RFP605_C09", "RFP605_C10", "RFP605_C11",
    "RFP605_C12", "RFP605_C13", "RFP605_C14", "RFP605_C15"};

struct ChannelState {
  uint32_t baselineSum = 0;
  uint32_t sequence = 0;
  uint16_t baselineRaw = 0;
  uint16_t pressDeltaRaw = 0;
  uint16_t releaseDeltaRaw = 0;
  uint16_t calibrationMinRaw = 4095;
  uint16_t calibrationMaxRaw = 0;
  uint8_t pressCount = 0;
  uint8_t releaseCount = 0;
  bool pressed = false;
};

static ChannelState channels[CHANNEL_COUNT];
static uint32_t lastScanMs = 0;
#if defined(RFP605_GATEWAY_INTEGRATION)
static constexpr UBaseType_t RFP605_QUEUE_CAPACITY = 256;
static QueueHandle_t rfp605SampleQueue = nullptr;
static TaskHandle_t rfp605TaskHandle = nullptr;
static portMUX_TYPE rfp605StateMux = portMUX_INITIALIZER_UNLOCKED;
static std::atomic<uint32_t> rfp605QueueDropCount{0};
#endif

static void selectMuxChannel(uint8_t channel) {
  digitalWrite(MUX_S0_PIN, (channel & 0x01) ? HIGH : LOW);
  digitalWrite(MUX_S1_PIN, (channel & 0x02) ? HIGH : LOW);
  digitalWrite(MUX_S2_PIN, (channel & 0x04) ? HIGH : LOW);
  digitalWrite(MUX_S3_PIN, (channel & 0x08) ? HIGH : LOW);
}

static uint16_t readRawAverage(uint8_t channel) {
  selectMuxChannel(channel);
  delayMicroseconds(MUX_SETTLE_US);

  // Let the mux/ADC settle, then flush conversions left from the prior channel.
  for (uint8_t i = 0; i < ADC_DISCARD_COUNT; ++i)
    (void)analogRead(ADC_PIN);

  uint32_t total = 0;
  for (uint8_t i = 0; i < ADC_AVERAGE_COUNT; ++i)
    total += static_cast<uint16_t>(analogRead(ADC_PIN));

  return static_cast<uint16_t>(total / ADC_AVERAGE_COUNT);
}

static uint16_t readMillivoltsAverage() {
  uint32_t total = 0;
  for (uint8_t i = 0; i < ADC_AVERAGE_COUNT; ++i)
    total += static_cast<uint16_t>(analogReadMilliVolts(ADC_PIN));

  return static_cast<uint16_t>(total / ADC_AVERAGE_COUNT);
}

static void calibrateReleasedBaseline() {
  Serial.println("\n========================================");
  Serial.println("  [Auto-Calibrating active RFP605 channels] Leave sensors released...  ");

  Serial.printf(
      "{\"type\":\"calibrating\",\"samples_per_channel\":%u,"
      "\"duration_ms\":%lu,\"instruction\":\"leave_sensors_released\"}\n",
      static_cast<unsigned>(RFP605_CALIBRATION_SAMPLES),
      static_cast<unsigned long>(RFP605_CALIBRATION_SAMPLES * SCAN_INTERVAL_MS));

  for (uint8_t sample = 0; sample < RFP605_CALIBRATION_SAMPLES; ++sample) {
    for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
      if ((ACTIVE_CHANNEL_MASK & (1U << channel)) == 0)
        continue;

      const uint16_t raw = readRawAverage(channel);
      channels[channel].baselineSum += raw;
      if (raw < channels[channel].calibrationMinRaw)
        channels[channel].calibrationMinRaw = raw;
      if (raw > channels[channel].calibrationMaxRaw)
        channels[channel].calibrationMaxRaw = raw;
    }
    delay(SCAN_INTERVAL_MS);
  }

  for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
    if ((ACTIVE_CHANNEL_MASK & (1U << channel)) == 0)
      continue;

    ChannelState &state = channels[channel];
    state.baselineRaw = static_cast<uint16_t>(
        state.baselineSum / RFP605_CALIBRATION_SAMPLES);

    // Raise thresholds above observed idle noise, while keeping a configurable
    // minimum response for sensors with a very quiet baseline.
    const uint16_t noiseRange =
        state.calibrationMaxRaw - state.calibrationMinRaw;
    const uint16_t adaptivePressDelta = static_cast<uint16_t>(noiseRange * 2U);
    const uint16_t adaptiveReleaseDelta = noiseRange;
    uint16_t pressDelta =
        adaptivePressDelta > RFP605_PRESS_DELTA_RAW
            ? adaptivePressDelta
            : RFP605_PRESS_DELTA_RAW;
    uint16_t releaseDelta =
        adaptiveReleaseDelta > RFP605_RELEASE_DELTA_RAW
            ? adaptiveReleaseDelta
            : RFP605_RELEASE_DELTA_RAW;

    // A single calibration spike must not make a channel impossible to press.
    if (pressDelta > RFP605_MAX_PRESS_DELTA_RAW)
      pressDelta = RFP605_MAX_PRESS_DELTA_RAW;
    if (releaseDelta > RFP605_MAX_RELEASE_DELTA_RAW)
      releaseDelta = RFP605_MAX_RELEASE_DELTA_RAW;
    if (releaseDelta >= pressDelta)
      releaseDelta = pressDelta / 2U;

    state.pressDeltaRaw = pressDelta;
    state.releaseDeltaRaw = releaseDelta;

    Serial.printf("  C%u Baseline = %u | Threshold = +%u\n",
                  static_cast<unsigned>(channel),
                  static_cast<unsigned>(state.baselineRaw),
                  static_cast<unsigned>(state.pressDeltaRaw));
  }

  Serial.println("  [Calibration Complete] พร้อมใช้งาน!");
  Serial.println("========================================\n");

  Serial.printf(
      "{\"type\":\"ready\",\"mux\":\"CD74HC4067\","
      "\"adc_gpio\":%u,\"active_mask\":\"0x%04X\","
      "\"scan_interval_ms\":%lu,\"debug_scan\":%u}\n",
      static_cast<unsigned>(ADC_PIN),
      static_cast<unsigned>(ACTIVE_CHANNEL_MASK),
      static_cast<unsigned long>(SCAN_INTERVAL_MS),
      static_cast<unsigned>(RFP605_DEBUG_SCAN));
}

static bool updatePressedState(ChannelState &state, uint16_t raw) {
  // With pull-down resistor on SIG, voltage increases when sensor is pressed.
  const uint16_t deviation = raw > state.baselineRaw
                                 ? static_cast<uint16_t>(raw - state.baselineRaw)
                                 : 0;

  if (!state.pressed) {
    if (deviation >= state.pressDeltaRaw) {
      if (state.pressCount < PRESS_CONFIRM_SAMPLES)
        ++state.pressCount;
    } else {
      state.pressCount = 0;
    }

    if (state.pressCount >= PRESS_CONFIRM_SAMPLES) {
      state.pressed = true;
      state.pressCount = 0;
      state.releaseCount = 0;
    }
  } else {
    if (deviation <= state.releaseDeltaRaw) {
      if (state.releaseCount < RELEASE_CONFIRM_SAMPLES)
        ++state.releaseCount;
    } else {
      state.releaseCount = 0;
    }

    if (state.releaseCount >= RELEASE_CONFIRM_SAMPLES) {
      state.pressed = false;
      state.releaseCount = 0;
      state.pressCount = 0;
    }
  }

  return state.pressed && deviation > state.releaseDeltaRaw;
}

void rfp605Begin() {
  pinMode(MUX_S0_PIN, OUTPUT);
  pinMode(MUX_S1_PIN, OUTPUT);
  pinMode(MUX_S2_PIN, OUTPUT);
  pinMode(MUX_S3_PIN, OUTPUT);
  selectMuxChannel(0);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  pinMode(ADC_PIN, INPUT);

  calibrateReleasedBaseline();
}

#if defined(RFP605_GATEWAY_INTEGRATION)
static void rfp605SamplerTask(void *parameter) {
  (void)parameter;
  Rfp605Sample batch[CHANNEL_COUNT] = {};
  for (;;) {
    const size_t count = rfp605Update(batch, CHANNEL_COUNT);
    for (size_t i = 0; i < count; ++i) {
      if (xQueueSend(rfp605SampleQueue, &batch[i], 0) != pdTRUE) {
        // Keep the newest pressure readings if the gateway loop is busy.
        Rfp605Sample discarded = {};
        if (xQueueReceive(rfp605SampleQueue, &discarded, 0) == pdTRUE) {
          rfp605QueueDropCount.fetch_add(1, std::memory_order_relaxed);
          (void)xQueueSend(rfp605SampleQueue, &batch[i], 0);
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void rfp605Start() {
  if (rfp605TaskHandle != nullptr)
    return;
  rfp605SampleQueue = xQueueCreate(RFP605_QUEUE_CAPACITY,
                                   sizeof(Rfp605Sample));
  if (rfp605SampleQueue == nullptr) {
    Serial.println("[RFP605] Failed to create sample queue");
    return;
  }
  // Use an unpinned task so this target works on both single-core ESP32-C3
  // and dual-core ESP32 boards.
  const BaseType_t result =
      xTaskCreate(rfp605SamplerTask, "rfp605_scan", 4096, nullptr, 1,
                  &rfp605TaskHandle);
  if (result != pdPASS) {
    rfp605TaskHandle = nullptr;
    vQueueDelete(rfp605SampleQueue);
    rfp605SampleQueue = nullptr;
    Serial.println("[RFP605] Failed to start scanner task");
    return;
  }
  Serial.printf("[RFP605] Background scan active at 20 Hz/channel; queue=%u\n",
                static_cast<unsigned>(RFP605_QUEUE_CAPACITY));
}

size_t rfp605ReadQueuedSamples(Rfp605Sample *samples, size_t capacity) {
  if (rfp605SampleQueue == nullptr || samples == nullptr)
    return 0;
  size_t count = 0;
  while (count < capacity &&
         xQueueReceive(rfp605SampleQueue, &samples[count], 0) == pdTRUE) {
    ++count;
  }
  return count;
}

uint32_t rfp605DroppedSamples() {
  return rfp605QueueDropCount.load(std::memory_order_relaxed);
}
#endif

size_t rfp605Update(Rfp605Sample *samples, size_t capacity) {
  const uint32_t now = millis();
  if (now - lastScanMs < SCAN_INTERVAL_MS)
    return 0;
  lastScanMs = now;
  size_t sampleCount = 0;
#if RFP605_DEBUG_SCAN
  static uint32_t lastDebugMs = 0;
  const bool logScan = now - lastDebugMs >= 1000;
  if (logScan)
    lastDebugMs = now;
#endif

  for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
    if ((ACTIVE_CHANNEL_MASK & (1U << channel)) == 0)
      continue;

    ChannelState &state = channels[channel];
    const uint16_t raw = readRawAverage(channel);
    const uint32_t sequence = state.sequence++;
#if defined(RFP605_GATEWAY_INTEGRATION)
    portENTER_CRITICAL(&rfp605StateMux);
    const bool wasPressed = state.pressed;
    (void)updatePressedState(state, raw);
    const bool pressed = state.pressed;
    const bool stateChanged = wasPressed != pressed;
    portEXIT_CRITICAL(&rfp605StateMux);
#else
    const bool active = updatePressedState(state, raw);
    const bool pressed = state.pressed;
#endif

    const uint16_t deviation = raw >= state.baselineRaw
                                   ? raw - state.baselineRaw
                                   : state.baselineRaw - raw;
    const uint32_t sampleMs = millis();

#if RFP605_DEBUG_SCAN
    if (logScan) {
      const int delta = static_cast<int>(raw) - state.baselineRaw;
      Serial.printf(
          "[RFP605 ADC] C%u raw=%u baseline=%u delta=%d press=+%u release=+%u pressed=%u\n",
          static_cast<unsigned>(channel), static_cast<unsigned>(raw),
          static_cast<unsigned>(state.baselineRaw), delta,
          static_cast<unsigned>(state.pressDeltaRaw),
          static_cast<unsigned>(state.releaseDeltaRaw), pressed ? 1U : 0U);
    }
#endif
#if defined(RFP605_GATEWAY_INTEGRATION)
    // Stream live samples while pressed and one final sample on release.
    if (!pressed && !stateChanged)
      continue;
#else
    // Standalone sandbox keeps its compact pressed-sample behavior.
    if (!active)
      continue;
#endif

    const uint16_t millivolts = readMillivoltsAverage();
    if (samples != nullptr && sampleCount < capacity) {
      Rfp605Sample &sample = samples[sampleCount++];
      sample.channel = channel;
      sample.sequence = sequence;
      sample.sampleMs = sampleMs;
      sample.baselineRaw = state.baselineRaw;
      sample.pressDeltaRaw = state.pressDeltaRaw;
      sample.deviationRaw = deviation;
      sample.pressureRaw = raw;
      sample.voltageMv = millivolts;
      sample.pressed = pressed;
    }
  }
  return sampleCount;
}

uint16_t rfp605PressedMask() {
  uint16_t mask = 0;
#if defined(RFP605_GATEWAY_INTEGRATION)
  portENTER_CRITICAL(&rfp605StateMux);
#endif
  for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
    if ((ACTIVE_CHANNEL_MASK & (1U << channel)) && channels[channel].pressed)
      mask |= static_cast<uint16_t>(1U << channel);
  }
#if defined(RFP605_GATEWAY_INTEGRATION)
  portEXIT_CRITICAL(&rfp605StateMux);
#endif
  return mask;
}

#if defined(RFP605_SANDBOX) && !defined(RFP605_GATEWAY_INTEGRATION)
void setup() {
  Serial.begin(115200);
  delay(1000);
  rfp605Begin();
}

void loop() {
  Rfp605Sample samples[CHANNEL_COUNT] = {};
  const size_t count = rfp605Update(samples, CHANNEL_COUNT);

  static uint16_t previousMask = 0;
  const uint16_t currentMask = rfp605PressedMask();
  const uint16_t changedMask = currentMask ^ previousMask;
  for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
    const uint16_t bit = static_cast<uint16_t>(1U << channel);
    if ((changedMask & bit) == 0)
      continue;

    if ((currentMask & bit) == 0) {
      Serial.printf("[RFP605] RELEASED C%u | pressed mask=0x%04X\n",
                    static_cast<unsigned>(channel),
                    static_cast<unsigned>(currentMask));
      continue;
    }

    const Rfp605Sample *pressedSample = nullptr;
    for (size_t i = 0; i < count; ++i) {
      if (samples[i].channel == channel) {
        pressedSample = &samples[i];
        break;
      }
    }
    if (pressedSample != nullptr) {
      Serial.printf(
          "[RFP605] PRESSED C%u | raw=%u baseline=%u delta=%u "
          "voltage=%u mV | mask=0x%04X\n",
          static_cast<unsigned>(channel),
          static_cast<unsigned>(pressedSample->pressureRaw),
          static_cast<unsigned>(pressedSample->baselineRaw),
          static_cast<unsigned>(pressedSample->deviationRaw),
          static_cast<unsigned>(pressedSample->voltageMv),
          static_cast<unsigned>(currentMask));
    } else {
      Serial.printf("[RFP605] PRESSED C%u | pressed mask=0x%04X\n",
                    static_cast<unsigned>(channel),
                    static_cast<unsigned>(currentMask));
    }
  }
  previousMask = currentMask;
}
#endif // standalone sensor sandbox

#endif // RFP605_SANDBOX || RFP605_GATEWAY_INTEGRATION
```

### 6.16 ไฟล์ `src/dataset_page.h` — หน้าเว็บ HTML/JS ที่ฝังอยู่ในบอร์ด ESP32 Gateway (Port 80 HTTP Dataset Server) (452 บรรทัด)
```cpp
#pragma once

#include <Arduino.h>

static const char DATASET_PAGE[] PROGMEM = R"PLANKOHTML(
<!doctype html>
<html lang="th">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>PlanKO IMU Dataset Recorder</title>
  <style>
    :root{color-scheme:dark;--bg:#0b1219;--panel:#14222c;--line:#2b414e;--text:#edf5f8;--muted:#a9bbc4;--mint:#74f0c2;--red:#ff9292}
    *{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px system-ui,sans-serif}
    main{max-width:1120px;margin:auto;padding:24px 16px 48px}h1,h2,p{margin-top:0}h1{font-size:25px;margin-bottom:6px}h2{font-size:18px;margin-bottom:12px}
    .muted,.hint{color:var(--muted)}.hint{font-size:13px;line-height:1.5}.topline{display:flex;justify-content:space-between;gap:12px;align-items:center;margin-bottom:18px}
    .badge{padding:6px 11px;border-radius:999px;background:#253440;color:var(--muted);font-size:12px}.badge.online{background:#153c32;color:var(--mint)}.card,.node{background:var(--panel);border:1px solid var(--line);border-radius:14px;padding:16px;margin:14px 0}
    .grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:10px}.field{display:grid;gap:6px;color:var(--muted);font-size:12px}.field input,.field select{width:100%;min-height:40px;padding:8px 10px;background:#0d171f;color:var(--text);border:1px solid var(--line);border-radius:8px;font:inherit}
    button{border:1px solid var(--line);border-radius:8px;background:#1c303c;color:var(--text);padding:9px 13px;font:inherit;cursor:pointer}button:disabled{opacity:.45;cursor:default}.primary{background:#17634f;border-color:#26836b}.danger{background:#542a31;border-color:#82424a}.controls{display:flex;flex-wrap:wrap;gap:8px;margin-top:12px;align-items:center}.phase.active{outline:2px solid var(--mint);color:var(--mint)}
    .node-head{display:flex;justify-content:space-between;gap:10px;align-items:center}.node-head strong{font-size:16px}.node-status{font-size:12px;color:var(--muted)}.node-status.online{color:var(--mint)}.node-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(170px,1fr));gap:9px;margin-top:12px}.node-foot{margin-top:10px;color:var(--muted);font-size:12px}.feedback{min-height:20px;margin:10px 0 0;color:var(--muted);font-size:13px}.links{display:grid;gap:7px;margin-top:12px}.links a{color:var(--mint);overflow-wrap:anywhere}.small{font-size:12px}.recording{color:var(--red)}
    .guided{margin-top:14px;padding:12px;border:1px solid var(--line);border-radius:10px;background:#101c27}.guided-steps{display:grid;gap:7px;margin-top:9px}.guided-step{display:grid;grid-template-columns:minmax(130px,1fr) minmax(130px,.6fr);gap:8px;align-items:center}.guided-step select,.guided-step input,.review-row select,.review-row input[type=number]{min-height:36px;width:100%;padding:6px 9px;background:#0d171f;color:var(--text);border:1px solid var(--line);border-radius:8px;font:inherit}.review{display:grid;gap:7px;margin-top:10px}.review-row{display:grid;grid-template-columns:minmax(100px,.5fr) minmax(140px,1fr) auto;gap:8px;align-items:center}.review-row label{display:flex;gap:5px;align-items:center;color:var(--muted);font-size:12px}
    @media(max-width:560px){main{padding:18px 11px 36px}.topline{align-items:flex-start;flex-direction:column}.card,.node{padding:13px}.review-row{grid-template-columns:minmax(80px,.5fr) 1fr}.review-row label{grid-column:1/-1}}
  </style>
</head>
<body>
<main>
  <div class="topline"><div><h1>PlanKO · เก็บข้อมูล IMU</h1><div class="muted">Gateway รับข้อมูลจริงจาก Node ผ่าน BLE และแยก CSV ตาม Node ID</div></div><span id="gatewayStatus" class="badge">กำลังเชื่อมต่อ…</span></div>
  <p id="networkInfo" class="hint"></p>

  <section class="card">
    <h2>ข้อมูลชุดทดลอง</h2>
    <div class="grid">
      <label class="field">ผู้เข้าร่วม<input id="subjectId" value="P01" maxlength="40"></label>
      <label class="field">Session<input id="sessionId" maxlength="60"></label>
      <label class="field">Trial<input id="trialId" value="trial-01" maxlength="60"></label>
      <label class="field">ท่าที่บันทึก<input id="actionLabel" maxlength="60" placeholder="เช่น neck_flexion"></label>
    </div>
    <div class="controls" aria-label="phase label">
      <span class="small muted">Phase ปัจจุบัน:</span>
      <button class="phase active" data-phase="neutral" type="button">ปกติ</button>
      <button class="phase" data-phase="approaching" type="button">กำลังเข้าท่า</button>
      <button class="phase" data-phase="holding" type="button">ค้างในท่า</button>
      <button class="phase" data-phase="returning" type="button">กำลังกลับ</button>
      <span id="phaseLabel" class="small">neutral</span>
    </div>
    <div class="guided">
      <label class="small"><input id="guidedEnabled" type="checkbox" checked> ใช้ตัวช่วยเปลี่ยน phase ตามเวลา</label>
      <div class="guided-steps">
        <label class="guided-step"><select class="guided-phase"><option value="neutral">neutral / ปกติ</option><option value="approaching">approaching / กำลังเข้าท่า</option><option value="holding">holding / ค้างในท่า</option><option value="returning">returning / กำลังกลับ</option></select><span><input class="guided-duration" type="number" min="0.5" max="120" step="0.5" value="3" aria-label="ระยะเวลา neutral วินาที"></span></label>
        <label class="guided-step"><select class="guided-phase"><option value="neutral">neutral / ปกติ</option><option value="approaching" selected>approaching / กำลังเข้าท่า</option><option value="holding">holding / ค้างในท่า</option><option value="returning">returning / กำลังกลับ</option></select><span><input class="guided-duration" type="number" min="0.5" max="120" step="0.5" value="3" aria-label="ระยะเวลา approaching วินาที"></span></label>
        <label class="guided-step"><select class="guided-phase"><option value="neutral">neutral / ปกติ</option><option value="approaching">approaching / กำลังเข้าท่า</option><option value="holding" selected>holding / ค้างในท่า</option><option value="returning">returning / กำลังกลับ</option></select><span><input class="guided-duration" type="number" min="0.5" max="120" step="0.5" value="5" aria-label="ระยะเวลา holding วินาที"></span></label>
        <label class="guided-step"><select class="guided-phase"><option value="neutral">neutral / ปกติ</option><option value="approaching">approaching / กำลังเข้าท่า</option><option value="holding">holding / ค้างในท่า</option><option value="returning" selected>returning / กำลังกลับ</option></select><span><input class="guided-duration" type="number" min="0.5" max="120" step="0.5" value="3" aria-label="ระยะเวลา returning วินาที"></span></label>
        <label class="guided-step"><select class="guided-phase"><option value="neutral" selected>neutral / ปกติ</option><option value="approaching">approaching / กำลังเข้าท่า</option><option value="holding">holding / ค้างในท่า</option><option value="returning">returning / กำลังกลับ</option></select><span><input class="guided-duration" type="number" min="0.5" max="120" step="0.5" value="2" aria-label="ระยะเวลา neutral วินาที"></span></label>
      </div>
      <p class="hint">เริ่มตามลำดับด้านบน; เวลาป้ายกำกับเป็น cue เบื้องต้น ให้ตรวจเวลาของแต่ละช่วงก่อนดาวน์โหลด CSV.</p>
    </div>
    <div class="controls">
      <button id="recordStart" class="primary" type="button">เริ่มบันทึกทุก Node</button>
      <button id="recordStop" class="danger" type="button" disabled>หยุดและสร้าง CSV</button>
      <span id="recordCount" class="small muted">ยังไม่บันทึก</span>
    </div>
    <p class="hint">ข้อมูลจะอยู่ในหน่วยความจำของหน้าเว็บระหว่างบันทึก เมื่อหยุด ระบบสร้างไฟล์ CSV แยกให้แต่ละ Node เพื่อดาวน์โหลด ควรตรวจ label และ phase ก่อนนำไปฝึกโมเดล</p>
    <div id="phaseEditor" class="review"></div>
    <div class="controls"><button id="addPhaseBoundary" type="button" disabled>เพิ่มจุดเปลี่ยน phase</button><button id="downloadCsv" class="primary" type="button" disabled>ยืนยัน label และดาวน์โหลด CSV แยกตาม Node</button></div>
    <div id="feedback" class="feedback" role="status">กำลังอ่านสถานะ Gateway…</div>
    <div id="downloads" class="links"></div>
  </section>

  <h2>Node และตำแหน่งติดตั้ง</h2>
  <p class="hint">ค่าเริ่มต้น Node 1 = คอ, Node 2 = หลังส่วนล่าง; เปลี่ยนชื่อ ตำแหน่ง ส่วนร่างกาย และข้างได้ แล้วกดบันทึก</p>
  <div id="nodes"></div>
</main>
<script>
(() => {
  const $ = id => document.getElementById(id);
  const SITE_OPTIONS = ["upper_back","lumbar","wrist"];
  const SITE_LABELS = {upper_back:"หลังส่วนบน",lumbar:"หลังส่วนล่าง",wrist:"ข้อมือ"};
  const BODY_OPTIONS = ["back","wrist"];
  const BODY_LABELS = {back:"หลัง",wrist:"ข้อมือ"};
  const PHASE_LABELS = {neutral:"neutral",approaching:"approaching",holding:"holding",returning:"returning"};
  const cursors = {};
  const nodeCache = {};
  const recorders = {};
  const downloadUrls = [];
  let currentPhase = "neutral";
  let phaseEvents = [];
  let guidedTimer = 0;
  let guidedPlan = [];
  let recording = false;
  let pollBusy = false;
  let lastPollAt = 0;

  function escCsv(value) {
    const s = value === null || value === undefined ? "" : String(value);
    return /[",\r\n]/.test(s) ? '"' + s.replace(/"/g, '""') + '"' : s;
  }
  function setFeedback(message, isError) {
    $("feedback").textContent = message;
    $("feedback").style.color = isError ? "var(--red)" : "var(--muted)";
  }
  function setNodeConfigControlsDisabled(disabled) {
    document.querySelectorAll(".node-name,.node-site,.node-body,.node-side,.save-config")
      .forEach(control => control.disabled = disabled);
  }
  function optionsHtml(values, labels) {
    return values.map(v => `<option value="${v}">${labels[v] || v}</option>`).join("");
  }
  function makeNodeCard(node) {
    const card = document.createElement("article");
    card.className = "node";
    card.id = `node-${node.nodeId}`;
    card.innerHTML = `<div class="node-head"><strong></strong><span class="node-status">รอ Node</span></div>
      <div class="node-grid">
        <label class="field">ชื่อที่แสดง<input class="node-name" maxlength="30"></label>
        <label class="field">ตำแหน่งติด IMU<select class="node-site">${optionsHtml(SITE_OPTIONS,SITE_LABELS)}</select></label>
        <label class="field">ส่วนร่างกาย<select class="node-body">${optionsHtml(BODY_OPTIONS,BODY_LABELS)}</select></label>
        <label class="field">ข้าง<select class="node-side"><option value="unknown">ไม่ระบุ</option><option value="left">ซ้าย</option><option value="right">ขวา</option></select></label>
      </div><div class="controls"><button class="save-config" type="button">บันทึกตำแหน่ง</button><span class="small muted config-status"></span></div>
      <div class="node-battery small muted"></div><div class="node-foot"></div>`;
    card.querySelector(".node-head strong").textContent = `Node ${node.nodeId}`;
    card.querySelector(".node-name").value = node.name || `Node ${node.nodeId}`;
    card.querySelector(".node-site").value = node.mount_site || "other";
    card.querySelector(".node-body").value = node.body_part || "other";
    card.querySelector(".node-side").value = node.side || "unknown";
    card.querySelector(".save-config").addEventListener("click", () => saveNodeConfig(node.nodeId));
    $("nodes").appendChild(card);
    return card;
  }
  function updateNodeCard(node) {
    nodeCache[node.nodeId] = node;
    let card = $(`node-${node.nodeId}`);
    if (!card) card = makeNodeCard(node);
    const status = card.querySelector(".node-status");
    status.textContent = node.connected ? (node.hasData ? "BLE connected · รับข้อมูล" : "BLE connected · รอข้อมูล IMU") : "ออฟไลน์";
    status.classList.toggle("online", !!node.connected && !!node.hasData);
    card.querySelector(".node-battery").textContent = node.battery_valid === true &&
      Number.isFinite(node.battery_percent) && Number.isFinite(node.battery_mv)
      ? `แบต ≈ ${node.battery_percent}% · ${(node.battery_mv / 1000).toFixed(2)} V`
      : "แบต: — (ยังไม่มีข้อมูลปัจจุบัน)";
    card.querySelector(".node-foot").textContent = `UID: ${node.sensor_uid || "ยังไม่พบ"} · Seq: ${node.latest_seq || 0} · ตกหล่น: ${node.total_dropped || 0}${recording && recorders[node.nodeId] ? ` · บันทึก ${recorders[node.nodeId].rows.length} samples` : ""}`;
  }
  async function refreshNodeList() {
    const response = await fetch("/api/nodes", {cache:"no-store"});
    if (!response.ok) throw new Error(`Gateway HTTP ${response.status}`);
    const data = await response.json();
    (data.nodes || []).forEach(updateNodeCard);
    $("networkInfo").textContent = `Gateway: ${data.ip || location.host} · BLE nodes: ${data.connectedNodes || 0}/4 · MQTT: ${data.mqtt ? "เชื่อมต่อ" : "ยังไม่เชื่อมต่อ"}`;
    $("gatewayStatus").textContent = "Gateway online";
    $("gatewayStatus").classList.add("online");
  }
  async function saveNodeConfig(nodeId) {
    const card = $(`node-${nodeId}`);
    const button = card.querySelector(".save-config");
    const status = card.querySelector(".config-status");
    button.disabled = true;
    status.textContent = "กำลังบันทึก…";
    try {
      const body = new URLSearchParams({
        node_id:String(nodeId), name:card.querySelector(".node-name").value.trim() || `Node ${nodeId}`,
        mount_site:card.querySelector(".node-site").value, body_part:card.querySelector(".node-body").value,
        side:card.querySelector(".node-side").value
      });
      const response = await fetch("/api/node-config", {method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body});
      const result = await response.json();
      if (!response.ok) throw new Error(result.message || `HTTP ${response.status}`);
      status.textContent = "บันทึกแล้ว";
      await refreshNodeList();
    } catch (error) {
      status.textContent = `บันทึกไม่สำเร็จ: ${error.message}`;
    } finally { button.disabled = recording; }
  }
  function isSeqAfter(seq, after) {
    const delta = (Number(seq) - Number(after)) & 0xffff;
    return delta > 0 && delta < 0x8000;
  }
  function updatePhase(phase, source = "manual_button") {
    currentPhase = phase;
    $("phaseLabel").textContent = phase;
    document.querySelectorAll(".phase").forEach(button => button.classList.toggle("active", button.dataset.phase === phase));
    if (recording) {
      const elapsedMs = Math.max(0, Date.now() - window.datasetMetadata.started);
      const previous = phaseEvents[phaseEvents.length - 1];
      if (!previous || previous.phase !== phase || elapsedMs !== previous.elapsedMs) {
        phaseEvents.push({elapsedMs, phase, source, reviewed:false});
      }
    }
  }
  function setCaptureControlsDisabled(disabled, guided = false) {
    ["subjectId", "sessionId", "trialId", "actionLabel", "guidedEnabled"].forEach(id => $(id).disabled = disabled);
    document.querySelectorAll(".guided-phase,.guided-duration").forEach(control => control.disabled = disabled);
    document.querySelectorAll(".phase").forEach(button => button.disabled = disabled && guided);
  }
  function readGuidedPlan() {
    const phases = [...document.querySelectorAll(".guided-phase")];
    const durations = [...document.querySelectorAll(".guided-duration")];
    return phases.map((select, index) => ({phase:select.value,durationMs:Math.round(Number(durations[index].value) * 1000)}));
  }
  function runGuidedStep(index) {
    if (!recording) return;
    if (index >= guidedPlan.length) { stopRecording(); return; }
    updatePhase(guidedPlan[index].phase, "guided");
    guidedTimer = window.setTimeout(() => runGuidedStep(index + 1), guidedPlan[index].durationMs);
  }
  function startRecording() {
    const subject = $("subjectId").value.trim();
    const trial = $("trialId").value.trim();
    const action = $("actionLabel").value.trim();
    if (!subject || !trial || !action) { setFeedback("กรอกผู้เข้าร่วม Trial และท่าที่บันทึกก่อนเริ่ม", true); return; }
    const useGuided = $("guidedEnabled").checked;
    const plan = useGuided ? readGuidedPlan() : [];
    if (useGuided && (!plan.length || plan.some(step => !Number.isFinite(step.durationMs) || step.durationMs < 500 || step.durationMs > 120000))) {
      setFeedback("กำหนดเวลาแต่ละ phase ระหว่าง 0.5 ถึง 120 วินาที", true); return;
    }
    const connected = Object.values(nodeCache).filter(node => node.connected);
    if (!connected.length) { setFeedback("ยังไม่มี Node เชื่อมต่อ BLE", true); return; }
    const started = Date.now();
    const session = $("sessionId").value.trim() || new Date(started).toISOString().replace(/[-:.TZ]/g, "").slice(0,14);
    Object.keys(recorders).forEach(key => delete recorders[key]);
    for (const node of connected) {
      const cursor = cursors[node.nodeId];
      const startSeq = cursor && cursor.bootId === Number(node.boot_id) && cursor.afterSeq !== null
        ? cursor.afterSeq : Number(node.latest_seq || 0);
      recorders[node.nodeId] = {rows:[], startedAt:started, startSeq, startBootId:Number(node.boot_id), lastSeq:null, bootId:null, streamEpoch:0, gapPending:false};
    }
    phaseEvents = [];
    guidedPlan = plan;
    $("phaseEditor").replaceChildren();
    $("addPhaseBoundary").disabled = true;
    $("downloadCsv").disabled = true;
    recording = true;
    setNodeConfigControlsDisabled(true);
    setCaptureControlsDisabled(true, useGuided);
    $("recordStart").disabled = true;
    $("recordStop").disabled = false;
    $("recordCount").textContent = `กำลังบันทึก ${connected.length} Node`;
    $("downloads").replaceChildren();
    setFeedback("กำลังบันทึก sample จริงจาก Node ที่เชื่อมต่ออยู่; ถ้า buffer แจ้งว่าข้อมูลตกหล่น CSV จะทำเครื่องหมาย gap_before", false);
    window.datasetMetadata = {subject, session, trial, action, started};
    updatePhase(useGuided ? plan[0].phase : "neutral", useGuided ? "guided" : "manual_button");
    if (useGuided) guidedTimer = window.setTimeout(() => runGuidedStep(1), plan[0].durationMs);
  }
  const CSV_COLUMNS = ["sensor_uid","sensor_id","firmware_sensor_id","node_id","node_name","mount_site","body_part","side","subject_id","session_id","trial_id","action_label","phase_label","phase_annotation_source","phase_annotation_reviewed","phase_elapsed_ms","calibration_id","calibration_method_version","calibration_time_boot_ms","calibration_boot_id","calibrated","relative_angle_valid","relative_orientation_valid","unit_accel","unit_gyro","host_time_iso","host_time_s","host_elapsed_ms","boot_ms","boot_id","seq","stream_epoch","gap_before","raw_accel_x","raw_accel_y","raw_accel_z","raw_gyro_x","raw_gyro_y","raw_gyro_z","accel_x","accel_y","accel_z","gyro_x","gyro_y","gyro_z","corrected_gyro_x","corrected_gyro_y","corrected_gyro_z","roll_deg","pitch_deg","relative_roll_deg","relative_pitch_deg","relative_tilt_deg","yaw_deg","accel_norm_g","gyro_norm_dps","steady","valid","firmware_status"];
  function captureSample(node, sample, hostTimeMs) {
    if (!recording) return;
    if (!recorders[node.nodeId]) recorders[node.nodeId] = {rows:[], startedAt:window.datasetMetadata.started, startSeq:null, startBootId:Number(node.boot_id), lastSeq:null, bootId:null, streamEpoch:0, gapPending:false};
    const rec = recorders[node.nodeId];
    const meta = window.datasetMetadata;
    if (hostTimeMs < meta.started) return;
    const sampleBootId = Number(node.boot_id);
    if (rec.lastSeq === null && rec.startSeq !== null && rec.startBootId === sampleBootId &&
        !isSeqAfter(sample.seq,rec.startSeq)) return;
    if (rec.bootId !== null && rec.bootId !== Number(node.boot_id)) { rec.streamEpoch++; rec.gapPending = true; }
    const startGap = rec.lastSeq === null && rec.startSeq !== null && rec.startBootId === sampleBootId &&
      Number(sample.seq) !== ((rec.startSeq + 1) & 0xffff);
    const bootChangedAtStart = rec.lastSeq === null && rec.startBootId !== null && rec.startBootId !== sampleBootId;
    rec.bootId = sampleBootId;
    const gap = rec.gapPending || startGap || bootChangedAtStart ||
      (rec.lastSeq !== null && Number(sample.seq) !== ((rec.lastSeq + 1) & 0xffff));
    rec.gapPending = false;
    rec.lastSeq = Number(sample.seq);
    rec.rows.push({
      sensor_uid:node.sensor_uid || "", sensor_id:node.sensor_uid || "", firmware_sensor_id:node.sensor_uid || "",
      node_id:node.nodeId, node_name:node.name || `Node ${node.nodeId}`,
      mount_site:node.mount_site || "other", body_part:node.body_part || "other", side:node.side || "unknown",
      subject_id:meta.subject, session_id:meta.session, trial_id:meta.trial, action_label:meta.action,
      phase_label:currentPhase, phase_annotation_source:"manual_button", phase_annotation_reviewed:false,
      phase_elapsed_ms:"", calibration_id:"", calibration_method_version:"",
      calibration_time_boot_ms:"", calibration_boot_id:"", calibrated:sample.calibrated,
      relative_angle_valid:false, relative_orientation_valid:false, unit_accel:"m/s^2", unit_gyro:"rad/s",
      host_time_iso:new Date(hostTimeMs).toISOString(), host_time_s:(hostTimeMs/1000).toFixed(6),
      host_elapsed_ms:Math.max(0,hostTimeMs-meta.started), boot_id:node.boot_id, boot_ms:sample.sample_ms, seq:sample.seq,
      stream_epoch:rec.streamEpoch, gap_before:gap,
      raw_accel_x:sample.raw_ax, raw_accel_y:sample.raw_ay, raw_accel_z:sample.raw_az,
      raw_gyro_x:sample.raw_gx, raw_gyro_y:sample.raw_gy, raw_gyro_z:sample.raw_gz,
      accel_x:sample.ax, accel_y:sample.ay, accel_z:sample.az,
      gyro_x:sample.gx, gyro_y:sample.gy, gyro_z:sample.gz,
      roll_deg:sample.angle_valid ? sample.roll_deg : "", pitch_deg:sample.angle_valid ? sample.pitch_deg : "",
      corrected_gyro_x:"", corrected_gyro_y:"", corrected_gyro_z:"", relative_roll_deg:"", relative_pitch_deg:"",
      relative_tilt_deg:"", yaw_deg:sample.angle_valid ? sample.yaw_deg : "", accel_norm_g:sample.accel_norm_g, gyro_norm_dps:sample.gyro_norm_dps,
      steady:"", valid:sample.valid, firmware_status:sample.status
    });
  }
  function downloadNodeCsv(nodeId, node, rows) {
    const csv = [CSV_COLUMNS.join(",")].concat(rows.map(row => CSV_COLUMNS.map(key => escCsv(row[key])).join(","))).join("\r\n");
    const url = URL.createObjectURL(new Blob(["\uFEFF" + csv], {type:"text/csv;charset=utf-8"}));
    downloadUrls.push(url);
    const link = document.createElement("a");
    const filePart = value => String(value || "").replace(/[^a-z0-9_-]+/gi,"_").replace(/^_+|_+$/g,"") || "data";
    link.href = url; link.download = `${filePart(window.datasetMetadata.trial)}_node${nodeId}_${filePart(node.mount_site || "other")}.csv`;
    link.textContent = `Node ${nodeId} · ${node.name || "IMU"} · ${rows.length.toLocaleString()} samples · ดาวน์โหลด CSV`;
    $("downloads").appendChild(link);
  }
  function stopRecording() {
    if (!recording) return;
    window.clearTimeout(guidedTimer);
    guidedTimer = 0;
    recording = false;
    setNodeConfigControlsDisabled(false);
    setCaptureControlsDisabled(false);
    $("recordStart").disabled = true;
    $("recordStop").disabled = true;
    const total = Object.values(recorders).reduce((sum,rec) => sum + rec.rows.length, 0);
    $("recordCount").textContent = `บันทึก ${total.toLocaleString()} samples; ทบทวน phase ก่อนสร้าง CSV`;
    if (!total) {
      $("recordStart").disabled = false;
      $("phaseEditor").replaceChildren();
      $("addPhaseBoundary").disabled = true;
      $("downloadCsv").disabled = true;
      setFeedback("ไม่มี sample ที่บันทึกได้", true);
      return;
    }
    if (!phaseEvents.length || phaseEvents[0].elapsedMs > 0) phaseEvents.unshift({elapsedMs:0,phase:"neutral",source:"recording_start",reviewed:false});
    renderPhaseReview();
    $("addPhaseBoundary").disabled = false;
    $("downloadCsv").disabled = false;
    setFeedback("ทบทวนเวลาและ phase ของแต่ละช่วง แล้วกดยืนยันเพื่อสร้าง CSV แยกตาม Node", false);
  }
  function renderPhaseReview() {
    const host = $("phaseEditor");
    host.replaceChildren();
    const heading = document.createElement("strong");
    heading.textContent = "ทบทวนจุดเปลี่ยน phase (เวลาเทียบจากเริ่มบันทึก)";
    host.appendChild(heading);
    phaseEvents.forEach((event,index) => {
      const row = document.createElement("div");
      row.className = "review-row";
      row.innerHTML = `<input type="number" min="0" step="0.05" aria-label="เวลาเป็นวินาที"><select aria-label="phase">${optionsHtml(Object.keys(PHASE_LABELS),PHASE_LABELS)}</select><label><input type="checkbox"> review แล้ว</label>`;
      row.querySelector("input[type=number]").value = (event.elapsedMs / 1000).toFixed(2);
      row.querySelector("select").value = event.phase;
      row.querySelector("input[type=checkbox]").checked = !!event.reviewed;
      row.dataset.index = String(index);
      host.appendChild(row);
    });
  }
  function addPhaseBoundary() {
    if (recording) return;
    phaseEvents = readPhaseReviewEdits();
    let latestSample = 0;
    Object.values(recorders).forEach(rec => rec.rows.forEach(row => {
      latestSample = Math.max(latestSample, Number(row.host_elapsed_ms) || 0);
    }));
    phaseEvents.push({elapsedMs:Math.round(latestSample / 2),phase:"neutral",source:"review",reviewed:false});
    renderPhaseReview();
  }
  function readPhaseReviewEdits() {
    const edits = [...$("phaseEditor").querySelectorAll(".review-row")];
    return edits.map(row => {
      const index = Number(row.dataset.index);
      const old = phaseEvents[index] || {};
      const phase = row.querySelector("select").value;
      return {
        elapsedMs:Math.max(0, Math.round(Number(row.querySelector("input[type=number]").value || 0) * 1000)),
        phase,
        source:phase !== old.phase ? "review" : (old.source || "guided"),
        reviewed:row.querySelector("input[type=checkbox]").checked
      };
    }).sort((a,b) => a.elapsedMs - b.elapsedMs);
  }
  function applyPhaseReviewAndDownload() {
    if (recording) return;
    phaseEvents = readPhaseReviewEdits();
    if (!phaseEvents.length || phaseEvents[0].elapsedMs > 0) phaseEvents.unshift({elapsedMs:0,phase:"neutral",source:"recording_start",reviewed:false});
    for (const rec of Object.values(recorders)) {
      for (const sample of rec.rows) {
        let boundary = phaseEvents[0];
        for (const event of phaseEvents) {
          if (event.elapsedMs > Number(sample.host_elapsed_ms)) break;
          boundary = event;
        }
        sample.phase_label = boundary.phase;
        sample.phase_annotation_source = boundary.source;
        sample.phase_annotation_reviewed = boundary.reviewed;
        sample.phase_elapsed_ms = Math.max(0, Number(sample.host_elapsed_ms) - boundary.elapsedMs);
      }
    }
    $("downloads").replaceChildren();
    let total = 0, files = 0;
    for (const key of Object.keys(recorders)) {
      const id = Number(key), rec = recorders[id], node = nodeCache[id];
      total += rec.rows.length;
      if (rec.rows.length) { downloadNodeCsv(id, node || {nodeId:id}, rec.rows); files++; }
    }
    $("recordStart").disabled = false;
    $("downloadCsv").disabled = true;
    $("addPhaseBoundary").disabled = true;
    $("recordCount").textContent = `${total.toLocaleString()} samples · ${files} CSV`;
    setFeedback(`สร้าง CSV แยก ${files} Node แล้ว; ตรวจ checkbox เพื่อระบุช่วงที่ผ่านการ review`, false);
    for (const url of downloadUrls.splice(0)) setTimeout(() => URL.revokeObjectURL(url), 60000);
  }
  async function pollSamples() {
    if (pollBusy) return;
    pollBusy = true;
    try {
      const url = new URL("/samples", location.href);
      for (let id=1; id<=4; id++) {
        const cursor = cursors[id];
        if (cursor && cursor.afterSeq !== null) { url.searchParams.set(`n${id}_boot_id`, String(cursor.bootId)); url.searchParams.set(`n${id}_after_seq`, String(cursor.afterSeq)); }
      }
      const response = await fetch(url, {cache:"no-store"});
      if (!response.ok) throw new Error(`Gateway /samples HTTP ${response.status}`);
      const payload = await response.json();
      const receivedAt = Date.now();
      for (const node of payload.nodes || []) {
        updateNodeCard(node);
        const id = Number(node.nodeId);
        let cursor = cursors[id];
        const newSession = !cursor || cursor.bootId !== Number(node.boot_id);
        if (newSession) cursor = cursors[id] = {bootId:Number(node.boot_id),afterSeq:null};
        const fresh = newSession || cursor.afterSeq === null
          ? (node.samples || [])
          : (node.samples || []).filter(sample => isSeqAfter(sample.seq,cursor.afterSeq));
        const newestMs = fresh.length ? Number(fresh[fresh.length-1].sample_ms) : 0;
        if (node.dropped && recording) {
          if (!recorders[id]) recorders[id] = {rows:[], startedAt:window.datasetMetadata.started, startSeq:null, startBootId:Number(node.boot_id), lastSeq:null, bootId:null, streamEpoch:0, gapPending:false};
          recorders[id].gapPending = true;
        }
        for (const sample of fresh) {
          const ageMs = (newestMs - Number(sample.sample_ms) + 4294967296) % 4294967296;
          const sampleHostMs = receivedAt - ageMs;
          captureSample(node,sample,sampleHostMs);
          cursor.afterSeq = Number(sample.seq);
        }
      }
      if (recording) {
        const count = Object.values(recorders).reduce((sum,rec) => sum+rec.rows.length,0);
        $("recordCount").textContent = `${count.toLocaleString()} samples · กำลังบันทึก`;
      }
      const elapsed = Date.now()-lastPollAt; lastPollAt=Date.now();
      if (elapsed > 800 && recording) setFeedback("การตอบสนองช้า; ตรวจ gap_before ใน CSV หลังบันทึก", true);
      $("gatewayStatus").textContent = "Gateway online";
      $("gatewayStatus").classList.add("online");
    } catch(error) {
      $("gatewayStatus").textContent = "Gateway offline";
      $("gatewayStatus").classList.remove("online");
      setFeedback(`อ่านข้อมูลไม่สำเร็จ: ${error.message}`, true);
    } finally { pollBusy=false; }
  }
  document.querySelectorAll(".phase").forEach(button => button.addEventListener("click", () => updatePhase(button.dataset.phase)));
  $("recordStart").addEventListener("click",startRecording);
  $("recordStop").addEventListener("click",stopRecording);
  $("addPhaseBoundary").addEventListener("click",addPhaseBoundary);
  $("downloadCsv").addEventListener("click",applyPhaseReviewAndDownload);
  $("sessionId").value = "session-" + new Date().toISOString().replace(/[-:.TZ]/g,"").slice(0,14);
  refreshNodeList().catch(error => setFeedback(`อ่านสถานะ Node ไม่สำเร็จ: ${error.message}`,true));
  setInterval(pollSamples,100);
  window.addEventListener("beforeunload",() => downloadUrls.forEach(URL.revokeObjectURL));
})();
</script>
</body>
</html>
)PLANKOHTML";
```

### 6.17 ไฟล์ `src/main.cpp` — โค้ดหลัก ESP32 ครบทุกบทบาท (ROLE_NODE 1-4 + MAX30102 + Battery ADC และ ROLE_GATEWAY + BLE Central + Wi-Fi + MQTT + Pad + RFP) (2289 บรรทัด)
```cpp
/**
 * ==============================================================================
 * Project: PlanKO - Smart Plank Training System
 * Hardware: ESP32-C3 gateway / classic ESP32 RFP605 gateway sandbox
 *
 * Supports 2 Unified Roles via PlatformIO build flags:
 *   1. ROLE_GATEWAY:
 *      - BLE Central (connects to up to 4 IMU Nodes via BLE GATT)
 *      - Wi-Fi Station + local Dataset HTTP Server on Port 80
 *      - 16-pad Smart Mat Switch Inputs + Debounce + Serial Simulator
 *      - (Optional) MAX30102 Pulse Oximeter
 *      - Broadcasts consolidated IMU + Touch events to Expo App
 *
 *   2. ROLE_NODE:
 *      - BLE Peripheral (Advertises as PlanKO_IMU_1 .. PlanKO_IMU_4)
 *      - Reads MPU-6050 6-DOF IMU over I2C (SDA=8, SCL=9)
 *      - Computes Pitch, Roll, Yaw via Complementary Filter
 *      - Transmits telemetry and raw IMU data in one BLE packet per sample
 * ==============================================================================
 */

#include "common_types.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <stdlib.h>

#if defined(ROLE_NODE)
// ==============================================================================
// ------------------------------ ROLE: IMU NODE
// --------------------------------
// ==============================================================================
#include "mpu6050_reader.h"
#include <NimBLEDevice.h>

#ifndef NODE_ID
#define NODE_ID 1
#endif

#ifndef ENABLE_NODE1_MAX30102
#define ENABLE_NODE1_MAX30102 1
#endif

#if (NODE_ID == 1) && ENABLE_NODE1_MAX30102
#include <MAX30105.h>
#include <heartRate.h>
MAX30105 max30102;
bool max30102Detected = false;
uint32_t currentRed = 0, currentIr = 0;
float currentBpm = 0.0f, beatAvg = 0.0f;
uint8_t currentSpo2 = 0;
bool currentFinger = false;
const byte RATE_SIZE = 4;
byte rates[RATE_SIZE] = {}, rateSpot = 0;
unsigned long lastBeat = 0, lastBioNotifyTime = 0;
const unsigned long BIO_NOTIFY_INTERVAL_MS = 250;
bool initMax30102() {
  if (!max30102.begin(Wire, I2C_SPEED_STANDARD))
    return false;
  max30102.setup(0x1F, 4, 2, 100, 411, 4096);
  max30102.setPulseAmplitudeGreen(0);
  return true;
}
#endif

#ifndef I2C_SDA_PIN
#define I2C_SDA_PIN 8
#endif

#ifndef I2C_SCL_PIN
#define I2C_SCL_PIN 9
#endif

// ESP32-C3 Super Mini: OUT+ -- 10k -- GPIO4 -- 10k -- OUT-/GND.
#ifndef BATTERY_ADC_PIN
#define BATTERY_ADC_PIN 4
#endif
constexpr unsigned long BATTERY_INTERVAL_MS = 5000;
constexpr uint16_t BATTERY_EMPTY_MV = 3300;
constexpr uint16_t BATTERY_FULL_MV = 4200;
unsigned long lastBatterySampleTime = 0;
bool batteryWasSubscribed = false;

BatteryPacket readNodeBattery() {
  uint32_t totalMv = 0;
  (void)analogReadMilliVolts(BATTERY_ADC_PIN); // Discard first conversion.
  for (uint8_t i = 0; i < 16; ++i)
    totalMv += analogReadMilliVolts(BATTERY_ADC_PIN);
  BatteryPacket packet = {};
  packet.nodeId = NODE_ID;
  packet.millivolts = static_cast<uint16_t>((totalMv * 2 + 8) / 16);
  packet.marker = BATTERY_PACKET_MARKER;
  // Coarse voltage-based estimate for a 1S 4.2V Li-ion/LiPo cell, not a
  // fuel-gauge measurement. Charging/load and regulator dropout affect it.
  if (packet.millivolts < 2500 || packet.millivolts > 4500)
    packet.percent = BATTERY_PERCENT_UNKNOWN;
  else if (packet.millivolts <= BATTERY_EMPTY_MV)
    packet.percent = 0;
  else if (packet.millivolts >= BATTERY_FULL_MV)
    packet.percent = 100;
  else
    packet.percent =
        static_cast<uint8_t>((packet.millivolts - BATTERY_EMPTY_MV) * 100UL /
                             (BATTERY_FULL_MV - BATTERY_EMPTY_MV));
  return packet;
}

// Sampling frequency: 20Hz (50ms) - Optimal for BLE & Wi-Fi coexistence and
// queue stability
const unsigned long SAMPLE_INTERVAL_MS = 50;

Mpu6050Reader imu;
NimBLEServer *pServer = nullptr;
NimBLECharacteristic *pImuCharacteristic = nullptr;
bool bleConnected = false;
uint16_t bleConnectionHandle = 0xFFFF;
uint16_t packetSequence = 0;
unsigned long lastSampleTime = 0;
unsigned long lastSerialPrintTime = 0;

#define NODE_LOG(x)                                                            \
  do {                                                                         \
    if (Serial && Serial.availableForWrite() > 32)                             \
      Serial.print(x);                                                         \
  } while (0)
#define NODE_LOGLN(x)                                                          \
  do {                                                                         \
    if (Serial && Serial.availableForWrite() > 32)                             \
      Serial.println(x);                                                       \
  } while (0)
#define NODE_LOGF(...)                                                         \
  do {                                                                         \
    if (Serial && Serial.availableForWrite() > 32)                             \
      Serial.printf(__VA_ARGS__);                                              \
  } while (0)

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *pServer) override {
    bleConnected = true;
    NODE_LOGLN("🔗 [BLE] Gateway Connected!");
  }
  void onConnect(NimBLEServer *pServer, ble_gap_conn_desc *desc) override {
    bleConnected = true;
    bleConnectionHandle = desc->conn_handle;
    NODE_LOGF("🔗 [BLE] Gateway Connected! conn=%u\n", desc->conn_handle);
  }
  void onDisconnect(NimBLEServer *pServer) override {
    bleConnected = false;
    bleConnectionHandle = 0xFFFF;
    NODE_LOGLN("🔌 [BLE] Gateway Disconnected! Restarting Adv...");
    NimBLEDevice::startAdvertising();
  }
  void onDisconnect(NimBLEServer *pServer, ble_gap_conn_desc *desc) override {
    bleConnected = false;
    bleConnectionHandle = 0xFFFF;
    NODE_LOGF("🔌 [BLE] Gateway Disconnected! conn=%u. Restarting Adv...\n",
              desc->conn_handle);
    NimBLEDevice::startAdvertising();
  }
};

class CharacteristicCallbacks : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic *pChar, ble_gap_conn_desc *desc,
                   uint16_t subVal) override {
    NODE_LOGF(
        "🔔 [NODE-BLE] Client %d subscription status changed: subVal = %u\n",
        desc->conn_handle, subVal);
  }
};

void printNodeBanner() {
  NODE_LOGLN(
      "\n****************************************************************");
  NODE_LOGF("🤖 [PlanKO IMU Node #%d] Firmware Started!\n", NODE_ID);
  NODE_LOGF("📡 BLE Advertising Name: PlanKO_IMU_%d\n", NODE_ID);
  NODE_LOGF("🔌 I2C Pins: SDA = GPIO %d, SCL = GPIO %d\n", I2C_SDA_PIN,
            I2C_SCL_PIN);
  NODE_LOGLN("💡 Type 'CAL' in Serial Monitor to re-calibrate IMU sensor");
  NODE_LOGLN(
      "****************************************************************\n");
}

void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);
  delay(100);

  printNodeBanner();

  pinMode(BATTERY_ADC_PIN, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);

  // 1. Initialize MPU-6050 IMU
  NODE_LOGLN("🔍 [IMU] Initializing MPU-6050...");
  if (!imu.begin(I2C_SDA_PIN, I2C_SCL_PIN)) {
    NODE_LOGLN("❌ [IMU] MPU-6050 not detected. Retrying in background...");
  }

#if (NODE_ID == 1) && ENABLE_NODE1_MAX30102
  // 1.1 Initialize MAX30102 Pulse Oximeter on I2C bus (SDA=8, SCL=9)
  max30102Detected = initMax30102();
#endif

  // 2. Initialize NimBLE Peripheral
  char bleName[20];
  snprintf(bleName, sizeof(bleName), "PlanKO_IMU_%d", NODE_ID);
  NimBLEDevice::init(bleName);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9); // Maximum TX power
  const int mtuResult = NimBLEDevice::setMTU(64);
  NODE_LOGF("📦 [BLE] Preferred ATT MTU 64: %s\n",
            mtuResult == 0 ? "ready" : "failed");

  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  NimBLEService *pService = pServer->createService(PLANKO_SERVICE_UUID);
  pImuCharacteristic = pService->createCharacteristic(
      PLANKO_CHARACTERISTIC_UUID,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  pImuCharacteristic->setCallbacks(new CharacteristicCallbacks());
  pService->start();
  NODE_LOGF("🔍 [NODE-BLE] pImuChar handle: %d\n",
            pImuCharacteristic->getHandle());

  NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
  NimBLEAdvertisementData advData;
  advData.setFlags(0x06); // General Discovery, BR/EDR not supported
  advData.setName(bleName);
  pAdvertising->setAdvertisementData(advData);

  NimBLEAdvertisementData scanResponseData;
  scanResponseData.setCompleteServices(NimBLEUUID(PLANKO_SERVICE_UUID));
  pAdvertising->setScanResponseData(scanResponseData);

  pAdvertising->setMinInterval(32);
  pAdvertising->setMaxInterval(64);
  pAdvertising->start();

  NODE_LOGF("🚀 [BLE] Advertising as '%s' [%s] - Waiting for Gateway...\n",
            bleName, NimBLEDevice::getAddress().toString().c_str());
}

void loop() {
  unsigned long now = millis();

  // Battery telemetry is independent of IMU detection and sequence numbers.
  const bool batterySubscribed = pImuCharacteristic != nullptr &&
                                 pImuCharacteristic->getSubscribedCount() > 0;
  if ((batterySubscribed && !batteryWasSubscribed) ||
      now - lastBatterySampleTime >= BATTERY_INTERVAL_MS) {
    lastBatterySampleTime = now;
    BatteryPacket battery = readNodeBattery();
    if (batterySubscribed)
      pImuCharacteristic->notify(reinterpret_cast<uint8_t *>(&battery),
                                 sizeof(battery));
    if (battery.percent == BATTERY_PERCENT_UNKNOWN) {
      NODE_LOGF("[BATTERY] Node #%d GPIO%d: invalid reading (%u mV)\n", NODE_ID,
                BATTERY_ADC_PIN, static_cast<unsigned>(battery.millivolts));
    } else {
      NODE_LOGF("[BATTERY] Node #%d GPIO%d: %u mV, ~%u%%\n", NODE_ID,
                BATTERY_ADC_PIN, static_cast<unsigned>(battery.millivolts),
                static_cast<unsigned>(battery.percent));
    }
  }
  batteryWasSubscribed = batterySubscribed;

  // Check for Serial commands (e.g. CAL) non-blockingly
  if (Serial.available()) {
    static String rxCmd = "";
    while (Serial.available()) {
      char c = (char)Serial.read();
      if (c == '\n' || c == '\r') {
        rxCmd.trim();
        rxCmd.toUpperCase();
        if (rxCmd == "CAL") {
          imu.calibrate();
        }
        rxCmd = "";
      } else {
        rxCmd += c;
      }
    }
  }

  // Ensure advertising stays active if not connected
  static unsigned long lastAdvCheck = 0;
  if (!bleConnected && (now - lastAdvCheck >= 2000)) {
    lastAdvCheck = now;
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    if (adv && !adv->isAdvertising()) {
      adv->start();
    }
  }

  // Read IMU and send BLE Notification
  if (now - lastSampleTime >= SAMPLE_INTERVAL_MS) {
    lastSampleTime = now;
    const bool sampleReady =
        imu.isDetected ? imu.update() : (imu.update() && imu.isDetected);
    if (sampleReady && pImuCharacteristic != nullptr &&
        (bleConnected || pImuCharacteristic->getSubscribedCount() > 0)) {
      const uint16_t sequence = ++packetSequence;
      const uint32_t sampleTime = millis();

      const uint16_t peerMtu =
          (pServer != nullptr && bleConnectionHandle != 0xFFFF)
              ? pServer->getPeerMTU(bleConnectionHandle)
              : 0;
      if (peerMtu >= sizeof(CombinedImuPacket) + 3) {
        CombinedImuPacket pkt = {};
        pkt.nodeId = NODE_ID;
        pkt.sequence = sequence;
        pkt.timestamp = sampleTime;
        pkt.pitch = (int16_t)(imu.pitch * 100.0f);
        pkt.roll = (int16_t)(imu.roll * 100.0f);
        pkt.yaw = (int16_t)(imu.yaw * 100.0f);
        pkt.ax = (int16_t)(imu.ax * 100.0f);
        pkt.ay = (int16_t)(imu.ay * 100.0f);
        pkt.az = (int16_t)(imu.az * 100.0f);
        pkt.rawAx = imu.rawAx;
        pkt.rawAy = imu.rawAy;
        pkt.rawAz = imu.rawAz;
        pkt.rawGx = imu.rawGx;
        pkt.rawGy = imu.rawGy;
        pkt.rawGz = imu.rawGz;
        pkt.status = imu.isCalibrated ? 1 : 0;
        pkt.marker = COMBINED_IMU_PACKET_MARKER;
        pImuCharacteristic->notify((uint8_t *)&pkt, sizeof(pkt));
      } else {
        // Retain compatibility with older gateways that negotiate only MTU 23.
        ImuPacket telemetryPkt = {};
        telemetryPkt.nodeId = NODE_ID;
        telemetryPkt.sequence = sequence;
        telemetryPkt.timestamp = sampleTime;
        telemetryPkt.pitch = (int16_t)(imu.pitch * 100.0f);
        telemetryPkt.roll = (int16_t)(imu.roll * 100.0f);
        telemetryPkt.yaw = (int16_t)(imu.yaw * 100.0f);
        telemetryPkt.ax = (int16_t)(imu.ax * 100.0f);
        telemetryPkt.ay = (int16_t)(imu.ay * 100.0f);
        telemetryPkt.az = (int16_t)(imu.az * 100.0f);
        telemetryPkt.status = imu.isCalibrated ? 1 : 0;
        pImuCharacteristic->notify((uint8_t *)&telemetryPkt,
                                   sizeof(telemetryPkt));

        RawImuPacket rawPkt = {};
        rawPkt.nodeId = NODE_ID;
        rawPkt.sequence = sequence;
        rawPkt.timestamp = sampleTime;
        rawPkt.ax = imu.rawAx;
        rawPkt.ay = imu.rawAy;
        rawPkt.az = imu.rawAz;
        rawPkt.gx = imu.rawGx;
        rawPkt.gy = imu.rawGy;
        rawPkt.gz = imu.rawGz;
        rawPkt.marker = RAW_IMU_PACKET_MARKER | (imu.isCalibrated ? 1 : 0);
        pImuCharacteristic->notify((uint8_t *)&rawPkt, sizeof(rawPkt));
      }
    }
  }

#if (NODE_ID == 1) && ENABLE_NODE1_MAX30102
  // Background retry if MAX30102 was not ready at boot
  static unsigned long lastMaxRetryTime = 0;
  if (!max30102Detected && (now - lastMaxRetryTime >= 2000)) {
    lastMaxRetryTime = now;
    max30102Detected = initMax30102();
  }

  // Read MAX30102 sensor and process pulse oximetry
  if (max30102Detected) {
    max30102.check();
    while (max30102.available()) {
      currentRed = max30102.getFIFORed();
      currentIr = max30102.getFIFOIR();
      max30102.nextSample();
    }

    // Direct read fallback if FIFO empty
    if (currentIr == 0) {
      currentIr = max30102.getIR();
      currentRed = max30102.getRed();
    }

    // Finger & Heartbeat detection logic
    bool noRecentBeat = (lastBeat == 0 || (now - lastBeat > 2500));

    // If IR is low or no heartbeat detected recently, decay values to 0
    if (currentIr < 50000 || noRecentBeat) {
      currentBpm = 0.0f;
      currentSpo2 = 0;
      if (now - lastBeat > 3000) {
        currentFinger = false;
        for (byte x = 0; x < RATE_SIZE; x++)
          rates[x] = 0;
        rateSpot = 0;
        beatAvg = 0.0f;
      }
    }

    if (currentIr >= 50000) {
      if (checkForBeat(currentIr)) {
        long delta = now - lastBeat;
        lastBeat = now;
        if (delta > 270 && delta < 2000) { // Valid HR: 30-220 BPM
          float beatsPerMinute = 60.0f / (delta / 1000.0f);
          rates[rateSpot++] = (byte)round(beatsPerMinute);
          rateSpot %= RATE_SIZE;

          int sum = 0;
          byte count = 0;
          for (byte x = 0; x < RATE_SIZE; x++) {
            if (rates[x] > 0) {
              sum += rates[x];
              count++;
            }
          }

          // Require at least 2 consecutive rhythmic beats to confirm a real
          // human finger
          if (count >= 2) {
            beatAvg = (float)sum / count;
            currentBpm = beatAvg;
            currentFinger = true;

            if (currentIr > 0 && currentRed > 0) {
              float ratio = (float)currentRed / (float)currentIr;
              int estimatedSpo2 = (int)(110.0f - 25.0f * ratio);
              if (estimatedSpo2 > 100)
                estimatedSpo2 = 100;
              if (estimatedSpo2 < 85)
                estimatedSpo2 = 85;
              currentSpo2 = (uint8_t)estimatedSpo2;
            }
          }
        }
      }
    }

    // Transmit BioPacket via BLE Notifications at 4Hz (every 250ms)
    if (now - lastBioNotifyTime >= BIO_NOTIFY_INTERVAL_MS) {
      lastBioNotifyTime = now;
      if (pImuCharacteristic != nullptr &&
          (bleConnected || pImuCharacteristic->getSubscribedCount() > 0)) {
        BioPacket bioPkt;
        bioPkt.nodeId = NODE_ID;
        bioPkt.sequence = packetSequence;
        bioPkt.timestamp = now;
        bioPkt.bpm = (uint16_t)round(currentBpm * 10.0f);
        bioPkt.spo2 = currentSpo2;
        bioPkt.finger = currentFinger ? 1 : 0;
        bioPkt.red = currentRed;
        bioPkt.ir = currentIr;
        bioPkt.marker = BIO_PACKET_MARKER;
        pImuCharacteristic->notify((uint8_t *)&bioPkt, sizeof(bioPkt));
      }
    }
  }
#endif

  // Print telemetry to Serial once every 2 seconds
  if (now - lastSerialPrintTime >= 2000) {
    lastSerialPrintTime = now;
    if (Serial && Serial.availableForWrite() > 64) {
      size_t subs =
          pImuCharacteristic ? pImuCharacteristic->getSubscribedCount() : 0;
      uint16_t hdl = pImuCharacteristic ? pImuCharacteristic->getHandle() : 0;
      if (imu.isDetected) {
        Serial.printf("📊 [Node #%d][REAL IMU] Pitch: %5.1f° | Roll: %5.1f° | "
                      "BLE: %s (Subs: %u, hdl: %u) | Seq: %u\n",
                      NODE_ID, imu.pitch, imu.roll,
                      bleConnected ? "CONNECTED" : "ADVERTISING",
                      (unsigned int)subs, (unsigned int)hdl, packetSequence);
      } else {
        Serial.printf("⚠️ [Node #%d] MPU-6050 unavailable; waiting for real "
                      "sensor | BLE: %s\n",
                      NODE_ID, bleConnected ? "CONNECTED" : "ADVERTISING");
      }
#if (NODE_ID == 1) && ENABLE_NODE1_MAX30102
      if (max30102Detected) {
        Serial.printf("❤️ [Node #%d][MAX30102] IR: %u | Red: %u | BPM: %5.1f | "
                      "SpO2: %3u%% | Finger: %s\n",
                      NODE_ID, currentIr, currentRed, currentBpm,
                      (unsigned int)currentSpo2,
                      currentFinger ? "DETECTED" : "NO FINGER");
      } else {
        Serial.printf("⚠️ [Node #%d][MAX30102] Not responding at 0x57 (Check "
                      "SDA=8, SCL=9, 3.3V, GND)\n",
                      NODE_ID);
      }
#endif
    }
  }
}

#elif defined(ROLE_GATEWAY)
// ==============================================================================
// ----------------------------- ROLE: GATEWAY
// ----------------------------------
// ==============================================================================
#include "dataset_page.h"
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <WiFi.h>
#include <atomic>
#include <esp_random.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

#if defined(RFP605_GATEWAY_INTEGRATION)
#include "rfp605_sensor.h"
#endif

// Wi-Fi Configuration
const char *WIFI_SSID = "APISIT164 @2.4G";
const char *WIFI_PASS = "09E2898E";

// Node -> BLE -> Gateway -> local Docker Mosquitto -> subscribers.
// Update this destination if the PC's DHCP-assigned Wi-Fi address changes.
const char *MQTT_BROKER = "192.168.1.160";
const uint16_t MQTT_PORT = 1883;
const char *TOPIC_IMU = "planko/imu";
const char *TOPIC_PAD = "planko/pad";
const char *TOPIC_STATUS = "planko/status";
const char *TOPIC_PRESSURE = "planko/pressure";
const char *TOPIC_BIO = "planko/bio";
char mqttClientId[32] = "PlanKO_Gateway_ESP32";
char mqttCommandTopic[80] = {};
char pendingMqttPadCommand[8] = {};
bool pendingMqttPadEvent = false;

// ==============================================================================
// RFP-602 Thin-Film Pressure Sensor Configuration
// ==============================================================================
#ifndef RFP602_PIN
#define RFP602_PIN 0 // ADC1_CH0 on ESP32-C3
#endif

const int RFP602_PRESS_THRESHOLD =
    300; // ADC threshold (0-4095) for detecting press
int rfp602RawValue = 0;
float rfp602Voltage = 0.0f;
bool rfp602Pressed = false;
bool rfp602LastPressedState = false;
unsigned long lastRfp602SampleTime = 0;
const unsigned long RFP602_SAMPLE_INTERVAL_MS =
    50; // Read ADC at 20Hz (every 50ms)
unsigned long lastRfp602MqttTime = 0;
const unsigned long RFP602_MQTT_INTERVAL_MS =
    250; // 4Hz MQTT stream (every 250ms = 4 packets/sec)

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// DHCP supplies the local address, route and DNS for the current Wi-Fi network.
// MQTT subscribers identify the Gateway by client ID, not by its local IP.

WebServer webServer(80);
Preferences nodeConfigPreferences;

// 16-Pad GPIO Hardware Configuration (ESP32-C3 Safe Pins)
// Note: L1 is set to -1 because GPIO 0 is used by analog RFP-602 pressure
// sensor
struct PadConfig {
  const char *name;
  int8_t pin;
};

// Direct pad GPIO inputs are disabled so the normal Gateway only handles the 3 BLE IMU nodes.
const PadConfig PAD_CONFIGS[] = {
    {"L1", -1}, {"L2", -1}, {"L3", -1}, {"L4", -1}, {"L5", -1}, {"L6", -1},
    {"L7", -1}, {"L8", -1}, {"R1", -1}, {"R2", -1}, {"R3", -1}, {"R4", -1},
    {"R5", -1}, {"R6", -1}, {"R7", -1}, {"R8", -1}};

const size_t TOTAL_PADS = sizeof(PAD_CONFIGS) / sizeof(PAD_CONFIGS[0]);

struct PadSensorState {
  const char *sensorId;
  int8_t pin;
  int lastRawState;
  int debouncedState;
  unsigned long lastDebounceTime;
};

PadSensorState sensors[TOTAL_PADS];
const unsigned long DEBOUNCE_DELAY_MS = 40;

// BLE central supports four node slots; this Gateway session uses Nodes 1-3.
const int MAX_NODES = 4;
const uint8_t EXPECTED_NODE_IDS[] = {1, 2, 3};
const size_t EXPECTED_NODE_COUNT =
    sizeof(EXPECTED_NODE_IDS) / sizeof(EXPECTED_NODE_IDS[0]);
NodeTelemetry nodesTelemetry[MAX_NODES];
NimBLEClient *pNodeClients[MAX_NODES] = {nullptr, nullptr, nullptr, nullptr};
struct PendingBleNode {
  NimBLEAddress address;
  bool pending = false;
  bool connecting = false;
  uint32_t retryAfter = 0;
};
PendingBleNode pendingBleNodes[MAX_NODES];
portMUX_TYPE bleConnectMux = portMUX_INITIALIZER_UNLOCKED;
std::atomic<bool> bleScanRestartRequested{false};

bool isExpectedBleNode(int nodeId) {
  for (size_t i = 0; i < EXPECTED_NODE_COUNT; ++i) {
    if (EXPECTED_NODE_IDS[i] == nodeId)
      return true;
  }
  return false;
}

int connectedExpectedBleNodeCount() {
  int connectedCount = 0;
  for (size_t i = 0; i < EXPECTED_NODE_COUNT; ++i) {
    const int idx = EXPECTED_NODE_IDS[i] - 1;
    if (nodesTelemetry[idx].connected)
      ++connectedCount;
  }
  return connectedCount;
}

const size_t DATASET_HISTORY_CAPACITY = 256;
const size_t DATASET_BATCH_LIMIT = 16;
struct DatasetSample {
  uint32_t timestamp;
  uint16_t sequence;
  int16_t rawAx, rawAy, rawAz;
  int16_t rawGx, rawGy, rawGz;
  int16_t pitch, roll, yaw;
  int16_t ax, ay, az;
  uint8_t status;
  bool telemetryValid;
};

struct NodeDatasetHistory {
  DatasetSample samples[DATASET_HISTORY_CAPACITY];
  size_t nextIndex;
  size_t count;
  uint32_t bootId;
};

NodeDatasetHistory datasetHistory[MAX_NODES] = {};
portMUX_TYPE datasetMux = portMUX_INITIALIZER_UNLOCKED;

void appendBatteryJson(JsonObject node, int idx) {
  portENTER_CRITICAL(&datasetMux);
  const bool received = nodesTelemetry[idx].batteryReceived;
  const uint16_t mv = nodesTelemetry[idx].batteryMillivolts;
  const uint8_t percent = nodesTelemetry[idx].batteryPercent;
  const uint32_t receivedAt = nodesTelemetry[idx].lastBatteryReceivedMillis;
  portEXIT_CRITICAL(&datasetMux);
  const bool packetRecent = received && millis() - receivedAt < 15000;
  const bool valid = packetRecent && nodesTelemetry[idx].connected &&
                     percent <= 100 && mv >= 2500 && mv <= 4500;
  node["battery_received"] = packetRecent;
  node["battery_valid"] = valid;
  node["battery_percent"] = nullptr;
  node["battery_mv"] = nullptr;
  if (packetRecent)
    node["battery_mv"] = mv;
  if (valid) {
    node["battery_percent"] = percent;
  }
}
const size_t MQTT_SAMPLE_BATCH_LIMIT = 4;
uint16_t mqttPublishedSequence[MAX_NODES] = {};
uint32_t mqttPublishedBootId[MAX_NODES] = {};
bool mqttPublishedCursorValid[MAX_NODES] = {};
char nodeSensorUids[MAX_NODES][20] = {};
uint32_t gatewayBootId = 0;

struct NodeConfig {
  char name[32];
  char mountSite[20];
  char bodyPart[20];
  char side[8];
};

NodeConfig nodeConfigs[MAX_NODES] = {
    {"Node 1 - Upper Back", "upper_back", "back", "unknown"},
    {"Node 2 - Lower Back", "lumbar", "back", "unknown"},
    {"Node 3 - Wrist", "wrist", "wrist", "unknown"},
    {"Node 4", "other", "other", "unknown"}};

unsigned long lastMqttStreamTime = 0;
const unsigned long MQTT_STREAM_INTERVAL_MS =
    50; // 20 Hz consolidated stream; one message includes all nodes.
unsigned long lastMqttReconnectAttempt = 0;
unsigned long lastBleScanTime = 0;
const unsigned long STALE_TIMEOUT_MS =
    15000; // Force disconnect if no data for 15s (prevents network jitter from
           // dropping BLE)
bool isScanning = false;

// Forward declaration
void sendPadPressEvent(const char *sensorId);
void gatewayMqttCallback(char *topic, byte *payload, unsigned int length);
void publishImuStream();
void readRfp602Sensor();
void maintainMqttConnection();
void resumeBleScanIfNeeded(bool forceRestart = false);
void handleNodesApi();
void handleNodeConfigApi();
void handleSamplesApi();
void loadNodeConfigs();
void setupDatasetServer();

// BLE Client Callbacks
class GatewayClientCallbacks : public NimBLEClientCallbacks {
public:
  uint8_t nodeId;
  GatewayClientCallbacks(uint8_t id) : nodeId(id) {}

  void onConnect(NimBLEClient *pClient) override {
    const int idx = nodeId - 1;
    String uid = pClient->getPeerAddress().toString().c_str();
    uid.replace(":", "");
    uid.toLowerCase();
    uid.toCharArray(nodeSensorUids[idx], sizeof(nodeSensorUids[idx]));
    portENTER_CRITICAL(&datasetMux);
    nodesTelemetry[idx].batteryReceived = false;
    datasetHistory[idx].nextIndex = 0;
    datasetHistory[idx].count = 0;
    datasetHistory[idx].bootId = esp_random();
    if (datasetHistory[idx].bootId == 0)
      datasetHistory[idx].bootId = 1;
    portEXIT_CRITICAL(&datasetMux);
    Serial.printf("✅ [GATEWAY-BLE] Connected to IMU Node #%d [%s]\n", nodeId,
                  pClient->getPeerAddress().toString().c_str());
    nodesTelemetry[idx].connected = true;
    nodesTelemetry[idx].lastReceivedMillis = millis();
  }

  void onDisconnect(NimBLEClient *pClient) override {
    Serial.printf("❌ [GATEWAY-BLE] Disconnected from IMU Node #%d.\n", nodeId);
    nodesTelemetry[nodeId - 1].connected = false;
    nodesTelemetry[nodeId - 1].lastReceivedMillis = 0;
    nodesTelemetry[nodeId - 1].lastSequence = 0;

    // The NimBLE host owns callback execution. Scanner/client lifecycle stays
    // in loop(); callbacks never delete objects used by a connection attempt.
    bleScanRestartRequested.store(true);
  }
};

// BLE Notification Callback for receiving ImuPacket from Nodes
void notifyCallback(NimBLERemoteCharacteristic *pRemoteChar, uint8_t *pData,
                    size_t length, bool isNotify) {
  // Handle short battery packets before the legacy 20-byte length guard.
  if (length == sizeof(BatteryPacket) &&
      pData[sizeof(BatteryPacket) - 1] == BATTERY_PACKET_MARKER) {
    BatteryPacket battery;
    memcpy(&battery, pData, sizeof(battery));
    if (battery.nodeId < 1 || battery.nodeId > MAX_NODES ||
        (battery.percent > 100 && battery.percent != BATTERY_PERCENT_UNKNOWN))
      return;
    const int idx = battery.nodeId - 1;
    const bool valid = battery.percent <= 100 && battery.millivolts >= 2500 &&
                       battery.millivolts <= 4500;
    portENTER_CRITICAL(&datasetMux);
    nodesTelemetry[idx].batteryMillivolts = battery.millivolts;
    nodesTelemetry[idx].batteryPercent =
        valid ? battery.percent : BATTERY_PERCENT_UNKNOWN;
    nodesTelemetry[idx].batteryReceived = true;
    nodesTelemetry[idx].lastBatteryReceivedMillis = millis();
    portEXIT_CRITICAL(&datasetMux);
    // Do not update IMU freshness or sequence/loss counters for battery data.
    if (valid)
      Serial.printf("[GW-BATTERY] Node #%u: %u mV, ~%u%%\n",
                    static_cast<unsigned>(battery.nodeId),
                    static_cast<unsigned>(battery.millivolts),
                    static_cast<unsigned>(battery.percent));
    return;
  }
  if (length < sizeof(ImuPacket)) {
    Serial.printf("⚠️ [GW-NOTIFY-LEN] Short packet: %u bytes (expected %u)\n",
                  (unsigned int)length, (unsigned int)sizeof(ImuPacket));
    return;
  }

  // Biomarker notification (MAX30102 on Node 1)
  if (length == sizeof(BioPacket) &&
      (pData[sizeof(BioPacket) - 1] & BIO_PACKET_MARKER_MASK) ==
          BIO_PACKET_MARKER) {
    const BioPacket *bio = (const BioPacket *)pData;
    if (bio->nodeId >= 1 && bio->nodeId <= MAX_NODES) {
      int idx = bio->nodeId - 1;
      nodesTelemetry[idx].bpm = bio->bpm / 10.0f;
      nodesTelemetry[idx].spo2 = bio->spo2;
      nodesTelemetry[idx].finger = (bio->finger != 0);
      nodesTelemetry[idx].rawRed = bio->red;
      nodesTelemetry[idx].rawIr = bio->ir;
      nodesTelemetry[idx].lastReceivedMillis = millis();

      static unsigned long lastBioLog = 0;
      if (millis() - lastBioLog >= 1000) {
        lastBioLog = millis();
        Serial.printf("📥 [GW-BIO] Node #%d | IR: %lu | Red: %lu | BPM: %.1f | "
                      "SpO2: %u%% | Finger: %s\n",
                      bio->nodeId, (unsigned long)bio->ir,
                      (unsigned long)bio->red, nodesTelemetry[idx].bpm,
                      nodesTelemetry[idx].spo2,
                      nodesTelemetry[idx].finger ? "YES" : "NO");
      }
    }
    return;
  }

  // New nodes send telemetry and raw IMU axes together in one 33-byte
  // notification. The link negotiates ATT MTU 64 before enabling this format.
  if (length == sizeof(CombinedImuPacket) &&
      pData[sizeof(CombinedImuPacket) - 1] == COMBINED_IMU_PACKET_MARKER) {
    const CombinedImuPacket *pkt =
        reinterpret_cast<const CombinedImuPacket *>(pData);
    if (pkt->nodeId < 1 || pkt->nodeId > MAX_NODES)
      return;

    const int idx = pkt->nodeId - 1;
    NodeTelemetry &telemetry = nodesTelemetry[idx];
    const unsigned long receivedAt = millis();
    const bool sequenceWrapped =
        telemetry.lastSequence > 60000 && pkt->sequence < 100;
    if (sequenceWrapped) {
      portENTER_CRITICAL(&datasetMux);
      datasetHistory[idx].nextIndex = 0;
      datasetHistory[idx].count = 0;
      datasetHistory[idx].bootId = esp_random();
      if (datasetHistory[idx].bootId == 0)
        datasetHistory[idx].bootId = 1;
      portEXIT_CRITICAL(&datasetMux);
      telemetry.lastSequence = 0;
    }

    if (telemetry.lastSequence == 0) {
      Serial.printf(
          "🎉 [GW-RECV] First combined packet from Node #%d! Seq: %u\n",
          pkt->nodeId, static_cast<unsigned>(pkt->sequence));
    } else {
      const uint16_t sequenceDelta =
          static_cast<uint16_t>(pkt->sequence - telemetry.lastSequence);
      if (sequenceDelta > 1 && sequenceDelta < 0x8000) {
        const uint16_t dropped = sequenceDelta - 1;
        telemetry.totalDropped += dropped;
        static unsigned long lastGapLogTime[MAX_NODES] = {0, 0, 0, 0};
        if (receivedAt - lastGapLogTime[idx] >= 1000) {
          lastGapLogTime[idx] = receivedAt;
          Serial.printf(
              "⚠️ [GATEWAY-BLE] Node #%d missed %u combined samples; total=%u\n",
              pkt->nodeId, static_cast<unsigned>(dropped),
              static_cast<unsigned>(telemetry.totalDropped));
        }
      }
    }

    telemetry.lastSequence = pkt->sequence;
    telemetry.lastTimestamp = pkt->timestamp;
    telemetry.pitch = pkt->pitch / 100.0f;
    telemetry.roll = pkt->roll / 100.0f;
    telemetry.yaw = pkt->yaw / 100.0f;
    telemetry.ax = pkt->ax / 100.0f;
    telemetry.ay = pkt->ay / 100.0f;
    telemetry.az = pkt->az / 100.0f;
    telemetry.gx = (pkt->rawGx / 65.5f) * 0.01745329252f;
    telemetry.gy = (pkt->rawGy / 65.5f) * 0.01745329252f;
    telemetry.gz = (pkt->rawGz / 65.5f) * 0.01745329252f;
    telemetry.status = pkt->status;
    telemetry.lastReceivedMillis = receivedAt;
    telemetry.lastRawReceivedMillis = receivedAt;

    DatasetSample sample = {};
    sample.timestamp = pkt->timestamp;
    sample.sequence = pkt->sequence;
    sample.rawAx = pkt->rawAx;
    sample.rawAy = pkt->rawAy;
    sample.rawAz = pkt->rawAz;
    sample.rawGx = pkt->rawGx;
    sample.rawGy = pkt->rawGy;
    sample.rawGz = pkt->rawGz;
    sample.pitch = pkt->pitch;
    sample.roll = pkt->roll;
    sample.yaw = pkt->yaw;
    sample.ax = pkt->ax;
    sample.ay = pkt->ay;
    sample.az = pkt->az;
    sample.status = pkt->status;
    sample.telemetryValid = true;
    portENTER_CRITICAL(&datasetMux);
    NodeDatasetHistory &history = datasetHistory[idx];
    history.samples[history.nextIndex] = sample;
    history.nextIndex = (history.nextIndex + 1) % DATASET_HISTORY_CAPACITY;
    if (history.count < DATASET_HISTORY_CAPACITY)
      ++history.count;
    portEXIT_CRITICAL(&datasetMux);

    static unsigned long lastCombinedLogTime[MAX_NODES] = {0, 0, 0, 0};
    if (receivedAt - lastCombinedLogTime[idx] >= 1000) {
      lastCombinedLogTime[idx] = receivedAt;
      Serial.printf("📥 [GW-RECV] Node #%d [COMBINED] | Seq: %5u | "
                    "P: %5.1f R: %5.1f Y: %5.1f | Drops: %u\n",
                    pkt->nodeId, static_cast<unsigned>(pkt->sequence),
                    telemetry.pitch, telemetry.roll, telemetry.yaw,
                    static_cast<unsigned>(telemetry.totalDropped));
    }
    return;
  }

  // Raw six-axis notification used only by the local Dataset recorder.
  if (length == sizeof(RawImuPacket) &&
      (pData[sizeof(RawImuPacket) - 1] & RAW_IMU_PACKET_MARKER_MASK) ==
          RAW_IMU_PACKET_MARKER) {
    const RawImuPacket *raw = (const RawImuPacket *)pData;
    if (raw->nodeId < 1 || raw->nodeId > MAX_NODES)
      return;
    const int idx = raw->nodeId - 1;
    DatasetSample sample = {};
    sample.timestamp = raw->timestamp;
    sample.sequence = raw->sequence;
    sample.rawAx = raw->ax;
    sample.rawAy = raw->ay;
    sample.rawAz = raw->az;
    sample.rawGx = raw->gx;
    sample.rawGy = raw->gy;
    sample.rawGz = raw->gz;
    sample.status = raw->marker & 1;
    nodesTelemetry[idx].lastRawReceivedMillis = millis();
    nodesTelemetry[idx].gx = (raw->gx / 65.5f) * 0.01745329252f;
    nodesTelemetry[idx].gy = (raw->gy / 65.5f) * 0.01745329252f;
    nodesTelemetry[idx].gz = (raw->gz / 65.5f) * 0.01745329252f;
    if (nodesTelemetry[idx].lastSequence == raw->sequence) {
      sample.telemetryValid = true;
      sample.pitch = (int16_t)roundf(nodesTelemetry[idx].pitch * 100.0f);
      sample.roll = (int16_t)roundf(nodesTelemetry[idx].roll * 100.0f);
      sample.yaw = (int16_t)roundf(nodesTelemetry[idx].yaw * 100.0f);
      sample.ax = (int16_t)roundf(nodesTelemetry[idx].ax * 100.0f);
      sample.ay = (int16_t)roundf(nodesTelemetry[idx].ay * 100.0f);
      sample.az = (int16_t)roundf(nodesTelemetry[idx].az * 100.0f);
    }
    portENTER_CRITICAL(&datasetMux);
    NodeDatasetHistory &history = datasetHistory[idx];
    history.samples[history.nextIndex] = sample;
    history.nextIndex = (history.nextIndex + 1) % DATASET_HISTORY_CAPACITY;
    if (history.count < DATASET_HISTORY_CAPACITY)
      ++history.count;
    portEXIT_CRITICAL(&datasetMux);
    return;
  }

  if (length != sizeof(ImuPacket)) {
    Serial.printf(
        "⚠️ [GW-NOTIFY-LEN] Unexpected packet: %u bytes (expected %u)\n",
        (unsigned int)length, (unsigned int)sizeof(ImuPacket));
    return;
  }

  const ImuPacket *pkt = (const ImuPacket *)pData;
  if (pkt->nodeId < 1 || pkt->nodeId > MAX_NODES)
    return;

  int idx = pkt->nodeId - 1;
  NodeTelemetry &telemetry = nodesTelemetry[idx];

  const bool sequenceWrapped =
      telemetry.lastSequence > 60000 && pkt->sequence < 100;
  if (sequenceWrapped) {
    portENTER_CRITICAL(&datasetMux);
    datasetHistory[idx].nextIndex = 0;
    datasetHistory[idx].count = 0;
    datasetHistory[idx].bootId = esp_random();
    if (datasetHistory[idx].bootId == 0)
      datasetHistory[idx].bootId = 1;
    portEXIT_CRITICAL(&datasetMux);
    telemetry.lastSequence = 0;
  }

  if (telemetry.lastSequence == 0) {
    Serial.printf("🎉 [GW-RECV] First packet from Node #%d received! Seq: %u\n",
                  pkt->nodeId, pkt->sequence);
  }

  // Check packet drop sequence
  if (telemetry.lastSequence != 0) {
    const uint16_t sequenceDelta =
        static_cast<uint16_t>(pkt->sequence - telemetry.lastSequence);
    if (sequenceDelta > 1 && sequenceDelta < 0x8000) {
      const uint16_t dropped = sequenceDelta - 1;
      telemetry.totalDropped += dropped;
      Serial.printf(
          "⚠️ [GATEWAY-BLE] Node #%d Dropped %u packets (Seq: %u -> %u)\n",
          pkt->nodeId, dropped, telemetry.lastSequence, pkt->sequence);
    }
  }
  telemetry.lastSequence = pkt->sequence;
  telemetry.lastTimestamp = pkt->timestamp;
  telemetry.pitch = pkt->pitch / 100.0f;
  telemetry.roll = pkt->roll / 100.0f;
  telemetry.yaw = pkt->yaw / 100.0f;
  telemetry.ax = pkt->ax / 100.0f;
  telemetry.ay = pkt->ay / 100.0f;
  telemetry.az = pkt->az / 100.0f;
  telemetry.status = pkt->status;
  telemetry.lastReceivedMillis = millis();

  // Log live telemetry from node (rate-limited to 1s per node)
  static unsigned long lastNodeLogTime[MAX_NODES] = {0, 0, 0, 0};
  if (millis() - lastNodeLogTime[idx] >= 1000) {
    lastNodeLogTime[idx] = millis();
    Serial.printf("📥 [GW-RECV] Node #%d%s | Seq: %5u | Pitch: %5.1f° | Roll: "
                  "%5.1f° | Yaw: %5.1f° | Drops: %u\n",
                  pkt->nodeId, (pkt->status == 2) ? " [MOCK]" : " [REAL]",
                  pkt->sequence, telemetry.pitch, telemetry.roll, telemetry.yaw,
                  static_cast<unsigned>(telemetry.totalDropped));
  }
}

// Advertised Device Callback
class AdvertisedDeviceCallbacks : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice *advertisedDevice) override {
    int id = 0;
    std::string name = advertisedDevice->getName();
    if (name.rfind("PlanKO_IMU_", 0) == 0 && name.length() == 12) {
      id = name[11] - '0';
    }

    // Only the configured Nodes participate in this Gateway session.
    if (isExpectedBleNode(id)) {
      int idx = id - 1;
      // If node is already connected, ignore advertisement
      if (nodesTelemetry[idx].connected) {
        return;
      }

      // Copy only the address. Advertised objects belong to the scanner and
      // can disappear on clearResults(); no shared heap pointer crosses tasks.
      const NimBLEAddress address = advertisedDevice->getAddress();
      bool queued = false;
      const uint32_t now = millis();
      portENTER_CRITICAL(&bleConnectMux);
      PendingBleNode &pending = pendingBleNodes[idx];
      if (!pending.pending && !pending.connecting &&
          (pending.retryAfter == 0 ||
           static_cast<int32_t>(now - pending.retryAfter) >= 0)) {
        pending.address = address;
        pending.pending = true;
        queued = true;
      }
      portEXIT_CRITICAL(&bleConnectMux);
      if (queued) {
        Serial.printf("🔎 [GATEWAY-BLE] Found Node #%d ('%s') [%s] RSSI: %d\n",
                      id, name.c_str(), address.toString().c_str(),
                      advertisedDevice->getRSSI());
      }
    }
  }
};

void resumeBleScanIfNeeded(bool forceRestart) {
  const int connectedCount = connectedExpectedBleNodeCount();
  const bool missingExpectedNode =
      connectedCount < static_cast<int>(EXPECTED_NODE_COUNT);
  static unsigned long lastScanCycle = 0;
  static int lastExpectedConnectedCount = -1;

  NimBLEScan *pScan = NimBLEDevice::getScan();
  if (!pScan)
    return;

  if (missingExpectedNode) {
    uint16_t desiredWindow = (connectedCount == 0) ? 120 : 32;
    unsigned long now = millis();

    // Restart scan if:
    // 1. Scanner is not currently running
    // 2. forceRestart requested (e.g. immediately on disconnect)
    // 3. Expected connected count changed (adjust 75%/20% scan duty cycle)
    // 4. Scanner has been running for >= 3000ms (re-arms ESP32 controller
    // GAP state to prevent stalls)
    bool shouldRestart = forceRestart || !pScan->isScanning() ||
                         (connectedCount != lastExpectedConnectedCount) ||
                         (now - lastScanCycle >= 3000);

    if (shouldRestart) {
      lastScanCycle = now;
      lastExpectedConnectedCount = connectedCount;

      if (pScan->isScanning()) {
        pScan->stop();
      }
      pScan->clearResults();
      pScan->setInterval(160);         // 100ms
      pScan->setWindow(desiredWindow); // 75ms (no nodes) or 20ms (coexistence)
      pScan->setActiveScan(false); // Passive scan: immediate ADV_IND reception
                                   // without RF collision
      pScan->start(0, nullptr, false);
    }
  } else {
    // All expected Nodes are connected. Stop scanning so the Gateway radio can
    // focus on the active BLE links and Wi-Fi; restart if an expected Node
    // drops.
    if (pScan->isScanning()) {
      pScan->stop();
      pScan->clearResults();
    }
    lastExpectedConnectedCount = connectedCount;
  }
}

bool connectBlePeer(int idx, const NimBLEAddress &peerAddr) {
  const int id = idx + 1;
  // Each Node owns one client for its lifetime. Reusing another Node's client
  // leaves stale aliases in pNodeClients and routes callbacks to the wrong ID.
  NimBLEClient *pClient = pNodeClients[idx];
  if (pClient == nullptr) {
    pClient = NimBLEDevice::createClient();
    if (pClient == nullptr) {
      Serial.printf("❌ [GATEWAY-BLE] Could not create client for Node #%d\n",
                    id);
      return false;
    }
    pClient->setClientCallbacks(new GatewayClientCallbacks(id), true);
    pNodeClients[idx] = pClient;
  }
  if (pClient->isConnected())
    return nodesTelemetry[idx].connected;
  // The Nodes publish at 20Hz. Give each BLE link a distinct connection
  // interval and disable slave latency so queued notifications are not
  // deferred across multiple events or repeatedly collide between links.
  // BLE units are 1.25ms: Nodes 1-4 use 30, 35, 40, and 45ms respectively.
  // Timeout 200 is 2.0s.
  const uint16_t connectionInterval =
      static_cast<uint16_t>(24 + (4 * (id - 1)));
  pClient->setConnectionParams(connectionInterval, connectionInterval, 0, 200);
  Serial.printf("[GATEWAY-BLE] Node #%d requested interval: %u ms, latency 0\n",
                id, static_cast<unsigned>(connectionInterval * 5U / 4U));
  pClient->setConnectTimeout(5);

  if (!pClient->connect(peerAddr, true)) {
    Serial.printf("❌ [GATEWAY-BLE] Failed to connect to Node #%d (NimBLE "
                  "error=%d; will retry)\n",
                  id, pClient->getLastError());
    return false;
  }

  NimBLERemoteService *pRemoteService =
      pClient->getService(PLANKO_SERVICE_UUID);
  if (pRemoteService == nullptr) {
    Serial.printf("❌ [GATEWAY-BLE] Failed to find service on Node #%d\n", id);
    pClient->disconnect();
    return false;
  }

  NimBLERemoteCharacteristic *pRemoteChar =
      pRemoteService->getCharacteristic(PLANKO_CHARACTERISTIC_UUID);
  if (pRemoteChar == nullptr || !pRemoteChar->canNotify()) {
    Serial.printf(
        "❌ [GATEWAY-BLE] Failed to find notify characteristic on Node #%d\n",
        id);
    pClient->disconnect();
    return false;
  }

  // Check CCCD descriptor
  NimBLERemoteDescriptor *pDesc =
      pRemoteChar->getDescriptor(NimBLEUUID((uint16_t)0x2902));
  Serial.printf("🔍 [GATEWAY-BLE] Node #%d Char handle: %d, CCCD handle: %d, "
                "canNotify: %d\n",
                id, pRemoteChar->getHandle(), pDesc ? pDesc->getHandle() : 0,
                pRemoteChar->canNotify());

  // Subscribe with response = false (standard non-blocking write to CCCD)
  bool subOk = pRemoteChar->subscribe(true, notifyCallback, false);
  Serial.printf("🔍 [GATEWAY-BLE] Node #%d subscribe returned: %s\n", id,
                subOk ? "TRUE" : "FALSE");

  if (!subOk || !pClient->isConnected()) {
    Serial.printf(
        "❌ [GATEWAY-BLE] Failed to subscribe to Node #%d notifications\n", id);
    pClient->disconnect();
    return false;
  }

  Serial.printf("🎉 [GATEWAY-BLE] Successfully subscribed to Node #%d "
                "telemetry! ATT MTU=%u\n",
                id, static_cast<unsigned>(pClient->getMTU()));
  nodesTelemetry[idx].connected = true;
  nodesTelemetry[idx].lastReceivedMillis = millis();
  nodesTelemetry[idx].lastSequence = 0;
  return true;
}

void connectToNode(int idx) {
  NimBLEAddress peerAddr;
  portENTER_CRITICAL(&bleConnectMux);
  PendingBleNode &pending = pendingBleNodes[idx];
  if (!pending.pending || pending.connecting) {
    portEXIT_CRITICAL(&bleConnectMux);
    return;
  }
  peerAddr = pending.address;
  pending.pending = false;
  pending.connecting = true;
  portEXIT_CRITICAL(&bleConnectMux);

  Serial.printf("⏳ [GATEWAY-BLE] Connecting to Node #%d [%s]...\n", idx + 1,
                peerAddr.toString().c_str());
  NimBLEScan *pScan = NimBLEDevice::getScan();
  if (pScan && pScan->isScanning())
    pScan->stop();

  const bool connected = connectBlePeer(idx, peerAddr);
  const uint32_t retryAfter = connected ? 0 : millis() + 3000;
  portENTER_CRITICAL(&bleConnectMux);
  pending.connecting = false;
  pending.retryAfter = retryAfter;
  portEXIT_CRITICAL(&bleConnectMux);
  // Keep weaker/unavailable Nodes from monopolizing retries; the next loop can
  // connect a different pending Node during this Node's cooldown.
  resumeBleScanIfNeeded(true);
}

void maintainMqttConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (!mqttClient.connected()) {
    unsigned long now = millis();
    if (now - lastMqttReconnectAttempt > 5000) {
      lastMqttReconnectAttempt = now;
      Serial.printf("[MQTT] Connecting to %s:%u... ", MQTT_BROKER, MQTT_PORT);
      IPAddress brokerAddress;
      if (WiFi.hostByName(MQTT_BROKER, brokerAddress) != 1) {
        Serial.println("DNS lookup failed; checking DHCP network settings");
        Serial.printf("[WIFI] IP=%s Gateway=%s DNS1=%s DNS2=%s\n",
                      WiFi.localIP().toString().c_str(),
                      WiFi.gatewayIP().toString().c_str(),
                      WiFi.dnsIP(0).toString().c_str(),
                      WiFi.dnsIP(1).toString().c_str());
        lastMqttReconnectAttempt = millis();
        return;
      }
      mqttClient.setServer(brokerAddress, MQTT_PORT);
      // Connect with Last Will and Testament on planko/status (non-retained)
      if (mqttClient.connect(mqttClientId, TOPIC_STATUS, 0, false, "offline")) {
        Serial.println("CONNECTED! 🎉");
        mqttClient.publish(TOPIC_STATUS, "online", false);
        if (mqttClient.subscribe(mqttCommandTopic, 0)) {
          Serial.printf("📡 [MQTT] Ready! Publishing '%s', '%s', '%s'; "
                        "listening on '%s'\n",
                        TOPIC_IMU, TOPIC_PAD, TOPIC_PRESSURE, mqttCommandTopic);
        } else {
          Serial.printf("⚠️ [MQTT] Subscribe failed for '%s'\n",
                        mqttCommandTopic);
        }
      } else {
        Serial.printf("failed, rc=%d (will retry in 5s)\n", mqttClient.state());
      }
      lastMqttReconnectAttempt = millis();
    }
  }
}

bool isKnownPad(const char *sensorId) {
  if (sensorId == nullptr || sensorId[0] == '\0')
    return false;
  for (size_t i = 0; i < TOTAL_PADS; ++i) {
    if (strcmp(PAD_CONFIGS[i].name, sensorId) == 0)
      return true;
  }
  return false;
}

void gatewayMqttCallback(char *topic, byte *payload, unsigned int length) {
  if (topic == nullptr || strcmp(topic, mqttCommandTopic) != 0 ||
      payload == nullptr || length == 0)
    return;
  JsonDocument command;
  if (deserializeJson(command, payload, length)) {
    Serial.println("[MQTT] Ignored malformed Gateway command");
    return;
  }

  const char *targetGateway = command["gatewayId"] | "";
  if (strcmp(targetGateway, mqttClientId) != 0)
    return;

  const char *cmd = command["cmd"] | "";
  if (strcmp(cmd, "pad") == 0) {
    const char *pad = command["pad"] | "";
    if (isKnownPad(pad)) {
      snprintf(pendingMqttPadCommand, sizeof(pendingMqttPadCommand), "%s", pad);
      pendingMqttPadEvent = true;
    } else {
      Serial.printf("[MQTT] Ignored unknown pad command: '%s'\n", pad);
    }
  }
}

void sendPadPressEvent(const char *sensorId) {
  char jsonBuffer[128];
  snprintf(jsonBuffer, sizeof(jsonBuffer),
           "{\"sensorId\":\"%s\",\"value\":1,\"gatewayId\":\"%s\"}", sensorId,
           mqttClientId);
  if (mqttClient.connected()) {
    mqttClient.publish(TOPIC_PAD, jsonBuffer);
  }
  Serial.printf("⚡ [PAD EVENT] %s (Published to %s)\n", sensorId, TOPIC_PAD);
}

void publishImuStream() {
  if (!mqttClient.connected())
    return;

  // Check how many nodes are connected
  int connectedCount = 0;
  for (int i = 0; i < MAX_NODES; i++) {
    if (nodesTelemetry[i].connected)
      connectedCount++;
  }

  unsigned long now = millis();
  JsonDocument doc;
  doc["type"] = "imu_stream";
  doc["schema"] = "planko-imu-v2";
  doc["units"]["accel"] = "m/s^2";
  doc["units"]["gyro"] = "rad/s";
  doc["units"]["orientation"] = "degrees";

  JsonObject gw = doc["gateway"].to<JsonObject>();
  gw["uptimeMs"] = millis();
  gw["connectedNodes"] = connectedCount;
  static uint32_t mqttSequence = 0;
  gw["mqttSeq"] = mqttSequence++;
  gw["clientId"] = mqttClientId;
  gw["bootId"] = gatewayBootId;
  gw["commandTopic"] = mqttCommandTopic;

  JsonArray nodes = doc["nodes"].to<JsonArray>();
  bool cursorAdvance[MAX_NODES] = {};
  uint16_t cursorSequence[MAX_NODES] = {};
  uint32_t cursorBootId[MAX_NODES] = {};

  for (int i = 0; i < MAX_NODES; i++) {
    const bool nodeConnected =
        nodesTelemetry[i].connected &&
        (nodesTelemetry[i].lastReceivedMillis == 0 ||
         (now - nodesTelemetry[i].lastReceivedMillis) < STALE_TIMEOUT_MS);
    DatasetSample latestSample = {};
    DatasetSample newSamples[MQTT_SAMPLE_BATCH_LIMIT] = {};
    size_t newSampleCount = 0;
    uint32_t nodeBootId = 0;
    bool hasSample = false;

    portENTER_CRITICAL(&datasetMux);
    const NodeDatasetHistory &history = datasetHistory[i];
    if (history.count > 0) {
      const size_t oldest =
          (history.nextIndex + DATASET_HISTORY_CAPACITY - history.count) %
          DATASET_HISTORY_CAPACITY;
      latestSample =
          history.samples[(history.nextIndex + DATASET_HISTORY_CAPACITY - 1) %
                          DATASET_HISTORY_CAPACITY];
      nodeBootId = history.bootId;
      hasSample = true;

      const bool sameCursorEpoch =
          mqttPublishedCursorValid[i] && mqttPublishedBootId[i] == nodeBootId;
      uint16_t cursor =
          sameCursorEpoch
              ? mqttPublishedSequence[i]
              : static_cast<uint16_t>(history.samples[oldest].sequence - 1);

      for (size_t j = 0;
           j < history.count && newSampleCount < MQTT_SAMPLE_BATCH_LIMIT; ++j) {
        const DatasetSample &candidate =
            history.samples[(oldest + j) % DATASET_HISTORY_CAPACITY];
        const uint16_t delta =
            static_cast<uint16_t>(candidate.sequence - cursor);
        if (delta == 0 || delta >= 0x8000)
          continue;
        newSamples[newSampleCount++] = candidate;
        cursor = candidate.sequence;
      }
      if (newSampleCount > 0) {
        cursorAdvance[i] = true;
        cursorSequence[i] = cursor;
        cursorBootId[i] = nodeBootId;
      }
    }
    portEXIT_CRITICAL(&datasetMux);

    // Flush buffered samples even after a BLE disconnect. Keep connected nodes
    // in the stream for live status; disconnected nodes appear only while a
    // batch is waiting to be published.
    if (!nodeConnected && newSampleCount == 0)
      continue;

    JsonObject n = nodes.add<JsonObject>();
    n["nodeId"] = i + 1;
    appendBatteryJson(n, i);
    n["sensor_uid"] = nodeSensorUids[i];
    n["mount_site"] = nodeConfigs[i].mountSite;
    n["body_part"] = nodeConfigs[i].bodyPart;
    n["side"] = nodeConfigs[i].side;
    n["boot_id"] = nodeBootId;
    n["connected"] = nodeConnected;
    n["seq"] =
        hasSample ? latestSample.sequence : nodesTelemetry[i].lastSequence;
    n["ts"] =
        hasSample ? latestSample.timestamp : nodesTelemetry[i].lastTimestamp;
    n["valid"] =
        hasSample && now - nodesTelemetry[i].lastRawReceivedMillis < 1000;
    n["calibrated"] = hasSample && latestSample.status == 1;
    // IMU bias calibration is not a relative-orientation calibration profile.
    n["relative_orientation_valid"] = false;
    n["pitch"] = round(nodesTelemetry[i].pitch * 10.0f) / 10.0f;
    n["roll"] = round(nodesTelemetry[i].roll * 10.0f) / 10.0f;
    n["yaw"] = round(nodesTelemetry[i].yaw * 10.0f) / 10.0f;
    n["dropped"] = nodesTelemetry[i].totalDropped;
    n["isMock"] = (nodesTelemetry[i].status == 2);

    JsonArray acc = n["accel"].to<JsonArray>();
    acc.add(hasSample ? (latestSample.telemetryValid
                             ? latestSample.ax / 100.0f
                             : latestSample.rawAx / 8192.0f * 9.80665f)
                      : nodesTelemetry[i].ax);
    acc.add(hasSample ? (latestSample.telemetryValid
                             ? latestSample.ay / 100.0f
                             : latestSample.rawAy / 8192.0f * 9.80665f)
                      : nodesTelemetry[i].ay);
    acc.add(hasSample ? (latestSample.telemetryValid
                             ? latestSample.az / 100.0f
                             : latestSample.rawAz / 8192.0f * 9.80665f)
                      : nodesTelemetry[i].az);
    if (hasSample) {
      JsonArray gyro = n["gyro"].to<JsonArray>();
      gyro.add(latestSample.rawGx / 65.5f * 0.01745329252f);
      gyro.add(latestSample.rawGy / 65.5f * 0.01745329252f);
      gyro.add(latestSample.rawGz / 65.5f * 0.01745329252f);
    }

    JsonArray batch = n["samples"].to<JsonArray>();
    for (size_t j = 0; j < newSampleCount; ++j) {
      const DatasetSample &sample = newSamples[j];
      JsonObject row = batch.add<JsonObject>();
      row["boot_id"] = nodeBootId;
      row["seq"] = sample.sequence;
      row["ts"] = sample.timestamp;
      row["valid"] = true;
      row["calibrated"] = sample.status == 1;
      row["isMock"] = false;
      row["relative_orientation_valid"] = false;
      row["pitch"] = sample.telemetryValid ? sample.pitch / 100.0f : 0.0f;
      row["roll"] = sample.telemetryValid ? sample.roll / 100.0f : 0.0f;
      row["yaw"] = sample.telemetryValid ? sample.yaw / 100.0f : 0.0f;

      JsonArray sampleAccel = row["accel"].to<JsonArray>();
      sampleAccel.add(sample.telemetryValid
                          ? sample.ax / 100.0f
                          : sample.rawAx / 8192.0f * 9.80665f);
      sampleAccel.add(sample.telemetryValid
                          ? sample.ay / 100.0f
                          : sample.rawAy / 8192.0f * 9.80665f);
      sampleAccel.add(sample.telemetryValid
                          ? sample.az / 100.0f
                          : sample.rawAz / 8192.0f * 9.80665f);

      JsonArray sampleGyro = row["gyro"].to<JsonArray>();
      sampleGyro.add(sample.rawGx / 65.5f * 0.01745329252f);
      sampleGyro.add(sample.rawGy / 65.5f * 0.01745329252f);
      sampleGyro.add(sample.rawGz / 65.5f * 0.01745329252f);
    }
  }

  String output;
  serializeJson(doc, output);
  bool pubOk = mqttClient.publish(TOPIC_IMU, output.c_str());
  if (pubOk) {
    for (int i = 0; i < MAX_NODES; ++i) {
      if (!cursorAdvance[i])
        continue;
      mqttPublishedSequence[i] = cursorSequence[i];
      mqttPublishedBootId[i] = cursorBootId[i];
      mqttPublishedCursorValid[i] = true;
    }
  }
  static unsigned long lastPubFailLog = 0;
  if (!pubOk && (millis() - lastPubFailLog > 3000)) {
    lastPubFailLog = millis();
    Serial.printf(
        "⚠️ [MQTT] Publish to '%s' failed: rc=%d payload=%u bytes buffer=%u "
        "bytes connectedNodes=%d\n",
        TOPIC_IMU, mqttClient.state(), static_cast<unsigned>(output.length()),
        static_cast<unsigned>(mqttClient.getBufferSize()), connectedCount);
  }
}

void readRfp602Sensor() {
  // Normal Gateway runs 3 BLE IMU nodes only (no RFP sensor polling).
  return;
}

#if defined(RFP605_GATEWAY_INTEGRATION)
void publishRfp605Stream() {
  if (!mqttClient.connected())
    return;
  Rfp605Sample samples[16] = {};
  const size_t count = rfp605ReadQueuedSamples(samples, 16);
  if (count == 0)
    return;

  JsonDocument doc;
  doc["type"] = "rfp605_stream";
  doc["schema"] = "planko-rfp605-v1";
  doc["gatewayId"] = mqttClientId;
  doc["boot_id"] = gatewayBootId;
  doc["scan_interval_ms"] = 50;
  doc["sample_count"] = count;
  doc["queue_dropped_samples"] = rfp605DroppedSamples();
  JsonArray records = doc["samples"].to<JsonArray>();
  for (size_t i = 0; i < count; ++i) {
    const Rfp605Sample &sample = samples[i];
    JsonObject row = records.add<JsonObject>();
    char sensorId[16];
    snprintf(sensorId, sizeof(sensorId), "RFP605_C%02u",
             static_cast<unsigned>(sample.channel));
    row["id"] = sensorId;
    row["channel"] = sample.channel;
    row["seq"] = sample.sequence;
    row["sample_ms"] = sample.sampleMs;
    row["baseline_raw"] = sample.baselineRaw;
    row["press_delta_raw"] = sample.pressDeltaRaw;
    row["deviation_raw"] = sample.deviationRaw;
    row["pressure_raw"] = sample.pressureRaw;
    row["voltage_mV"] = sample.voltageMv;
    row["pressed"] = sample.pressed;
    row["event"] = sample.pressed ? "pressed" : "released";
  }

  String output;
  serializeJson(doc, output);
  if (!mqttClient.publish(TOPIC_PRESSURE, output.c_str())) {
    static unsigned long lastPublishError = 0;
    const unsigned long now = millis();
    if (now - lastPublishError >= 3000) {
      lastPublishError = now;
      Serial.printf("[MQTT] RFP605 publish to '%s' failed (rc=%d)\n",
                    TOPIC_PRESSURE, mqttClient.state());
    }
  }
}

void logRfp605StateChanges() {
  static uint16_t previousMask = 0;
  const uint16_t currentMask = rfp605PressedMask();
  const uint16_t changedMask = currentMask ^ previousMask;
  if (changedMask == 0)
    return;

  for (uint8_t channel = 0; channel < 16; ++channel) {
    const uint16_t bit = static_cast<uint16_t>(1U << channel);
    if ((changedMask & bit) == 0)
      continue;

    Serial.printf("[RFP605] %s C%u | pressed mask=0x%04X\n",
                  (currentMask & bit) ? "PRESSED" : "RELEASED",
                  static_cast<unsigned>(channel),
                  static_cast<unsigned>(currentMask));
  }
  previousMask = currentMask;
}
#endif

void publishBioTelemetry() {
  static unsigned long lastPublish = 0;
  const unsigned long now = millis();
  if (!mqttClient.connected() || !nodesTelemetry[0].connected ||
      now - lastPublish < 250)
    return;
  lastPublish = now;
  JsonDocument doc;
  doc["nodeId"] = 1;
  doc["bpm"] = nodesTelemetry[0].bpm;
  doc["spo2"] = nodesTelemetry[0].spo2;
  doc["finger"] = nodesTelemetry[0].finger;
  doc["red"] = nodesTelemetry[0].rawRed;
  doc["ir"] = nodesTelemetry[0].rawIr;
  String output;
  serializeJson(doc, output);
  mqttClient.publish(TOPIC_BIO, output.c_str());
}

void handlePadInputs() {
  unsigned long now = millis();
  for (size_t i = 0; i < TOTAL_PADS; i++) {
    if (sensors[i].pin < 0)
      continue;

    int rawReading = digitalRead(sensors[i].pin);
    if (rawReading != sensors[i].lastRawState) {
      sensors[i].lastDebounceTime = now;
    }

    if ((now - sensors[i].lastDebounceTime) > DEBOUNCE_DELAY_MS) {
      if (rawReading != sensors[i].debouncedState) {
        sensors[i].debouncedState = rawReading;
        if (sensors[i].debouncedState == LOW) {
          sendPadPressEvent(sensors[i].sensorId);
        }
      }
    }
    sensors[i].lastRawState = rawReading;
  }
}

void handleSerialSimulator() {
  if (!Serial.available())
    return;
  String input = Serial.readStringUntil('\n');
  input.trim();
  input.toUpperCase();
  if (input.length() == 0)
    return;

  if (input == "HELP") {
    Serial.println("\n--- [PlanKO Gateway Simulator] ---");
    Serial.println("Type 'L1' - 'L8' or 'R1' - 'R8' to trigger pad event");
    Serial.println("Type 'RFP' or 'PRESSURE' to read RFP-602 sensor");
    Serial.println("Type 'STATUS' to see connected BLE nodes & stats");
    return;
  }

  if (input == "RFP" || input == "PRESSURE") {
#if defined(RFP605_GATEWAY_INTEGRATION)
    Serial.printf("[RFP605] Active mask=0x%04X | Pressed mask=0x%04X\n",
                  static_cast<unsigned>(RFP605_ACTIVE_MASK),
                  static_cast<unsigned>(rfp605PressedMask()));
#else
    Serial.printf("🎯 [RFP-602] Raw ADC: %d | Voltage: %.2fV | State: %s\n",
                  rfp602RawValue, rfp602Voltage,
                  rfp602Pressed ? "PRESSED" : "IDLE");
#endif
    return;
  }

  if (input == "STATUS") {
    Serial.println("\n--- [Gateway Telemetry Status] ---");
    for (int i = 0; i < MAX_NODES; i++) {
      Serial.printf(
          "  Node #%d: %s | Seq: %u | Dropped: %u | P:%.1f R:%.1f Y:%.1f\n",
          i + 1, nodesTelemetry[i].connected ? "CONNECTED" : "OFFLINE",
          nodesTelemetry[i].lastSequence,
          static_cast<unsigned>(nodesTelemetry[i].totalDropped),
          nodesTelemetry[i].pitch, nodesTelemetry[i].roll,
          nodesTelemetry[i].yaw);
    }
    Serial.printf("  MQTT: %s | Gateway IP: %s\n\n",
                  mqttClient.connected() ? "CONNECTED" : "DISCONNECTED",
                  WiFi.localIP().toString().c_str());
    return;
  }

  for (size_t i = 0; i < TOTAL_PADS; i++) {
    if (input == PAD_CONFIGS[i].name) {
      Serial.printf("🎮 [SIMULATOR] Triggered: %s\n", input.c_str());
      sendPadPressEvent(PAD_CONFIGS[i].name);
      return;
    }
  }
}

bool isAllowedConfigValue(const String &value, const char *const allowed[],
                          size_t count) {
  for (size_t i = 0; i < count; ++i) {
    if (value == allowed[i])
      return true;
  }
  return false;
}

void loadNodeConfigs() {
  static const char *const sites[] = {"upper_back", "lumbar", "wrist"};
  static const char *const bodyParts[] = {"back", "wrist"};
  static const char *const sides[] = {"unknown", "left", "right"};
  // Create the namespace on first boot; read-only open reports NOT_FOUND on
  // fresh boards. Keep the compiled defaults if NVS itself is unavailable.
  if (!nodeConfigPreferences.begin("node_cfg", false)) {
    Serial.println("[NODES] NVS unavailable; using default mounting settings");
    return;
  }
  for (int i = 0; i < MAX_NODES; ++i) {
    char key[16];
    snprintf(key, sizeof(key), "name%d", i + 1);
    String name =
        nodeConfigPreferences.isKey(key)
            ? nodeConfigPreferences.getString(key, nodeConfigs[i].name)
            : String(nodeConfigs[i].name);
    if (name.length() > 0)
      name.toCharArray(nodeConfigs[i].name, sizeof(nodeConfigs[i].name));

    snprintf(key, sizeof(key), "site%d", i + 1);
    String site =
        nodeConfigPreferences.isKey(key)
            ? nodeConfigPreferences.getString(key, nodeConfigs[i].mountSite)
            : String(nodeConfigs[i].mountSite);
    if (isAllowedConfigValue(site, sites, sizeof(sites) / sizeof(sites[0])))
      site.toCharArray(nodeConfigs[i].mountSite,
                       sizeof(nodeConfigs[i].mountSite));

    snprintf(key, sizeof(key), "body%d", i + 1);
    String body =
        nodeConfigPreferences.isKey(key)
            ? nodeConfigPreferences.getString(key, nodeConfigs[i].bodyPart)
            : String(nodeConfigs[i].bodyPart);
    if (isAllowedConfigValue(body, bodyParts,
                             sizeof(bodyParts) / sizeof(bodyParts[0])))
      body.toCharArray(nodeConfigs[i].bodyPart,
                       sizeof(nodeConfigs[i].bodyPart));

    snprintf(key, sizeof(key), "side%d", i + 1);
    String side =
        nodeConfigPreferences.isKey(key)
            ? nodeConfigPreferences.getString(key, nodeConfigs[i].side)
            : String(nodeConfigs[i].side);
    if (isAllowedConfigValue(side, sides, sizeof(sides) / sizeof(sides[0])))
      side.toCharArray(nodeConfigs[i].side, sizeof(nodeConfigs[i].side));
  }
  nodeConfigPreferences.end();
}

void handleNodesApi() {
  JsonDocument doc;
  doc["ip"] = WiFi.localIP().toString();
  doc["uptime_ms"] = millis();
  doc["mqtt"] = mqttClient.connected();
  JsonArray list = doc["nodes"].to<JsonArray>();
  int connectedCount = 0;
  for (int i = 0; i < MAX_NODES; ++i) {
    const uint32_t lastReceived = nodesTelemetry[i].lastReceivedMillis;
    const bool hasData =
        lastReceived > 0 && millis() - lastReceived < STALE_TIMEOUT_MS;
    uint32_t bootId;
    portENTER_CRITICAL(&datasetMux);
    bootId = datasetHistory[i].bootId;
    portEXIT_CRITICAL(&datasetMux);
    if (nodesTelemetry[i].connected)
      ++connectedCount;
    JsonObject node = list.add<JsonObject>();
    node["nodeId"] = i + 1;
    appendBatteryJson(node, i);
    node["name"] = nodeConfigs[i].name;
    node["mount_site"] = nodeConfigs[i].mountSite;
    node["body_part"] = nodeConfigs[i].bodyPart;
    node["side"] = nodeConfigs[i].side;
    node["sensor_uid"] = nodeSensorUids[i];
    node["connected"] = nodesTelemetry[i].connected;
    node["hasData"] = hasData;
    node["latest_seq"] = nodesTelemetry[i].lastSequence;
    node["total_dropped"] = nodesTelemetry[i].totalDropped;
    node["boot_id"] = bootId;
  }
  doc["connectedNodes"] = connectedCount;
  String output;
  serializeJson(doc, output);
  webServer.send(200, "application/json", output);
}

void handleNodeConfigApi() {
  static const char *const sites[] = {"upper_back", "lumbar", "wrist"};
  static const char *const bodyParts[] = {"back", "wrist"};
  static const char *const sides[] = {"unknown", "left", "right"};
  if (!webServer.hasArg("node_id")) {
    webServer.send(400, "application/json",
                   "{\"message\":\"node_id is required\"}");
    return;
  }
  const int nodeId = webServer.arg("node_id").toInt();
  if (nodeId < 1 || nodeId > MAX_NODES) {
    webServer.send(400, "application/json",
                   "{\"message\":\"node_id must be 1..4\"}");
    return;
  }
  const String name = webServer.arg("name");
  const String site = webServer.arg("mount_site");
  const String body = webServer.arg("body_part");
  const String side = webServer.arg("side");
  if (name.length() == 0 || name.length() >= sizeof(nodeConfigs[0].name) ||
      !isAllowedConfigValue(site, sites, sizeof(sites) / sizeof(sites[0])) ||
      !isAllowedConfigValue(body, bodyParts,
                            sizeof(bodyParts) / sizeof(bodyParts[0])) ||
      !isAllowedConfigValue(side, sides, sizeof(sides) / sizeof(sides[0]))) {
    webServer.send(400, "application/json",
                   "{\"message\":\"invalid node configuration\"}");
    return;
  }

  char nameKey[16], siteKey[16], bodyKey[16], sideKey[16];
  snprintf(nameKey, sizeof(nameKey), "name%d", nodeId);
  snprintf(siteKey, sizeof(siteKey), "site%d", nodeId);
  snprintf(bodyKey, sizeof(bodyKey), "body%d", nodeId);
  snprintf(sideKey, sizeof(sideKey), "side%d", nodeId);
  nodeConfigPreferences.begin("node_cfg", false);
  const size_t savedName = nodeConfigPreferences.putString(nameKey, name);
  const size_t savedSite = nodeConfigPreferences.putString(siteKey, site);
  const size_t savedBody = nodeConfigPreferences.putString(bodyKey, body);
  const size_t savedSide = nodeConfigPreferences.putString(sideKey, side);
  nodeConfigPreferences.end();
  if (savedName == 0 || savedSite == 0 || savedBody == 0 || savedSide == 0) {
    webServer.send(500, "application/json",
                   "{\"message\":\"could not save node configuration\"}");
    return;
  }

  name.toCharArray(nodeConfigs[nodeId - 1].name,
                   sizeof(nodeConfigs[nodeId - 1].name));
  site.toCharArray(nodeConfigs[nodeId - 1].mountSite,
                   sizeof(nodeConfigs[nodeId - 1].mountSite));
  body.toCharArray(nodeConfigs[nodeId - 1].bodyPart,
                   sizeof(nodeConfigs[nodeId - 1].bodyPart));
  side.toCharArray(nodeConfigs[nodeId - 1].side,
                   sizeof(nodeConfigs[nodeId - 1].side));
  webServer.send(200, "application/json", "{\"ok\":true}");
}

bool isSequenceAfter(uint16_t sequence, uint16_t afterSequence) {
  return static_cast<int16_t>(sequence - afterSequence) > 0;
}

void handleSamplesApi() {
  static DatasetSample snapshot[MAX_NODES][DATASET_BATCH_LIMIT];
  static size_t snapshotCount[MAX_NODES] = {};
  static uint32_t snapshotBootId[MAX_NODES] = {};
  static bool snapshotOverflow[MAX_NODES] = {};

  portENTER_CRITICAL(&datasetMux);
  for (int i = 0; i < MAX_NODES; ++i) {
    const NodeDatasetHistory &history = datasetHistory[i];
    snapshotCount[i] = history.count < DATASET_BATCH_LIMIT
                           ? history.count
                           : DATASET_BATCH_LIMIT;
    snapshotOverflow[i] = history.count > DATASET_BATCH_LIMIT;
    snapshotBootId[i] = history.bootId;
    const size_t oldest =
        (history.nextIndex + DATASET_HISTORY_CAPACITY - snapshotCount[i]) %
        DATASET_HISTORY_CAPACITY;
    for (size_t j = 0; j < snapshotCount[i]; ++j) {
      snapshot[i][j] = history.samples[(oldest + j) % DATASET_HISTORY_CAPACITY];
    }
  }
  portEXIT_CRITICAL(&datasetMux);

  JsonDocument doc;
  doc["gateway_boot_id"] = gatewayBootId;
  doc["units"]["accel"] = "m/s^2";
  doc["units"]["gyro"] = "rad/s";
  JsonArray nodes = doc["nodes"].to<JsonArray>();
  const uint32_t now = millis();
  for (int i = 0; i < MAX_NODES; ++i) {
    char bootKey[32], sequenceKey[32];
    snprintf(bootKey, sizeof(bootKey), "n%d_boot_id", i + 1);
    snprintf(sequenceKey, sizeof(sequenceKey), "n%d_after_seq", i + 1);
    const bool hasCursor =
        webServer.hasArg(bootKey) && webServer.hasArg(sequenceKey) &&
        static_cast<uint32_t>(strtoul(webServer.arg(bootKey).c_str(), nullptr,
                                      10)) == snapshotBootId[i];
    const uint16_t afterSequence =
        hasCursor ? static_cast<uint16_t>(strtoul(
                        webServer.arg(sequenceKey).c_str(), nullptr, 10))
                  : 0;

    bool dropped = !hasCursor && snapshotOverflow[i];
    if (hasCursor && snapshotCount[i] > 0) {
      const uint16_t expected = static_cast<uint16_t>(afterSequence + 1);
      bool expectedStillBuffered = false;
      for (size_t j = 0; j < snapshotCount[i]; ++j) {
        if (snapshot[i][j].sequence == expected)
          expectedStillBuffered = true;
      }
      dropped = isSequenceAfter(snapshot[i][snapshotCount[i] - 1].sequence,
                                afterSequence) &&
                !expectedStillBuffered;
    }

    JsonObject node = nodes.add<JsonObject>();
    const uint32_t lastReceived = nodesTelemetry[i].lastReceivedMillis;
    node["nodeId"] = i + 1;
    node["name"] = nodeConfigs[i].name;
    node["mount_site"] = nodeConfigs[i].mountSite;
    node["body_part"] = nodeConfigs[i].bodyPart;
    node["side"] = nodeConfigs[i].side;
    node["sensor_uid"] = nodeSensorUids[i];
    node["connected"] = nodesTelemetry[i].connected;
    node["hasData"] = lastReceived > 0 && now - lastReceived < STALE_TIMEOUT_MS;
    node["boot_id"] = snapshotBootId[i];
    node["latest_seq"] =
        snapshotCount[i] ? snapshot[i][snapshotCount[i] - 1].sequence : 0;
    node["total_dropped"] = nodesTelemetry[i].totalDropped;
    node["dropped"] = dropped;
    JsonArray samples = node["samples"].to<JsonArray>();
    for (size_t j = 0; j < snapshotCount[i]; ++j) {
      const DatasetSample &source = snapshot[i][j];
      if (hasCursor && !isSequenceAfter(source.sequence, afterSequence))
        continue;
      JsonObject sample = samples.add<JsonObject>();
      sample["sample_ms"] = source.timestamp;
      sample["sensor_uid"] = nodeSensorUids[i];
      sample["mount_site"] = nodeConfigs[i].mountSite;
      sample["body_part"] = nodeConfigs[i].bodyPart;
      sample["side"] = nodeConfigs[i].side;
      sample["boot_id"] = snapshotBootId[i];
      sample["accel_unit"] = "m/s^2";
      sample["gyro_unit"] = "rad/s";
      sample["relative_orientation_valid"] = false;
      sample["seq"] = source.sequence;
      sample["raw_ax"] = source.rawAx;
      sample["raw_ay"] = source.rawAy;
      sample["raw_az"] = source.rawAz;
      sample["raw_gx"] = source.rawGx;
      sample["raw_gy"] = source.rawGy;
      sample["raw_gz"] = source.rawGz;
      const float rawAxG = source.rawAx / 8192.0f;
      const float rawAyG = source.rawAy / 8192.0f;
      const float rawAzG = source.rawAz / 8192.0f;
      const float accelX =
          source.telemetryValid ? source.ax / 100.0f : rawAxG * 9.80665f;
      const float accelY =
          source.telemetryValid ? source.ay / 100.0f : rawAyG * 9.80665f;
      const float accelZ =
          source.telemetryValid ? source.az / 100.0f : rawAzG * 9.80665f;
      sample["ax"] = accelX;
      sample["ay"] = accelY;
      sample["az"] = accelZ;
      sample["gx"] = (source.rawGx / 65.5f) * 0.01745329252f;
      sample["gy"] = (source.rawGy / 65.5f) * 0.01745329252f;
      sample["gz"] = (source.rawGz / 65.5f) * 0.01745329252f;
      const float accelNormG =
          sqrtf(accelX * accelX + accelY * accelY + accelZ * accelZ) / 9.80665f;
      const float gxDps = source.rawGx / 65.5f;
      const float gyDps = source.rawGy / 65.5f;
      const float gzDps = source.rawGz / 65.5f;
      const float gyroNormDps =
          sqrtf(gxDps * gxDps + gyDps * gyDps + gzDps * gzDps);
      sample["accel_norm_g"] = accelNormG;
      sample["gyro_norm_dps"] = gyroNormDps;
      sample["steady"] =
          accelNormG >= 0.90f && accelNormG <= 1.10f && gyroNormDps <= 10.0f;
      sample["roll_deg"] = source.roll / 100.0f;
      sample["pitch_deg"] = source.pitch / 100.0f;
      sample["yaw_deg"] = source.yaw / 100.0f;
      sample["angle_valid"] = source.telemetryValid;
      sample["calibrated"] = source.status == 1;
      sample["valid"] = true;
      sample["status"] =
          source.status == 1 ? "real_calibrated" : "real_uncalibrated";
    }
  }
  String output;
  serializeJson(doc, output);
  webServer.send(200, "application/json", output);
}

void setupDatasetServer() {
  webServer.on("/", HTTP_GET, []() {
    webServer.send_P(200, "text/html; charset=utf-8", DATASET_PAGE);
  });
  webServer.on("/api/nodes", HTTP_GET, handleNodesApi);
  webServer.on("/api/node-config", HTTP_POST, handleNodeConfigApi);
  webServer.on("/samples", HTTP_GET, handleSamplesApi);
  webServer.onNotFound([]() {
    webServer.send(404, "application/json", "{\"error\":\"not_found\"}");
  });
  webServer.begin();
  Serial.printf("🌐 [DATASET] Web recorder at http://%s/\n",
                WiFi.localIP().toString().c_str());
}

void setup() {
  Serial.begin(115200);
#if !defined(RFP605_GATEWAY_INTEGRATION)
  Serial.setTxTimeoutMs(0);
#endif
  // Give USB CDC time to attach
  for (int i = 0; i < 20 && !Serial; i++) {
    delay(100);
  }
  delay(500);

  Serial.println(
      "\n****************************************************************");
  Serial.println("🌐 [PlanKO GATEWAY] Firmware Initializing...");
  Serial.println(
      "****************************************************************");

  gatewayBootId = esp_random();
  if (gatewayBootId == 0)
    gatewayBootId = 1;
  loadNodeConfigs();

#if defined(RFP605_GATEWAY_INTEGRATION)
  // Leave all connected sensors released during the baseline calibration.
  rfp605Begin();
  Serial.printf("[RFP605] CD74HC4067 ready: ADC GPIO %d, active mask 0x%04X\n",
                RFP605_ADC_PIN, static_cast<unsigned>(RFP605_ACTIVE_MASK));
#else
  Serial.println("📡 [GATEWAY] Running in 3-Node IMU-only mode (RFP disabled)");
#endif

  // Initialize Pads
  for (size_t i = 0; i < TOTAL_PADS; i++) {
    sensors[i].sensorId = PAD_CONFIGS[i].name;
    sensors[i].pin = PAD_CONFIGS[i].pin;
    sensors[i].lastRawState = HIGH;
    sensors[i].debouncedState = HIGH;
    sensors[i].lastDebounceTime = 0;
    if (sensors[i].pin >= 0) {
      pinMode(sensors[i].pin, INPUT_PULLUP);
    }
  }

  // Connect to Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);
  WiFi.setAutoReconnect(true);

  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println("\n📡 [WIFI] Associated with Wi-Fi AP!");
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.printf("\n🎉 [WIFI] Connected! IP: %s\n",
                    WiFi.localIP().toString().c_str());
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
      Serial.printf("\n⚠️ [WIFI] Disconnected (Reason: %d)\n",
                    info.wifi_sta_disconnected.reason);
      // Wi-Fi callbacks can run before NimBLE initialization. Defer BLE work
      // to loop() instead of accessing the scanner from this event task.
      bleScanRestartRequested.store(true);
      break;
    }
    default:
      break;
    }
  });

  WiFi.disconnect(true, true); // Clear any stale NVS credentials / Wi-Fi state
  delay(200);

  Serial.println("🔍 [WIFI] Scanning available Wi-Fi networks...");
  int n = WiFi.scanNetworks();
  Serial.printf("📡 [WIFI] Found %d networks:\n", n);
  int targetChannel = 0;
  for (int i = 0; i < n; ++i) {
    Serial.printf("   - '%s' (RSSI: %d dBm, Ch: %d, Auth: %d)\n",
                  WiFi.SSID(i).c_str(), static_cast<int>(WiFi.RSSI(i)),
                  static_cast<int>(WiFi.channel(i)), WiFi.encryptionType(i));
    if (WiFi.SSID(i) == WIFI_SSID) {
      targetChannel = WiFi.channel(i);
    }
  }
  WiFi.scanDelete();

  // Enable PMF (Protected Management Frames) for compatibility with modern
  // Wi-Fi routers
  wifi_config_t current_conf = {};
  if (esp_wifi_get_config(WIFI_IF_STA, &current_conf) == ESP_OK) {
    current_conf.sta.pmf_cfg.capable = true;
    current_conf.sta.pmf_cfg.required = false;
    esp_wifi_set_config(WIFI_IF_STA, &current_conf);
  }

  WiFi.setTxPower(
      WIFI_POWER_8_5dBm); // Use reduced TX power during association.
  Serial.println(
      "🌐 [WIFI] DHCP enabled: requesting IP, gateway and DNS from Wi-Fi AP");
  Serial.printf("⏳ [WIFI] Connecting to SSID: '%s'...\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    attempts++;
    if (attempts % 30 == 0) {
      Serial.printf("\n⏳ Still connecting to '%s' (Attempt %d). Retrying...\n",
                    WIFI_SSID, attempts);
      WiFi.disconnect();
      delay(200);
      if (targetChannel > 0) {
        WiFi.begin(WIFI_SSID, WIFI_PASS, targetChannel);
      } else {
        WiFi.begin(WIFI_SSID, WIFI_PASS);
      }
    }
  }
  Serial.println();

  Serial.printf("🎉 Wi-Fi Connected! IP: %s\n",
                WiFi.localIP().toString().c_str());
  Serial.printf(
      "[WIFI] Gateway: %s | Subnet: %s | DNS1: %s | DNS2: %s\n",
      WiFi.gatewayIP().toString().c_str(), WiFi.subnetMask().toString().c_str(),
      WiFi.dnsIP(0).toString().c_str(), WiFi.dnsIP(1).toString().c_str());
  setupDatasetServer();

  // Initialize MQTT Client
  snprintf(mqttClientId, sizeof(mqttClientId), "PlanKO_GW_%08X",
           static_cast<unsigned>(ESP.getEfuseMac() & 0xFFFFFFFFULL));
  snprintf(mqttCommandTopic, sizeof(mqttCommandTopic), "planko/gateway/%s/cmd",
           mqttClientId);
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(gatewayMqttCallback);
  if (!mqttClient.setBufferSize(8192)) {
    Serial.printf(
        "[MQTT] Failed to allocate 8192-byte buffer; using %u bytes\n",
        static_cast<unsigned>(mqttClient.getBufferSize()));
  }
  mqttClient.setKeepAlive(30);
  mqttClient.setSocketTimeout(5);
#if defined(RFP605_GATEWAY_INTEGRATION)
  rfp605Start();
#endif
  Serial.printf("🔌 MQTT Broker: %s:%u (ClientID: %s)\n", MQTT_BROKER,
                MQTT_PORT, mqttClientId);
  maintainMqttConnection();
  Serial.printf("💾 [MEMORY] Free Heap: %u bytes\n",
                static_cast<unsigned>(ESP.getFreeHeap()));

  Serial.println("👉 [INIT] Step 1: Enabling modem sleep for coexistence...");
  Serial.flush();
  WiFi.setSleep(true);
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

  Serial.println("👉 [INIT] Step 2: Initializing NimBLEDevice...");
  Serial.flush();
  NimBLEDevice::init("PlanKO_Gateway");
  const int mtuResult = NimBLEDevice::setMTU(64);
  Serial.printf("[BLE] Preferred ATT MTU 64: %s\n",
                mtuResult == 0 ? "ready" : "failed");

  Serial.println("👉 [INIT] Step 3: Configuring BLE Scan...");
  Serial.flush();
  NimBLEScan *pScan = NimBLEDevice::getScan();
  pScan->setAdvertisedDeviceCallbacks(new AdvertisedDeviceCallbacks(),
                                      true); // wantDuplicates = true
  pScan->setActiveScan(
      false); // Passive scan: immediate ADV_IND reception without RF contention
  pScan->setDuplicateFilter(false);
  pScan->setMaxResults(0);
  pScan->setInterval(160); // 100ms interval
  pScan->setWindow(120);   // 75ms window (75% duty cycle)

  Serial.println("👉 [INIT] Step 4: Starting BLE Continuous Scan...");
  Serial.flush();
  pScan->start(0, nullptr, false);
  isScanning = true;
  Serial.printf("🎉 [INIT] Setup completed! Scanner active, MQTT %s.\n",
                mqttClient.connected() ? "connected" : "waiting for broker");
  Serial.flush();
}

void loop() {
  if (bleScanRestartRequested.exchange(false))
    resumeBleScanIfNeeded(true);
  maintainMqttConnection();
  if (mqttClient.connected()) {
    mqttClient.loop();
  }
  if (pendingMqttPadEvent) {
    pendingMqttPadEvent = false;
    sendPadPressEvent(pendingMqttPadCommand);
  }
  webServer.handleClient();
#if defined(RFP605_GATEWAY_INTEGRATION)
  publishRfp605Stream();
  logRfp605StateChanges();
#endif
  handlePadInputs();
  handleSerialSimulator();

  unsigned long now = millis();

  // Check for pending BLE connections (one at a time to keep MQTT & Wi-Fi
  // responsive)
  static int nextBleNode = 0;
  for (int offset = 0; offset < MAX_NODES; offset++) {
    const int i = (nextBleNode + offset) % MAX_NODES;
    portENTER_CRITICAL(&bleConnectMux);
    const bool pending = pendingBleNodes[i].pending;
    portEXIT_CRITICAL(&bleConnectMux);
    if (pending) {
      connectToNode(i);
      nextBleNode = (i + 1) % MAX_NODES;
      break;
    }
  }

  // Detect stale BLE nodes (no data received for 2.0s) and disconnect
  unsigned long freshNow = millis();
  for (int i = 0; i < MAX_NODES; i++) {
    if (nodesTelemetry[i].connected &&
        nodesTelemetry[i].lastReceivedMillis > 0) {
      unsigned long lastRecv = nodesTelemetry[i].lastReceivedMillis;
      unsigned long elapsed = freshNow - lastRecv;
      if (elapsed > STALE_TIMEOUT_MS && elapsed < 60000UL) {
        Serial.printf("⚠️ [GATEWAY-BLE] Node #%d timeout (no data for %lums). "
                      "Disconnecting...\n",
                      i + 1, elapsed);
        nodesTelemetry[i].connected = false;
        nodesTelemetry[i].lastReceivedMillis = 0;
        nodesTelemetry[i].lastSequence = 0;
        if (pNodeClients[i] != nullptr && pNodeClients[i]->isConnected()) {
          pNodeClients[i]->disconnect();
        }
        resumeBleScanIfNeeded();
      }
    }
  }

  // Keep 50 ms cadence without catch-up bursts after blocking work.
  const unsigned long publishNow = millis();
  if (publishNow - lastMqttStreamTime >= MQTT_STREAM_INTERVAL_MS) {
    lastMqttStreamTime +=
        ((publishNow - lastMqttStreamTime) / MQTT_STREAM_INTERVAL_MS) *
        MQTT_STREAM_INTERVAL_MS;
    publishImuStream();
  }

  // Periodic MQTT Publish of Node 1 biometric telemetry (4Hz)
  publishBioTelemetry();

  // 🌟 Auto-resume BLE Scan watchdog every 1s if any node is disconnected
  static unsigned long lastBleScanWatchdog = 0;
  if (now - lastBleScanWatchdog >= 1000) {
    lastBleScanWatchdog = now;
    resumeBleScanIfNeeded();
  }

  // 🌟 Periodic Gateway Heartbeat to Serial every 2 seconds
  static unsigned long lastGatewayHeartbeat = 0;
  if (now - lastGatewayHeartbeat >= 2000) {
    lastGatewayHeartbeat = now;
    const int connectedCount = connectedExpectedBleNodeCount();
    char nodesSummary[64] = "";
    for (size_t expectedIndex = 0; expectedIndex < EXPECTED_NODE_COUNT;
         ++expectedIndex) {
      const int i = EXPECTED_NODE_IDS[expectedIndex] - 1;
      if (nodesTelemetry[i].connected) {
        char tmp[16];
        snprintf(tmp, sizeof(tmp), "#%d ", i + 1);
        strcat(nodesSummary, tmp);
      }
    }
    if (connectedCount == 0)
      strcpy(nodesSummary, "None");

#if defined(RFP605_GATEWAY_INTEGRATION)
    Serial.printf("🌐 [GATEWAY] Wi-Fi: %s (%s) | MQTT: %s | BLE: %d/%u [%s] | "
                  "RFP605 pressed mask: 0x%04X | Heap: %uB | Uptime: %lus\n",
                  (WiFi.status() == WL_CONNECTED) ? "ONLINE" : "OFFLINE",
                  (WiFi.status() == WL_CONNECTED)
                      ? WiFi.localIP().toString().c_str()
                      : "No IP",
                  mqttClient.connected() ? "CONNECTED" : "DISCONNECTED",
                  connectedCount, static_cast<unsigned>(EXPECTED_NODE_COUNT),
                  nodesSummary, static_cast<unsigned>(rfp605PressedMask()),
                  static_cast<unsigned>(ESP.getFreeHeap()), now / 1000);
#else
    Serial.printf(
        "🌐 [GATEWAY] Wi-Fi: %s (%s) | MQTT: %s | BLE: %d/%u [%s] | "
        "Heap: %uB | Uptime: %lus\n",
        (WiFi.status() == WL_CONNECTED) ? "ONLINE" : "OFFLINE",
        (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString().c_str()
                                        : "No IP",
        mqttClient.connected() ? "CONNECTED" : "DISCONNECTED", connectedCount,
        static_cast<unsigned>(EXPECTED_NODE_COUNT), nodesSummary,
        static_cast<unsigned>(ESP.getFreeHeap()), now / 1000);
#endif
  }
}

#else
#error                                                                         \
    "Please select an environment: 'gateway' or 'imu_node_1'..'imu_node_4' in platformio.ini"
#endif
```

### 6.18 ไฟล์ `WEB/imu_protocol.js` — โมดูลจัดการโปรโตคอล MQTT IMU v2, การแปลงหน่วย SI และตัวบันทึก DatasetRecorder (172 บรรทัด)
```javascript
/* Raw-sample contract shared by recording and MQTT inference. No features here:
   Python imu_features.window_features remains the train/inference extractor. */
(function (root) {
  'use strict';
  function sampleFromMessage(message, node, context) {
    if (message.units?.accel !== 'm/s^2' || message.units?.gyro !== 'rad/s') {
      throw new Error('Firmware units missing or incompatible; update Gateway firmware');
    }
    for (const name of ['accel', 'gyro']) {
      if (!Array.isArray(node[name]) || node[name].length !== 3 || !node[name].every(Number.isFinite)) {
        throw new Error(`Missing finite ${name} axes; sample excluded instead of filling zeros`);
      }
    }
    if (!Number.isInteger(node.seq) || !Number.isFinite(node.ts) || node.ts < 0) {
      throw new Error('Invalid sensor sequence or device timestamp');
    }
    if (node.valid !== true || node.isMock === true) throw new Error('Waiting for a valid real BLE sample');
    const site = node.mount_site || context.mount_site;
    const body = node.body_part || context.body_part;
    if (site !== context.mount_site || body !== context.body_part) {
      throw new Error(`Selected context differs from Gateway: ${site}/${body}; configure the node or selection`);
    }
    const side = node.side || 'unknown';
    if (context.side !== 'unknown' && side !== context.side) throw new Error('Selected side differs from Gateway');
    return {
      sensor_uid: node.sensor_uid || `${message.gateway?.clientId || 'gateway'}:node_${node.nodeId}`,
      mount_site: site, body_part: body, side,
      stream_epoch: `${message.gateway?.bootId ?? 0}:${node.boot_id ?? 0}`,
      seq: node.seq, boot_ms: node.ts, device_time_s: node.ts / 1000,
      accel_x: node.accel[0], accel_y: node.accel[1], accel_z: node.accel[2],
      gyro_x: node.gyro[0], gyro_y: node.gyro[1], gyro_z: node.gyro[2],
      roll_deg: node.roll, pitch_deg: node.pitch, yaw_deg: node.yaw,
      calibrated: node.calibrated === true,
      relative_orientation_valid: node.relative_orientation_valid === true,
      calibration_id: node.calibration_id || '',
      calibration_method_version: node.calibration_method_version || 0,
      valid: true, firmware_status: 'real_ble'
    };
  }
  class SampleWindow {
    constructor(seconds = 1, gap = 0.15) { this.seconds = seconds; this.gap = gap; this.rows = []; }
    reset() { this.rows = []; }
    add(row) {
      const prev = this.rows.at(-1);
      const identity = ['sensor_uid', 'stream_epoch', 'mount_site', 'body_part', 'side', 'calibration_id',
                        'calibration_method_version', 'calibrated', 'relative_orientation_valid'];
      const same = prev && identity.every(k => prev[k] === row[k]);
      if (same && prev.seq === row.seq) return false;
      if (prev && (!same || row.seq !== prev.seq + 1 || row.device_time_s <= prev.device_time_s ||
          row.device_time_s - prev.device_time_s > this.gap)) this.reset();
      this.rows.push(row);
      while (this.rows.length > 3 && row.device_time_s - this.rows[1].device_time_s >= this.seconds) this.rows.shift();
      return true;
    }
    ready() { return this.rows.length >= 3 && this.rows.at(-1).device_time_s - this.rows[0].device_time_s + 1e-9 >= this.seconds; }
  }
  // The firmware's legacy "back" name means the lumbar context in pose_labels.json.
  function trainingBody(node) {
    return node.body_part === 'back' && node.mount_site === 'lumbar' ? 'lumbar' : node.body_part;
  }
  function nodeKey(message, node) {
    return JSON.stringify([message.gateway?.clientId, node.nodeId, node.sensor_uid,
      node.mount_site, node.body_part, node.side || 'unknown']);
  }
  const CSV_COLUMNS = [
    'gateway_id', 'gateway_boot_id', 'mqtt_seq', 'node_id', 'sensor_uid',
    'mount_site', 'body_part', 'firmware_body_part', 'side', 'subject_id', 'session_id', 'trial_id',
    'action_label', 'phase_label', 'phase_annotation_source', 'phase_annotation_reviewed',
    'host_time_iso', 'host_time_s', 'host_elapsed_ms', 'boot_ms', 'device_time_s',
    'boot_id', 'seq', 'stream_epoch', 'gap_before', 'missing_samples_before',
    'accel_unit', 'gyro_unit', 'accel_x', 'accel_y', 'accel_z', 'gyro_x', 'gyro_y', 'gyro_z',
    'roll_deg', 'pitch_deg', 'yaw_deg', 'calibrated', 'relative_orientation_valid',
    'calibration_id', 'calibration_method_version', 'valid', 'firmware_status'
  ];
  function csv(rows) {
    const quote = value => /[",\r\n]/.test(String(value ?? ''))
      ? '"' + String(value).replace(/"/g, '""') + '"' : String(value ?? '');
    return '\uFEFF' + [CSV_COLUMNS.join(','), ...rows.map(row =>
      CSV_COLUMNS.map(key => quote(row[key])).join(','))].join('\r\n');
  }
  class MultiNodeRecorder {
    constructor() { this.active = false; this.streams = new Map(); }
    start(gatewayId, metadata, now = Date.now()) {
      if (!gatewayId) throw new Error('Select a Gateway before recording');
      this.gatewayId = gatewayId;
      this.metadata = {...metadata};
      this.startedAt = now;
      this.stoppedAt = null;
      this.streams.clear();
      this.active = true;
    }
    ingest(message, labels, annotation, now = Date.now()) {
      if (!this.active || message.gateway?.clientId !== this.gatewayId) return;
      for (const node of message.nodes || []) {
        const key = nodeKey(message, node);
        let stream = this.streams.get(key);
        if (!stream) {
          stream = {key, nodeId: node.nodeId, sensorUid: node.sensor_uid,
            mountSite: node.mount_site, bodyPart: trainingBody(node), side: node.side || 'unknown',
            rows: [], window: new SampleWindow(), duplicates: 0, invalid: 0,
            outOfOrder: 0, missing: 0, breaks: 0, gapPending: false, lastError: ''};
          this.streams.set(key, stream);
        }
        // Accept optional batches as well as the existing one-sample-per-node v2 stream.
        const samples = Array.isArray(node.samples) ? node.samples : [node];
        for (const sample of samples) {
          const source = {...node, ...sample};
          let row;
          try {
            if (!source.sensor_uid || !source.mount_site || !source.body_part) {
              throw new Error('Missing firmware sensor identity or mounting context');
            }
            row = sampleFromMessage(message, source, {
              mount_site: node.mount_site, body_part: node.body_part, side: node.side || 'unknown'
            });
          } catch (error) {
            stream.invalid++; stream.gapPending = true; stream.lastError = error.message;
            continue;
          }
          row.stream_epoch = `${this.gatewayId}:${row.stream_epoch}`;
          const previous = stream.window.rows.at(-1);
          const sameEpoch = previous && previous.stream_epoch === row.stream_epoch;
          if (sameEpoch && previous.seq === row.seq) { stream.duplicates++; continue; }
          const delta = previous ? (row.seq - previous.seq + 65536) % 65536 : 1;
          if (sameEpoch && (row.device_time_s <= previous.device_time_s || delta > 32768)) {
            stream.outOfOrder++; continue;
          }
          const missing = sameEpoch && delta > 1 ? delta - 1 : 0;
          const gap = stream.gapPending || (!!previous && (!sameEpoch || delta !== 1 ||
            row.device_time_s - previous.device_time_s > stream.window.gap));
          stream.window.add(row);
          stream.missing += missing;
          if (gap) stream.breaks++;
          stream.gapPending = false;
          stream.lastError = '';
          const label = labels(key, source) || '';
          stream.rows.push({...row, ...this.metadata,
            gateway_id: this.gatewayId, gateway_boot_id: message.gateway.bootId,
            mqtt_seq: message.gateway.mqttSeq, node_id: node.nodeId, boot_id: source.boot_id,
            body_part: trainingBody(source), firmware_body_part: source.body_part,
            action_label: label, phase_label: annotation.phase,
            phase_annotation_source: annotation.source,
            phase_annotation_reviewed: annotation.reviewed === true,
            host_time_iso: new Date(now).toISOString(), host_time_s: now / 1000,
            host_elapsed_ms: now - this.startedAt, gap_before: gap,
            missing_samples_before: missing, accel_unit: 'm/s^2', gyro_unit: 'rad/s'});
        }
      }
    }
    stop(now = Date.now()) { this.active = false; this.stoppedAt = now; }
    manifest() {
      return {schema: 'planko-multinode-capture-v1', transport: 'MQTT',
        broker: 'wss://test.mosquitto.org:8081/mqtt', topic: 'planko/imu',
        gateway_id: this.gatewayId, ...this.metadata,
        started_at: new Date(this.startedAt).toISOString(),
        stopped_at: this.stoppedAt === null ? null : new Date(this.stoppedAt).toISOString(),
        target_sampling_hz: 20,
        units: {accel: 'm/s^2', gyro: 'rad/s', orientation: 'degrees'},
        note: 'Samples are forwarded in per-node batches. MQTT QoS 0 cannot confirm delivery across broker disconnects; observed sequence gaps are reported. Device clocks are independent across nodes.',
        streams: [...this.streams.values()].map(s => ({node_id: s.nodeId, sensor_uid: s.sensorUid,
          mount_site: s.mountSite, body_part: s.bodyPart, side: s.side, samples: s.rows.length,
          duplicates_excluded: s.duplicates, invalid_excluded: s.invalid,
          out_of_order_excluded: s.outOfOrder, observed_missing_samples: s.missing,
          discontinuities: s.breaks, unlabeled_samples: s.rows.filter(r => !r.action_label).length,
          phase_reviewed_samples: s.rows.filter(r => r.phase_annotation_reviewed).length}))};
    }
  }
  const api = {sampleFromMessage, SampleWindow, MultiNodeRecorder, nodeKey, trainingBody, CSV_COLUMNS, csv};
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.PlanKOImu = api;
})(globalThis);
```

### 6.19 ไฟล์ `WEB/index.html` — หน้าเว็บหลัก 3D IMU Dashboard, Live Graph, Battery Monitor, Dataset Recorder และ AI Live Prediction (2503 บรรทัด)
```html
<!doctype html>
<html lang="th">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta name="theme-color" content="#0b1118">
  <title>PlanKO · IMU 3D Monitor & Dataset AI Suite</title>
  <script src="https://cdnjs.cloudflare.com/ajax/libs/paho-mqtt/1.0.1/mqttws31.min.js"></script>
  <script src="imu_protocol.js"></script>
  <style>
    :root {
      color-scheme: dark;
      --bg: #0b1118;
      --panel: #111b25;
      --panel-2: #152230;
      --line: #223444;
      --text: #e9f1f7;
      --muted: #8ea3b3;
      --mint: #74f0c2;
      --blue: #6ca9ff;
      --orange: #ffb86b;
      --red: #ff7777;
      --accent: #205c52;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      min-height: 100vh;
      background:
        radial-gradient(ellipse at 50% -15%, rgba(32, 92, 82, .32), transparent 50%),
        radial-gradient(ellipse at 85% 100%, rgba(30, 60, 95, .25), transparent 50%),
        var(--bg);
      color: var(--text);
      font-family: Inter, ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
    }
    button, input, select { font: inherit; }
    .shell {
      width: min(1320px, calc(100% - 32px));
      margin: 0 auto;
      padding: 24px 0 48px;
    }

    /* Top Line Header */
    .topline {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 16px;
      margin-bottom: 20px;
    }
    .brand { display: flex; align-items: center; gap: 14px; }
    .brand-mark {
      width: 44px; height: 44px;
      display: grid; place-items: center;
      border-radius: 12px;
      color: var(--mint);
      background: rgba(116, 240, 194, .12);
      border: 1px solid rgba(116, 240, 194, .3);
      font-size: 22px; font-weight: 800;
    }
    h1 { margin: 0; font-size: clamp(20px, 2.8vw, 26px); letter-spacing: -.02em; font-weight: 750; }
    .subtitle { margin: 3px 0 0; color: var(--muted); font-size: 13px; }
    .status-badge {
      display: inline-flex; align-items: center; gap: 8px;
      padding: 6px 14px; border-radius: 999px;
      background: #14222f; border: 1px solid var(--line);
      font-size: 12px; font-weight: 600;
    }
    .status-dot { width: 8px; height: 8px; border-radius: 50%; background: #556b7c; }
    .status-dot.online { background: var(--mint); box-shadow: 0 0 10px rgba(116, 240, 194, .7); }
    .status-dot.offline { background: var(--red); box-shadow: 0 0 10px rgba(255, 119, 119, .7); }

    /* Toolbar */
    .toolbar {
      display: flex; flex-wrap: wrap; align-items: center; gap: 10px;
      margin-bottom: 18px; padding: 12px 16px;
      background: var(--panel); border: 1px solid var(--line); border-radius: 12px;
    }
    .field { display: flex; align-items: center; gap: 8px; font-size: 12px; color: var(--muted); }
    .field input, .field select {
      background: #0d151e; color: var(--text); border: 1px solid var(--line);
      border-radius: 8px; padding: 6px 10px; min-height: 34px; font-size: 12px;
    }
    .field select { cursor: pointer; }
    .toolbar-spacer { flex: 1 1 auto; }
    .btn {
      display: inline-flex; align-items: center; justify-content: center; gap: 6px;
      padding: 7px 14px; border-radius: 8px; min-height: 34px;
      border: 1px solid var(--line); background: #182836; color: var(--text);
      font-size: 12px; font-weight: 600; cursor: pointer; transition: all .15s ease;
    }
    .btn:hover { border-color: var(--mint); color: #fff; background: #203547; }
    .btn.primary { background: var(--accent); border-color: #388a7a; color: #fff; }
    .btn.primary:hover { background: #287364; }
    .btn.danger { background: #542226; border-color: #7d333a; color: #ff9d9d; }
    .btn.danger:hover { background: #69282d; }
    .btn:disabled { opacity: .45; cursor: default; border-color: var(--line) !important; background: #182836 !important; }

    /* Main Stage Grid (3D + Sensor Cards) */
    .stage-grid {
      display: grid; grid-template-columns: minmax(0, 1.8fr) minmax(280px, 1fr);
      gap: 16px; margin-bottom: 20px;
    }
    .visual-card {
      background: var(--panel); border: 1px solid var(--line); border-radius: 14px;
      padding: 16px; display: flex; flex-direction: column;
    }
    .visual-head {
      display: flex; justify-content: space-between; align-items: flex-start;
      margin-bottom: 12px;
    }
    .visual-title { font-size: 16px; font-weight: 700; }
    .visual-caption { font-size: 12px; color: var(--muted); margin-top: 2px; }
    .source-label {
      padding: 4px 10px; border-radius: 6px; font-size: 11px; font-weight: 600;
      background: #152533; color: var(--mint); border: 1px solid rgba(116, 240, 194, .2);
    }
    .canvas-wrap {
      position: relative; width: 100%; flex: 1 1 auto; min-height: 420px;
      background: #090e14; border: 1px solid #192734; border-radius: 10px; overflow: hidden;
    }
    canvas { display: block; width: 100%; height: 100%; }
    .canvas-hint {
      position: absolute; left: 12px; bottom: 10px;
      font-size: 11px; color: #4e6575; pointer-events: none;
    }

    /* Side Cards */
    .side-stack { display: flex; flex-direction: column; gap: 14px; }
    .side-card {
      background: var(--panel); border: 1px solid var(--line); border-radius: 14px; padding: 16px;
    }
    .side-heading {
      display: flex; justify-content: space-between; align-items: center;
      margin-bottom: 12px; font-size: 14px; font-weight: 700;
    }
    .sensor-pill {
      font-size: 10px; font-weight: 700; padding: 3px 8px; border-radius: 6px;
      background: #173830; color: var(--mint); border: 1px solid rgba(116, 240, 194, .3);
    }
    .angle-list { display: flex; flex-direction: column; gap: 8px; }
    .angle-row {
      display: flex; align-items: center; justify-content: space-between;
      padding: 8px 12px; background: #0d151e; border: 1px solid #1a2a38; border-radius: 8px;
    }
    .angle-name { display: flex; align-items: center; gap: 8px; font-size: 13px; font-weight: 600; }
    .axis-dot { width: 8px; height: 8px; border-radius: 50%; }
    .axis-x { background: var(--red); }
    .axis-y { background: var(--mint); }
    .axis-z { background: var(--blue); }
    .angle-val { font-size: 18px; font-weight: 700; font-variant-numeric: tabular-nums; }
    .angle-unit { font-size: 11px; color: var(--muted); margin-left: 4px; }

    .vector-row { display: grid; grid-template-columns: repeat(3, 1fr); gap: 6px; margin-top: 8px; }
    .vector-cell {
      padding: 6px 8px; background: #0d151e; border: 1px solid #1a2a38; border-radius: 6px; text-align: center;
    }
    .vector-cell small { display: block; font-size: 10px; color: var(--muted); }
    .vector-cell b { font-size: 12px; font-variant-numeric: tabular-nums; font-weight: 600; color: var(--text); }

    /* Pressure Card */
    .pressure-meter { margin-top: 10px; background: #0d151e; border-radius: 8px; height: 16px; overflow: hidden; border: 1px solid #1c2c39; position: relative; }
    .pressure-fill { height: 100%; width: 0%; background: linear-gradient(90deg, #6ca9ff, var(--mint)); transition: width .15s ease; border-radius: 8px; }
    .pressure-status { margin-top: 8px; display: flex; align-items: center; justify-content: space-between; font-size: 12px; }
    .badge-press { padding: 3px 8px; border-radius: 6px; font-size: 11px; font-weight: 700; background: #1c2c39; color: var(--muted); }
    .badge-press.active { background: #153c32; color: var(--mint); }

    /* Per-node battery telemetry from the Gateway MQTT stream */
    .battery-list { display: grid; gap: 8px; }
    .battery-node { padding: 10px; background: #0d151e; border: 1px solid #1a2a38; border-radius: 8px; }
    .battery-node-head { display: flex; align-items: center; justify-content: space-between; gap: 8px; }
    .battery-node-title { font-size: 12px; font-weight: 700; }
    .battery-node-state { font-size: 10px; color: var(--muted); text-align: right; }
    .battery-node-state.live { color: var(--mint); }
    .battery-node-state.waiting { color: var(--orange); }
    .battery-node-state.invalid { color: var(--orange); }
    .battery-reading { display: flex; align-items: baseline; justify-content: space-between; gap: 8px; margin-top: 7px; font-variant-numeric: tabular-nums; }
    .battery-percent { font-size: 20px; font-weight: 750; }
    .battery-voltage { color: var(--muted); font-size: 12px; }
    .battery-meter { margin-top: 8px; height: 9px; overflow: hidden; background: #172330; border: 1px solid #243646; border-radius: 999px; }
    .battery-fill { height: 100%; width: 0%; background: var(--mint); border-radius: inherit; transition: width .25s ease; }
    .battery-fill.warn { background: var(--orange); }
    .battery-fill.low { background: var(--red); }
    .battery-fill.stale { opacity: .35; }
    .battery-summary { color: var(--muted); font-size: 10px; font-weight: 700; }
    .battery-summary.online { color: var(--mint); }
    .battery-summary.waiting { color: var(--orange); }
    .battery-note { margin: 8px 0 0; color: var(--muted); font-size: 10px; line-height: 1.4; }

    /* ============================================================================== */
    /* DATASET & AI MACHINE LEARNING SECTION */
    /* ============================================================================== */
    .suite-card {
      background: var(--panel); border: 1px solid var(--line); border-radius: 14px;
      padding: 20px; margin-bottom: 20px;
    }
    .suite-tabs {
      display: flex; gap: 8px; border-bottom: 1px solid var(--line); padding-bottom: 12px; margin-bottom: 18px;
    }
    .tab-btn {
      padding: 8px 16px; border-radius: 8px; background: transparent; border: 1px solid transparent;
      color: var(--muted); font-size: 13px; font-weight: 600; cursor: pointer; transition: all .15s ease;
    }
    .tab-btn:hover { color: var(--text); background: rgba(255,255,255,.04); }
    .tab-btn.active { background: #182837; border-color: var(--line); color: var(--mint); }

    .tab-pane { display: none; }
    .tab-pane.active { display: block; }

    /* Dataset Form Controls */
    .rec-grid {
      display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 12px;
      margin-bottom: 14px;
    }
    .rec-field { display: flex; flex-direction: column; gap: 5px; font-size: 12px; color: var(--muted); }
    .rec-field input, .rec-field select {
      background: #0d151e; color: var(--text); border: 1px solid var(--line); border-radius: 8px;
      padding: 8px 10px; font-size: 13px; min-height: 38px;
    }

    /* Phase Selection */
    .phase-bar {
      display: flex; flex-wrap: wrap; align-items: center; gap: 8px;
      padding: 12px; background: #0c141d; border: 1px solid var(--line); border-radius: 10px;
      margin-bottom: 14px;
    }
    .phase-btn {
      padding: 6px 14px; border-radius: 8px; font-size: 12px; font-weight: 600;
      background: #162430; border: 1px solid #233748; color: var(--muted); cursor: pointer;
    }
    .phase-btn.active {
      background: rgba(116, 240, 194, .15); border-color: var(--mint); color: var(--mint);
    }
    .guided-box {
      margin-top: 10px; padding: 10px 14px; background: rgba(20, 36, 48, .6);
      border: 1px solid #1c3242; border-radius: 8px; font-size: 12px;
    }
    .guided-progress-wrap {
      margin-top: 8px; background: #0d151e; height: 10px; border-radius: 5px; overflow: hidden;
    }
    .guided-progress-bar {
      height: 100%; width: 0%; background: linear-gradient(90deg, #6ca9ff, var(--mint));
      transition: width .2s linear;
    }

    .rec-actions {
      display: flex; flex-wrap: wrap; align-items: center; gap: 10px; margin-top: 14px;
    }
    .rec-badge {
      padding: 5px 12px; border-radius: 999px; font-size: 12px; font-weight: 700;
      background: #182836; color: var(--muted);
    }
    .rec-badge.recording {
      background: rgba(255, 119, 119, .2); color: #ff8e8e; animation: pulse 1.2s infinite;
    }
    @keyframes pulse { 0% { opacity: 1; } 50% { opacity: .5; } 100% { opacity: 1; } }

    .download-list {
      display: flex; flex-direction: column; gap: 6px; margin-top: 14px;
    }
    .csv-link {
      display: flex; align-items: center; justify-content: space-between;
      padding: 8px 12px; background: #0c141d; border: 1px solid #192a38; border-radius: 8px;
      color: var(--mint); font-size: 12px; text-decoration: none;
    }
    .csv-link:hover { border-color: var(--mint); background: #101c27; }

    /* AI Live Prediction Grid */
    .ai-grid {
      display: grid; grid-template-columns: repeat(auto-fit, minmax(160px, 1fr)); gap: 12px; margin-top: 12px;
    }
    .ai-card {
      padding: 12px; background: #0d151e; border: 1px solid var(--line); border-radius: 10px;
    }
    .ai-card small { display: block; font-size: 11px; color: var(--muted); margin-bottom: 4px; }
    .ai-card strong { font-size: 18px; font-weight: 750; color: var(--text); overflow-wrap: anywhere; }

    /* Code Snippet Box */
    .code-box {
      background: #090e15; border: 1px solid #192734; border-radius: 8px; padding: 12px 14px;
      font-family: ui-monospace, Menlo, Monaco, Consolas, monospace; font-size: 12px; color: #a4bece;
      line-height: 1.6; overflow-x: auto; margin-top: 10px;
    }

    .bottom-info { margin-top: 14px; font-size: 11px; color: #627787; line-height: 1.6; }
    .bottom-info code { background: #152230; padding: 2px 6px; border-radius: 4px; color: var(--mint); }

    @media (max-width: 960px) {
      .stage-grid { grid-template-columns: 1fr; }
      .canvas-wrap { min-height: 360px; }
    }
  </style>
</head>
<body>
  <main class="shell">
    <!-- Header -->
    <header class="topline">
      <div class="brand">
        <div class="brand-mark" aria-hidden="true">↻</div>
        <div>
          <h1>PlanKO · IMU 3D Monitor & Dataset AI Suite</h1>
          <p class="subtitle">ระบบวิเคราะห์ท่าทาง IMU 20Hz · บันทึก Dataset & จำแนกด้วย Machine Learning</p>
        </div>
      </div>
      <div class="status-badge">
        <i id="mqttDot" class="status-dot"></i>
        <span id="mqttStatusText">กำลังเชื่อมต่อ MQTT…</span>
      </div>
    </header>

    <!-- Toolbar -->
    <section class="toolbar" aria-label="เครื่องมือและการตั้งค่า">
      <div class="field">
        <span>Broker:</span>
        <input type="text" id="brokerHost" value="192.168.1.160" readonly style="width:165px; opacity:.8;">
      </div>
      <div class="field">
        <span>Topic:</span>
        <input type="text" id="topicName" value="planko/imu" readonly style="width:95px; color:var(--mint); font-weight:600;">
      </div>
      <div class="field">
        <span>เลือก Node:</span>
        <select id="nodeSelect" aria-label="เลือก Node">
          <option value="1" selected>Node #1 (Spine / Neck)</option>
          <option value="2">Node #2 (Lower Back)</option>
          <option value="3">Node #3</option>
          <option value="4">Node #4</option>
        </select>
      </div>

      <div class="toolbar-spacer"></div>

      <button id="zeroBtn" class="btn" type="button" title="เซ็ตมุมปัจจุบันเป็น 0°">⌖ เซ็ตศูนย์ (Tare)</button>
      <button id="pauseBtn" class="btn" type="button">⏸ พักสตรีม</button>
      <button id="reconnectBtn" class="btn primary" type="button">🔄 ต่อ MQTT ใหม่</button>
    </section>

    <section class="toolbar" style="margin-top:12px;">
      <label class="field">Gateway ID: <input id="gatewayFilter" placeholder="Auto-select first Gateway" style="width:220px;"></label>
      <span id="mqttRxStats" role="status">RX: 0 | 0.0 Hz | Gaps: 0 | Duplicates: 0</span>
      <span id="sampleContractStatus" role="status">Waiting for BLE samples with six axes</span>
    </section>
    <!-- 3D Visualization & Telemetry Cards -->
    <div class="stage-grid">
      <!-- 3D Canvas Box -->
      <article class="visual-card">
        <div class="visual-head">
          <div>
            <div class="visual-title">ภาพจำลอง 3D Orientation (20Hz)</div>
            <div class="visual-caption">X: สีแดง (Roll) · Y: สีเขียว (Pitch) · Z: สีน้ำเงิน (Yaw)</div>
          </div>
          <div id="sourceLabel" class="source-label">รอข้อมูลจาก planko/imu…</div>
        </div>
        <div class="canvas-wrap">
          <canvas id="scene" width="900" height="520" role="img" aria-label="3D Orientation IMU Canvas"></canvas>
          <div class="canvas-hint">แกนพิกัดโลกคงที่ (World Frame) มุมล่างซ้าย</div>
        </div>
      </article>

      <!-- Side Details Stack -->
      <div class="side-stack">
        <!-- Angles & Rate Card -->
        <aside class="side-card">
          <div class="side-heading">
            <span>มุมการเอียง (Euler Angles)</span>
            <span class="sensor-pill" id="sensorPill">Node #1</span>
          </div>
          <div class="angle-list">
            <div class="angle-row">
              <div class="angle-name"><i class="axis-dot axis-x"></i>Roll (เอียงข้าง)</div>
              <div><span class="angle-val" id="rollValue">0.0</span><span class="angle-unit">°</span></div>
            </div>
            <div class="angle-row">
              <div class="angle-name"><i class="axis-dot axis-y"></i>Pitch (ก้ม/เงย)</div>
              <div><span class="angle-val" id="pitchValue">0.0</span><span class="angle-unit">°</span></div>
            </div>
            <div class="angle-row">
              <div class="angle-name"><i class="axis-dot axis-z"></i>Yaw (หันซ้าย/ขวา)</div>
              <div><span class="angle-val" id="yawValue">0.0</span><span class="angle-unit">°</span></div>
            </div>
          </div>

          <!-- Accelerometer -->
          <div class="side-heading" style="margin-top: 14px; margin-bottom: 6px; font-size: 13px;">
            <strong>ความเร่ง Accelerometer</strong>
            <span id="accelUnit">m/s²</span>
          </div>
          <div class="vector-row">
            <div class="vector-cell"><small>AX</small><b id="axValue">—</b></div>
            <div class="vector-cell"><small>AY</small><b id="ayValue">—</b></div>
            <div class="vector-cell"><small>AZ</small><b id="azValue">—</b></div>
          </div>

          <!-- Stream Meta -->
          <div style="display: flex; justify-content: space-between; margin-top: 12px; font-size: 11px; color: var(--muted);">
            <span>Render: <b id="fpsText" style="color:var(--mint);">0 FPS</b></span>
            <span>Seq: <b id="seqText" style="color:var(--text);">—</b></span>
            <span>Uptime: <b id="uptimeText" style="color:var(--text);">—</b></span>
          </div>
        </aside>

        <!-- Pressure Sensor Card (Real Sensor) -->
        <aside class="side-card">
          <div class="side-heading">
            <span>เซนเซอร์วัดแรงกด (Pressure Sensor)</span>
            <span class="badge-press" id="pressBadge">RELEASED</span>
          </div>

          <div style="display:flex; justify-content:space-between; align-items:baseline; margin-top:6px;">
            <span style="color:var(--muted); font-size:12px;">Analog ADC Raw:</span>
            <span style="font-size:22px; font-weight:750; font-variant-numeric:tabular-nums;" id="pressRaw">0</span>
          </div>

          <div class="pressure-meter">
            <div class="pressure-fill" id="pressFill"></div>
          </div>

          <div class="pressure-status">
            <span style="color:var(--muted);">Voltage: <b id="pressVolt" style="color:var(--text);">0.00 V</b></span>
            <span style="color:var(--muted);">ช่วงสเกล: <b>0 - 4095 (12-bit)</b></span>
          </div>
          <div id="pressSource" style="margin-top:6px; color:var(--muted); font-size:11px;">Waiting for pressure data</div>
        </aside>
        <aside class="side-card battery-card">
          <div class="side-heading">
            <span>Node Battery</span>
            <span id="batterySummary" class="battery-summary">Waiting for MQTT</span>
          </div>
          <div id="nodeBatteryList" class="battery-list" aria-live="polite"></div>
          <p class="battery-note">mV is the latest ADC reading reported by the Gateway. Percent is an estimate for a 1-cell battery.</p>
        </aside>
      </div>
    </div>

    <!-- ============================================================================== -->
    <!-- DATASET RECORDER & MACHINE LEARNING SUITE -->
    <!-- ============================================================================== -->
    <section class="suite-card" aria-label="Dataset & AI Machine Learning Suite">
      <div class="suite-tabs">
        <button class="tab-btn active" data-tab="tab-record" type="button">📂 บันทึกชุดข้อมูล Dataset (CSV Recorder)</button>
        <button class="tab-btn" data-tab="tab-ai" type="button">🧠 ทดสอบโมเดล AI (Live Model Inference)</button>
        <button class="tab-btn" data-tab="tab-cli" type="button">📖 คำสั่ง Train & Serve โมเดล (CLI Guide)</button>
      </div>

      <!-- Tab 1: Dataset Recorder -->
      <div id="tab-record" class="tab-pane active">
        <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:12px;">
          <div>
            <strong style="font-size:15px;">บันทึกข้อมูลท่าทาง (IMU Dataset Recorder)</strong>
            <p style="margin:2px 0 0; color:var(--muted); font-size:12px;">IMU 20 Hz ต่อ Node · Gateway รวมทุก Node เป็น MQTT 1 ข้อความทุก 50 ms ผ่าน Mosquitto · CSV แยก Node สำหรับ train</p>
          </div>
          <span id="recStatusPill" class="rec-badge">พร้อมบันทึก (Ready)</span>
        </div>

        <!-- Form Grid -->
        <div class="rec-grid">
          <label class="rec-field">ผู้ทดสอบ (Subject ID)
            <input type="text" id="recSubjectId" value="P01" maxlength="30">
          </label>
          <label class="rec-field">Session ID
            <input type="text" id="recSessionId" value="S01" maxlength="30">
          </label>
          <label class="rec-field">Trial ID
            <input type="text" id="recTrialId" value="trial-01" maxlength="30">
          </label>
        </div>

        <div class="guided-box" style="border-color:rgba(116,240,194,.45);">
          <strong>บันทึก Node หลังบนและหลังล่างพร้อมกัน</strong>
          <p style="font-size:12px;color:var(--muted);">เลือก IMU สองตำแหน่งและเลือก Case/Action Label เพียงครั้งเดียว ทุก Node จะได้ label เดียวกัน แล้วกดเริ่มครั้งเดียวเพื่อบันทึกพร้อมกัน</p>
          <div class="rec-grid">
            <label class="rec-field">Node ที่ติดตั้งหลังบน
              <select id="recUpperNodeSelect" aria-label="เลือก Node หลังบน">
                <option value="">รอ Node...</option>
              </select>
            </label>
            <label class="rec-field">Node ที่ติดตั้งหลังล่าง
              <select id="recLowerNodeSelect" aria-label="เลือก Node หลังล่าง">
                <option value="">รอ Node...</option>
              </select>
            </label>
            <label class="rec-field">Case / Action Label (ใช้ร่วมกันทุก Node)
              <select id="sharedActionLabel" aria-label="เลือก Case หรือ Action Label ร่วมกัน">
                <option value="">เลือกท่าของ case...</option>
                <option value="case_neutral">ท่าปกติ (neutral)</option>
                <option value="case_flexion">ก้ม/งอ (flexion)</option>
                <option value="case_extension">แอ่น/เหยียด (extension)</option>
                <option value="case_lateral_left">เอียงซ้าย (lateral left)</option>
                <option value="case_lateral_right">เอียงขวา (lateral right)</option>
              </select>
            </label>
          </div>
          <p id="recordPairStatus" role="status" style="margin:0;font-size:12px;color:var(--muted);">รอ IMU จริงสอง Node จาก Gateway</p>
        </div>

        <div class="guided-box">
          <strong>สถานะ Node ใน Case เดียวกัน</strong>
          <p style="font-size:12px;color:var(--muted);">ทุก Node ที่เลือกใช้ Case/Action Label เดียวกัน; ตำแหน่งและ UID ของแต่ละ sensor ยังคงอ่านจาก firmware</p>
          <div id="recNodeList" class="rec-grid"><span>รอข้อมูล IMU จริงจาก Gateway…</span></div>
          <p style="font-size:12px;color:var(--muted);">ข้อมูลอยู่ในหน่วยความจำหน้าเว็บจนกว่าจะดาวน์โหลด CSV · back ที่ติด lumbar จะเก็บเป็น body_part=lumbar ตาม taxonomy สำหรับ train และเก็บชื่อเดิมใน firmware_body_part</p>
          <label style="font-size:12px;"><input type="checkbox" id="recPhaseReviewed"> ยืนยันว่าได้ตรวจ Phase แบบ manual แล้ว (Guided Timer จะบันทึกเป็น cue ที่ยังไม่ review)</label>
        </div>

        <!-- Phase Selector Bar -->
        <div class="phase-bar">
          <span style="font-size:12px; color:var(--muted); margin-right:4px;">Phase ปัจจุบัน:</span>
          <button class="phase-btn active" data-phase="neutral" type="button">neutral (ปกติ)</button>
          <button class="phase-btn" data-phase="approaching" type="button">approaching (กำลังเข้าท่า)</button>
          <button class="phase-btn" data-phase="holding" type="button">holding (ค้างในท่า)</button>
          <button class="phase-btn" data-phase="returning" type="button">returning (กำลังกลับ)</button>
          <span id="currentPhaseLabel" style="font-size:12px; color:var(--mint); margin-left:auto; font-weight:700;">Phase: neutral</span>
        </div>

        <!-- Guided Phase Timer Box -->
        <div class="guided-box">
          <label style="display:flex; align-items:center; gap:8px; cursor:pointer;">
            <input type="checkbox" id="guidedTimerCheck">
            <b>ใช้ตัวช่วยนำจังหวะอัตโนมัติ (Guided Phase Timer)</b> พร้อมเสียงแจ้งเตือน Beep · ไม่เลือก = บันทึกต่อเนื่องจนกดหยุดเอง
          </label>
          <p style="margin:4px 0 0; color:var(--muted); font-size:11px;">
            ลำดับเริ่มต้น: neutral 3s → approaching 3s → holding 5s → returning 3s → neutral 2s (ระบบจะเปลี่ยน Phase และจบให้อัตโนมัติ)
          </p>
          <div class="guided-progress-wrap">
            <div id="guidedProgressBar" class="guided-progress-bar"></div>
          </div>
          <div id="guidedText" style="margin-top:6px; font-size:11px; color:var(--text); font-variant-numeric:tabular-nums;">
            เวลานำจังหวะ: รอเริ่มการบันทึก
          </div>
        </div>

        <!-- Actions -->
        <div class="rec-actions">
          <button id="recStartBtn" class="btn primary" type="button">🔴 เริ่มบันทึก 2 Node พร้อมกัน</button>
          <button id="recStopBtn" class="btn danger" type="button" disabled>⏹ หยุดและดาวน์โหลด CSV (Stop & Save)</button>
          <button id="recClearBtn" class="btn" type="button">ล้างรายการ</button>
          <button id="recDownloadAllBtn" class="btn" type="button" disabled>ดาวน์โหลดทุกไฟล์</button>
          <span id="sampleCounterText" style="margin-left:auto; font-size:12px; color:var(--muted); font-variant-numeric:tabular-nums;">0 ตัวอย่าง (0.0s)</span>
        </div>

        <!-- Download List -->
        <div id="recDownloadList" class="download-list"></div>
      </div>

      <!-- Tab 2: AI Live Model Inspector -->
      <div id="tab-ai" class="tab-pane">
        <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:12px;">
          <div>
            <strong style="font-size:15px;">ทดสอบผลการทำนายด้วย AI โมเดลสด (Live Pose Classifier)</strong>
            <p style="margin:2px 0 0; color:var(--muted); font-size:12px;">เชื่อมต่อไปยัง Local Inference Server (พอร์ต 8765) จาก <code>tools/serve_model.py</code></p>
          </div>
          <span id="aiServerBadge" class="rec-badge">ตรวจสอบสถานะเซิร์ฟเวอร์…</span>
        </div>

        <div style="display:flex; flex-wrap:wrap; align-items:center; gap:10px; margin-bottom:14px;">
          <label class="field">Inference Host:
            <input type="text" id="aiHost" value="http://127.0.0.1:8765" style="width:180px;">
          </label>
          <button id="aiToggleBtn" class="btn primary" type="button">▶ เริ่มตรวจจับสด (Live Inference)</button>
          <a href="../model_test.html" target="_blank" class="btn" style="text-decoration:none;">↗ เปิดหน้า Model Testbench เต็มรูปแบบ</a>
        </div>

        <div class="rec-grid" style="margin:0 0 14px;">
          <label class="rec-field">ตำแหน่งสำหรับ AI (Mount Site)
            <select id="recSiteSelect"></select>
          </label>
          <label class="rec-field">ส่วนร่างกายสำหรับ AI (Body Part)
            <select id="recBodyPartSelect"></select>
          </label>
          <label class="rec-field">ข้าง (Side)
            <select id="recSideSelect">
              <option value="right">ขวา (Right)</option>
              <option value="left">ซ้าย (Left)</option>
              <option value="unknown" selected>กึ่งกลาง / ไม่ระบุ (Center/Unknown)</option>
            </select>
          </label>
        </div>
        <p style="margin:-6px 0 14px;color:var(--muted);font-size:12px;">ค่าตำแหน่งชุดนี้ใช้กับ AI Live Inference เท่านั้น; Dataset Recorder อ่านตำแหน่งและ UID ของแต่ละ Node จาก Gateway</p>

        <p id="aiModelContract" style="color:var(--muted);">Load model metadata to see supported body sites and features.</p>
        <div class="guided-box" style="margin:14px 0;">
          <strong>PlanKO Plank Phase Pilot · IMU 6-axis</strong>
          <p style="margin:4px 0 10px;color:var(--muted);font-size:12px;">
            Uses two real IMUs (upper trunk + lumbar/pelvis), mounted with matching sensor axes, a 2-second correct-plank baseline, and a rule-based trend state. This pilot is not a trained or clinically validated classifier.
          </p>
          <div style="display:flex;flex-wrap:wrap;align-items:center;gap:10px;">
            <label class="field">Upper trunk node:
              <select id="plankUpperNode" aria-label="Upper trunk IMU node">
                <option value="1">Node #1</option><option value="2">Node #2</option>
                <option value="3">Node #3</option><option value="4">Node #4</option>
              </select>
            </label>
            <label class="field">Lumbar / pelvis node:
              <select id="plankLowerNode" aria-label="Lumbar or pelvis IMU node">
                <option value="1">Node #1</option><option value="2" selected>Node #2</option>
                <option value="3">Node #3</option><option value="4">Node #4</option>
              </select>
            </label>
            <button id="plankMonitorBtn" class="btn primary" type="button">Start plank monitor</button>
            <button id="plankCalibrateBtn" class="btn" type="button" disabled>Calibrate correct plank (2 s)</button>
          </div>
          <p id="plankSensorStatus" role="status" style="margin:10px 0 0;font-size:12px;color:var(--muted);">
            Select two connected IMUs. Expected mount sites: chest/upper_back and lumbar/pelvis.
          </p>
          <div class="ai-grid">
            <div class="ai-card"><small>Paired plank phase</small><strong id="plankOutPhase">unknown</strong></div>
            <div class="ai-card"><small>Largest angular deviation</small><strong id="plankOutError">—</strong></div>
            <div class="ai-card"><small>Normalized error E</small><strong id="plankOutScore">—</strong></div>
            <div class="ai-card"><small>Trend V (E/s)</small><strong id="plankOutTrend">—</strong></div>
            <div class="ai-card"><small>Relative pitch delta</small><strong id="plankOutPitch">—</strong></div>
            <div class="ai-card"><small>Relative roll delta</small><strong id="plankOutRoll">—</strong></div>
            <div class="ai-card"><small>Upper trunk · Node phase / E / V</small><strong id="plankOutUpper">unknown</strong><small id="plankUpperMeta">—</small></div>
            <div class="ai-card"><small>Lumbar / pelvis · Node phase / E / V</small><strong id="plankOutLower">unknown</strong><small id="plankLowerMeta">—</small></div>
          </div>
          <p id="plankCapabilityText" style="margin:8px 0 0;font-size:11px;color:var(--muted);">
            Six-axis pilot uses fused pitch/roll and ignores yaw. E and V are deviation indicators, not Accuracy %. Accuracy requires labeled trials. The 12° score scales are provisional. Load distribution needs separate arm and foot force sensors; this Gateway has one pressure channel.
          </p>
          <details class="guided-box">
            <summary style="cursor:pointer;font-weight:700;">Record labeled Plank trials for Accuracy evaluation</summary>
            <p style="font-size:11px;color:var(--muted);">Annotate the reference phase separately for the paired output and each Node while recording. Review labels against video before using the resulting percentage.</p>
            <div style="display:flex;flex-wrap:wrap;align-items:center;gap:10px;">
              <label class="field">Subject ID: <input id="plankEvalSubject" value="P01" maxlength="30" style="width:90px;"></label>
              <label class="field">Session ID: <input id="plankEvalSession" value="S01" maxlength="30" style="width:90px;"></label>
              <label class="field">Trial ID: <input id="plankEvalTrial" value="plank-trial-01" maxlength="40" style="width:130px;"></label>
              <label class="field">Pair truth:
                <select id="plankTruthPair"><option value="unlabeled">Unlabeled</option><option value="normal">Normal</option><option value="phase1_approaching_error">Phase 1</option><option value="phase2_incorrect_posture">Phase 2</option><option value="phase3_recovery_trend">Phase 3</option></select>
              </label>
              <label class="field">Upper Node truth:
                <select id="plankTruthUpper"><option value="unlabeled">Unlabeled</option><option value="normal">Normal</option><option value="phase1_approaching_error">Phase 1</option><option value="phase2_incorrect_posture">Phase 2</option><option value="phase3_recovery_trend">Phase 3</option></select>
              </label>
              <label class="field">Lower Node truth:
                <select id="plankTruthLower"><option value="unlabeled">Unlabeled</option><option value="normal">Normal</option><option value="phase1_approaching_error">Phase 1</option><option value="phase2_incorrect_posture">Phase 2</option><option value="phase3_recovery_trend">Phase 3</option></select>
              </label>
              <button id="plankEvalStartBtn" class="btn primary" type="button" disabled>Start labeled capture</button>
              <button id="plankEvalStopBtn" class="btn" type="button" disabled>Stop & download CSV</button>
              <span id="plankEvalCount" role="status">0 labeled prediction rows</span>
            </div>
            <div id="plankEvalDownloadList" class="download-list"></div>
          </details>
        </div>
        <div class="ai-grid">
          <div class="ai-card">
            <small>ท่าทางที่ทำนาย (Predicted Action)</small>
            <strong id="aiOutAction" style="color:var(--mint);">—</strong>
          </div>
          <div class="ai-card">
            <small>ส่วนของร่างกาย (Body Part)</small>
            <strong id="aiOutBody">—</strong>
          </div>
          <div class="ai-card">
            <small>ช่วงการเคลื่อนไหว (Movement Stage)</small>
            <strong id="aiOutStage" style="color:var(--blue);">—</strong>
          </div>
          <div class="ai-card">
            <small>ความมั่นใจ (Confidence Score)</small>
            <strong id="aiOutScore">—</strong>
          </div>
          <div class="ai-card">
            <small>เวลาที่ค้างในท่า (Held Time)</small>
            <strong id="aiOutHeld">0 ms</strong>
          </div>
          <div class="ai-card">
            <small>สถานะ Calibration / โมเดล</small>
            <strong id="aiOutCal" style="font-size:13px;">—</strong>
          </div>
        </div>

        <p id="aiReasonText" style="margin-top:10px; font-size:12px; color:var(--muted);">
          เปิดรันเซิร์ฟเวอร์ด้วย <code>python tools/serve_model.py --port 8765</code> เพื่อเริ่มตรวจจับท่าทางแบบ Real-time
        </p>
      </div>

      <!-- Tab 3: CLI Guide -->
      <div id="tab-cli" class="tab-pane">
        <strong style="font-size:15px;">คู่มือการติดตั้งและ Train โมเดลด้วย Python CLI</strong>
        <p style="margin:4px 0 8px; color:var(--muted); font-size:12px;">ทำตาม 3 ขั้นตอนดังนี้เพื่อสร้าง AI ตรวจจับท่าทางจากไฟล์ CSV ที่บันทึก:</p>

        <div style="margin-top:12px;">
          <b style="font-size:13px; color:var(--mint);">1. ติดตั้ง Library สำหรับ Machine Learning:</b>
          <div class="code-box">pip install -r requirements-ml.txt</div>
        </div>

        <div style="margin-top:14px;">
          <b style="font-size:13px; color:var(--mint);">2. สั่ง Train โมเดลด้วยข้อมูล CSV (Random Forest / SVM Classifier):</b>
          <div class="code-box">python tools/train_model.py whist/*.csv --window 1.0 --output models/pose_model</div>
        </div>

        <div style="margin-top:14px;">
          <b style="font-size:13px; color:var(--mint);">3. เปิด Local Inference API Server เพื่อตรวจจับสดหรือ Replay:</b>
          <div class="code-box">python tools/serve_model.py --model models/pose_model/model_bundle.joblib --port 8765</div>
        </div>
      </div>
    </section>

    <!-- Bottom Note -->
    <p class="bottom-info">
      💡 โครงสร้างชุดข้อมูลและโมเดลอิงตามสถาปัตยกรรม <code>Arduino Ble 33 / PlanKO</code> โดยดึงคุณลักษณะ (Features) ความถี่ 20Hz 
      ครอบคลุม Mean, Std, RMS, Range, Slope, Vector Magnitude และ Mount-Site Isolation เพื่อให้โมเดลแยกแยะท่าทางได้อย่างแม่นยำ
    </p>
  </main>

  <script>
    "use strict";

    // DOM Elements
    const $ = (id) => document.getElementById(id);
    const canvas = $("scene");
    const ctx = canvas.getContext("2d");
    const mqttDot = $("mqttDot");
    const mqttStatusText = $("mqttStatusText");
    const nodeSelect = $("nodeSelect");
    const zeroBtn = $("zeroBtn");
    const pauseBtn = $("pauseBtn");
    const reconnectBtn = $("reconnectBtn");
    const sourceLabel = $("sourceLabel");
    const sensorPill = $("sensorPill");
    const rollValue = $("rollValue");
    const pitchValue = $("pitchValue");
    const yawValue = $("yawValue");
    const axValue = $("axValue");
    const ayValue = $("ayValue");
    const azValue = $("azValue");
    const pressRaw = $("pressRaw");
    const pressVolt = $("pressVolt");
    const pressFill = $("pressFill");
    const pressBadge = $("pressBadge");
    const pressSource = $("pressSource");
    const rfp605ChannelStates = new Map();
    const nodeBatteryList = $("nodeBatteryList");
    const batterySummary = $("batterySummary");
    const fpsText = $("fpsText");
    const seqText = $("seqText");
    const uptimeText = $("uptimeText");

    const DEG = Math.PI / 180;
    let mqttClient = null;
    let isPaused = false;
    let selectedNodeId = 1;
    const nodeBatteryStates = new Map([1, 2].map(nodeId => [nodeId, {
      nodeId, connected: false, lastNodeAt: 0, batteryReceived: false, batteryValid: false,
      batteryPercent: null, batteryMv: null, lastBatteryAt: 0, mountSite: ""
    }]));

    // Orientation state with tare offsets
    let tareRoll = 0, tarePitch = 0, tareYaw = 0;
    let targetRoll = 0, targetPitch = 0, targetYaw = 0;
    let currentRoll = 0, currentPitch = 0, currentYaw = 0;

    const inferenceWindow = new PlanKOImu.SampleWindow();
    let latestSample = null;
    let lastSampleArrival = 0;
    let modelMetadata = null;
    let lastInferenceKey = "";
    let plankMonitorActive = false;
    let plankCalibrating = false;
    let plankCalibrated = false;
    let plankCalibrationFrames = [];
    let plankCalibrationStartedAt = 0;
    let plankLastPairSeq = null;
    let plankLastPairSignature = "";
    let plankRuntimeKey = "";
    let plankRequestInFlight = false;
    let plankRequestSerial = 0;
    const plankPageSession = `${Date.now().toString(16)}-${Math.random().toString(16).slice(2, 8)}`;
    let plankEvaluationRecording = false;
    let plankEvaluationRows = [];

    // -------------------------------------------------------------
    // POSE & TAXONOMY CONFIG (From pose_labels.json)
    // -------------------------------------------------------------
    const POSE_TAXONOMY = {
      mount_sites: {
        upper_back: "หลังส่วนบน (upper_back)",
        wrist: "หลังมือ / ข้อมือ (wrist)",
        neck: "ลำคอ (neck)",
        lumbar: "หลังส่วนล่าง / เอว (lumbar)",
        pelvis: "เชิงกราน / สะโพก (pelvis)",
        chest: "หน้าอก (chest)",
        head: "ศีรษะ (head)",
        right_forearm: "ท่อนแขนขวา (right_forearm)",
        left_forearm: "ท่อนแขนซ้าย (left_forearm)",
        right_upper_arm: "ต้นแขนขวา (right_upper_arm)",
        left_upper_arm: "ต้นแขนซ้าย (left_upper_arm)",
        other: "อื่นๆ (other)"
      },
      body_parts: {
        back: {
          label: "หลัง (Back)",
          actions: [
            { id: "case_neutral", label: "ท่าปกติ (neutral)" },
            { id: "case_flexion", label: "ก้ม/งอ (flexion)" },
            { id: "case_extension", label: "แอ่น/เหยียด (extension)" }
          ]
        },
        neck: {
          label: "ลำคอ (Neck)",
          actions: [
            { id: "neck_neutral", label: "คอตรงปกติ (neutral)" },
            { id: "neck_flexion", label: "ก้มคอ (flexion)" },
            { id: "neck_extension", label: "เงยคอ (extension)" },
            { id: "neck_lateral_flexion_left", label: "เอียงคอซ้าย (lateral_left)" },
            { id: "neck_lateral_flexion_right", label: "เอียงคอขวา (lateral_right)" }
          ]
        },
        lumbar: {
          label: "หลัง / เอว (Lumbar Spine)",
          actions: [
            { id: "lumbar_neutral", label: "หลังตรงปกติ (neutral)" },
            { id: "lumbar_flexion", label: "ก้มหลัง / หลังโก่ง (flexion)" },
            { id: "lumbar_extension", label: "แอ่นหลัง (extension)" },
            { id: "lumbar_lateral_flexion_left", label: "เอียงตัวซ้าย (lateral_left)" },
            { id: "lumbar_lateral_flexion_right", label: "เอียงตัวขวา (lateral_right)" }
          ]
        },
        wrist: {
          label: "ข้อมือ (Wrist)",
          actions: [
            { id: "wrist_neutral", label: "วางปกติ (neutral)" },
            { id: "wrist_up", label: "งัดขึ้น (wrist_up)" },
            { id: "wrist_down", label: "งัดลง (wrist_down)" }
          ]
        },
        pelvis: {
          label: "เชิงกราน (Pelvis)",
          actions: [
            { id: "pelvis_neutral", label: "เชิงกรานปกติ (neutral)" },
            { id: "pelvis_anterior_tilt", label: "กระดกหน้า (anterior_tilt)" },
            { id: "pelvis_posterior_tilt", label: "กระดกหลัง (posterior_tilt)" }
          ]
        },
        elbow: {
          label: "ข้อศอก (Elbow)",
          actions: [
            { id: "elbow_raise_arm", label: "ยกแขน" },
            { id: "elbow_release_arm", label: "ปล่อยแขน" }
          ]
        }
      }
    };

    // Populate Select Dropdowns
    const recSiteSelect = $("recSiteSelect");
    const recBodyPartSelect = $("recBodyPartSelect");

    Object.entries(POSE_TAXONOMY.mount_sites).forEach(([k, v]) => {
      const opt = document.createElement("option");
      opt.value = k; opt.textContent = v;
      recSiteSelect.appendChild(opt);
    });

    Object.entries(POSE_TAXONOMY.body_parts).forEach(([k, v]) => {
      const opt = document.createElement("option");
      opt.value = k; opt.textContent = v.label;
      recBodyPartSelect.appendChild(opt);
    });

    function syncInferenceMountSite() {
      const selectedBody = recBodyPartSelect.value;
      if (POSE_TAXONOMY.mount_sites[selectedBody]) {
        recSiteSelect.value = selectedBody;
      }
    }
    recBodyPartSelect.addEventListener("change", syncInferenceMountSite);
    recBodyPartSelect.value = "neck";
    syncInferenceMountSite();

    // -------------------------------------------------------------
    // DATASET RECORDING LOGIC
    // -------------------------------------------------------------
    let isRecording = false;
    const datasetRecorder = new PlanKOImu.MultiNodeRecorder();
    const captureNodes = new Map();
    const captureDownloads = [];
    let recordStartTime = 0;
    let currentPhase = "neutral";
    let guidedStepIndex = 0;
    let guidedTimerId = null;
    let audioCtx = null;

    const GUIDED_SEQUENCE = [
      { phase: "neutral", duration: 3.0 },
      { phase: "approaching", duration: 3.0 },
      { phase: "holding", duration: 5.0 },
      { phase: "returning", duration: 3.0 },
      { phase: "neutral", duration: 2.0 }
    ];

    function playBeep(freq = 880, duration = 0.12) {
      try {
        if (!audioCtx) audioCtx = new (window.AudioContext || window.webkitAudioContext)();
        const osc = audioCtx.createOscillator();
        const gain = audioCtx.createGain();
        osc.frequency.value = freq;
        osc.connect(gain);
        gain.connect(audioCtx.destination);
        gain.gain.setValueAtTime(0.15, audioCtx.currentTime);
        gain.gain.exponentialRampToValueAtTime(0.001, audioCtx.currentTime + duration);
        osc.start();
        osc.stop(audioCtx.currentTime + duration);
      } catch(e) {}
    }

    // Phase Selection buttons
    const phaseBtns = document.querySelectorAll(".phase-btn");
    phaseBtns.forEach(btn => {
      btn.addEventListener("click", () => {
        setPhase(btn.dataset.phase);
      });
    });

    function setPhase(phase) {
      currentPhase = phase;
      phaseBtns.forEach(b => b.classList.toggle("active", b.dataset.phase === phase));
      $("currentPhaseLabel").textContent = "Phase: " + phase;
    }

    // Tabs logic
    const tabBtns = document.querySelectorAll(".tab-btn");
    tabBtns.forEach(btn => {
      btn.addEventListener("click", () => {
        tabBtns.forEach(b => b.classList.remove("active"));
        document.querySelectorAll(".tab-pane").forEach(p => p.classList.remove("active"));
        btn.classList.add("active");
        $(btn.dataset.tab).classList.add("active");
      });
    });

    // Start / Stop Recording
    const recStartBtn = $("recStartBtn");
    const recStopBtn = $("recStopBtn");
    const recStatusPill = $("recStatusPill");
    const sampleCounterText = $("sampleCounterText");
    const guidedTimerCheck = $("guidedTimerCheck");
    const guidedProgressBar = $("guidedProgressBar");
    const guidedText = $("guidedText");
    const recUpperNodeSelect = $("recUpperNodeSelect");
    const recLowerNodeSelect = $("recLowerNodeSelect");
    const sharedActionLabel = $("sharedActionLabel");
    const recNodeGrid = recUpperNodeSelect.parentElement?.parentElement || document.createElement("div");
    const recModeField = document.createElement("label");
    recModeField.className = "rec-field";
    recModeField.append(document.createTextNode("Recording mode"));
    const recModeSelect = document.createElement("select");
    recModeSelect.id = "recModeSelect";
    recModeSelect.append(
      new Option("All live Nodes (ทุก Node พร้อมกัน)", "all"),
      new Option("Upper + lower pair (คู่หลังบน + หลังล่าง)", "pair"),
      new Option("Single Node (1 Node)", "single")
    );
    recModeSelect.value = "all";
    recModeField.append(recModeSelect);
    const recSingleNodeField = document.createElement("label");
    recSingleNodeField.className = "rec-field";
    recSingleNodeField.append(document.createTextNode("Node to record"));
    const recSingleNodeSelect = document.createElement("select");
    recSingleNodeSelect.id = "recSingleNodeSelect";
    recSingleNodeSelect.append(new Option("Waiting for Node...", ""));
    recSingleNodeField.append(recSingleNodeSelect);
    recNodeGrid.prepend(recModeField, recSingleNodeField);
    const recUpperNodeField = recUpperNodeSelect.closest?.("label") || recUpperNodeSelect.parentElement || document.createElement("label");
    const recLowerNodeField = recLowerNodeSelect.closest?.("label") || recLowerNodeSelect.parentElement || document.createElement("label");
    let recordingNodeIds = new Set();
    const NODE_HOLD_MS = 2000;

    function liveCaptureNodeEntries() {
      const byNodeId = new Map();
      for (const entry of captureNodes.values()) {
        if (!entry.lastValidNode || Date.now() - entry.lastValidAt >= NODE_HOLD_MS) continue;
        byNodeId.set(Number(entry.node.nodeId), entry);
      }
      return [...byNodeId.values()].sort((a, b) => Number(a.node.nodeId) - Number(b.node.nodeId));
    }

    function refreshRecordPairOptions() {
      if (isRecording) return;
      const entries = liveCaptureNodeEntries();
      const nodes = entries.map(entry => entry.node);
      const fill = (select, preferredSites, fallbackId, excludeId) => {
        const previous = select.value;
        const available = nodes.filter(node => Number(node.nodeId) !== Number(excludeId));
        select.replaceChildren(new Option("เลือก Node...", ""));
        for (const node of nodes) {
          const context = (node.mount_site || "site?") + " / " + (PlanKOImu.trainingBody(node) || "body?");
          select.appendChild(new Option("Node #" + node.nodeId + " · " + context, String(node.nodeId)));
        }
        const preserved = available.find(node => String(node.nodeId) === previous);
        const preferred = available.find(node => preferredSites.includes(node.mount_site)) ||
          available.find(node => Number(node.nodeId) === fallbackId) || available[0];
        select.value = String((preserved || preferred)?.nodeId || "");
      };
      fill(recSingleNodeSelect, [], Number(nodeSelect.value) || 1, 0);
      fill(recUpperNodeSelect, ["upper_back", "chest"], 1, 0);
      fill(recLowerNodeSelect, ["lumbar", "pelvis"], 2, Number(recUpperNodeSelect.value) || 0);
      updateRecordPairStatus();
    }

    function updateRecordModeLayout() {
      const mode = recModeSelect.value;
      const singleMode = mode === "single";
      const pairMode = mode === "pair";
      recSingleNodeField.hidden = !singleMode;
      recUpperNodeField.hidden = !pairMode;
      recLowerNodeField.hidden = !pairMode;
      recStartBtn.textContent = mode === "all"
        ? "🔴 เริ่มบันทึกทุก Node พร้อมกัน"
        : singleMode
          ? "🔴 เริ่มบันทึก 1 Node"
          : "🔴 เริ่มบันทึก 2 Node พร้อมกัน (หลังบน + หลังล่าง)";
      const heading = recNodeGrid.parentElement?.querySelector?.("strong");
      const help = recNodeGrid.previousElementSibling;
      if (heading) heading.textContent = "IMU recording setup";
      if (help) help.textContent = "เลือกบันทึกทุก Node พร้อมกัน (เช่น Node 1 + 2 + 3), คู่หลังบน/หลังล่าง, หรือ 1 Node โดยทุก Node ที่เลือกจะใช้ Case/Action Label เดียวกัน";
      updateRecordPairStatus();
    }

    function updateRecordPairStatus() {
      const entries = liveCaptureNodeEntries();
      const byId = new Map(entries.map(entry => [Number(entry.node.nodeId), entry]));
      const status = $("recordPairStatus");
      if (recModeSelect.value === "all") {
        if (!entries.length) {
          status.textContent = "รอข้อมูล IMU จริงจาก Gateway ก่อนเริ่มบันทึก";
          status.style.color = "var(--orange)";
          return;
        }
        const summary = entries.map(entry => {
          const nodeState = entry.present && entry.currentValid ? "LIVE" : "HOLD";
          return `Node #${entry.node.nodeId} ${nodeState} (${entry.node.mount_site || "site?"})`;
        }).join(" + ");
        status.textContent = `พร้อมบันทึกพร้อมกัน ${entries.length} Node: ${summary} · Case: ${sharedActionLabel.value || "ยังไม่เลือก"}`;
        status.style.color = sharedActionLabel.value ? "var(--mint)" : "var(--orange)";
        return;
      }
      if (recModeSelect.value === "single") {
        const entry = byId.get(Number(recSingleNodeSelect.value));
        if (!entry) {
          status.textContent = "Select one live Node from the list to start recording.";
          status.style.color = "var(--orange)";
          return;
        }
        const nodeState = entry.present && entry.currentValid ? "LIVE" : "HOLD";
        status.textContent = `Ready to record Node #${entry.node.nodeId} (${entry.node.mount_site || "site?"}) · ${nodeState} · Case: ${sharedActionLabel.value || "not selected"}`;
        status.style.color = sharedActionLabel.value ? "var(--mint)" : "var(--orange)";
        return;
      }
      const upper = byId.get(Number(recUpperNodeSelect.value));
      const lower = byId.get(Number(recLowerNodeSelect.value));
      if (!upper || !lower || upper === lower) {
        status.textContent = "ต้องมี IMU จริงสอง Node ที่แยกกัน ก่อนเริ่มบันทึก";
        status.style.color = "var(--orange)";
        return;
      }
      const upperSite = upper.node.mount_site || "ไม่ระบุตำแหน่ง";
      const lowerSite = lower.node.mount_site || "ไม่ระบุตำแหน่ง";
      const warnings = [];
      if (!["upper_back", "chest"].includes(upper.node.mount_site)) warnings.push("หลังบนใช้ firmware site=" + upperSite);
      if (!["lumbar", "pelvis"].includes(lower.node.mount_site)) warnings.push("หลังล่างใช้ firmware site=" + lowerSite);
      if (!sharedActionLabel.value) warnings.push("เลือก Case/Action Label กลางก่อนเริ่ม");
      const upperState = upper.present && upper.currentValid ? "LIVE" : "HOLD";
      const lowerState = lower.present && lower.currentValid ? "LIVE" : "HOLD";
      status.textContent = "พร้อมบันทึกพร้อมกัน: หลังบน Node #" + upper.node.nodeId + " " + upperState + " (" + upperSite + ") + หลังล่าง Node #" + lower.node.nodeId + " " + lowerState + " (" + lowerSite + ") · Case: " + (sharedActionLabel.value || "ยังไม่เลือก") +
        (warnings.length ? " · ตรวจสอบ: " + warnings.join("; ") : "");
      status.style.color = warnings.length ? "var(--orange)" : "var(--mint)";
    }

    recUpperNodeSelect.addEventListener("change", () => {
      refreshRecordPairOptions();
      refreshCaptureStats();
    });
    recLowerNodeSelect.addEventListener("change", () => {
      refreshRecordPairOptions();
      refreshCaptureStats();
    });
    recModeSelect.addEventListener("change", () => {
      updateRecordModeLayout();
      refreshCaptureStats();
    });
    recSingleNodeSelect.addEventListener("change", () => {
      updateRecordPairStatus();
      refreshCaptureStats();
    });
    sharedActionLabel.addEventListener("change", updateRecordPairStatus);
    updateRecordModeLayout();

    function addCaptureRateSample(entry, message, node, sample) {
      const source = {...node, ...sample};
      const row = PlanKOImu.sampleFromMessage(message, source, {
        mount_site: node.mount_site, body_part: node.body_part,
        side: node.side || "unknown"
      });
      const previous = entry.rateWindow.at(-1);
      if (previous && (previous.stream_epoch !== row.stream_epoch ||
          row.device_time_s < previous.device_time_s)) {
        entry.rateWindow.length = 0;
      }
      const last = entry.rateWindow.at(-1);
      if (last && last.stream_epoch === row.stream_epoch && last.seq === row.seq) return;
      entry.rateWindow.push({
        seq: row.seq, stream_epoch: row.stream_epoch,
        device_time_s: row.device_time_s
      });
      const cutoff = row.device_time_s - 3;
      while (entry.rateWindow.length && entry.rateWindow[0].device_time_s < cutoff) {
        entry.rateWindow.shift();
      }
    }

    function updateCaptureNodes(message) {
      const now = Date.now();
      for (const entry of captureNodes.values()) {
        entry.present = false;
        entry.currentValid = false;
      }
      for (const node of message.nodes || []) {
        const key = PlanKOImu.nodeKey(message, node);
        let entry = captureNodes.get(key);
        if (!entry) {
          const card = document.createElement("div");
          card.className = "rec-field";
          const title = document.createElement("strong");
          const context = document.createElement("span");
          const status = document.createElement("span");
          status.setAttribute("role", "status");
          card.append(title, context, status);
          if (!captureNodes.size) $("recNodeList").replaceChildren();
          $("recNodeList").appendChild(card);
          entry = {card, title, context, status, node, lastSeen: now, rateWindow: [],
            lastValidAt: 0, lastValidNode: null, valid: false, currentValid: false, present: false};
          captureNodes.set(key, entry);
        }
        entry.present = true;
        entry.lastSeen = now;
        const rateSamples = Array.isArray(node.samples) && node.samples.length
          ? node.samples : [node];
        for (const sample of rateSamples) {
          try {
            addCaptureRateSample(entry, message, node, sample);
          } catch (error) {
            // A bad sample does not reset the independent display-rate window.
          }
        }
        try {
          PlanKOImu.sampleFromMessage(message, node, {
            mount_site: node.mount_site, body_part: node.body_part, side: node.side || "unknown"
          });
          entry.valid = !!node.sensor_uid && !!node.mount_site && !!node.body_part;
          entry.currentValid = entry.valid;
          if (entry.valid) {
            entry.node = node;
            entry.lastValidNode = node;
            entry.lastValidAt = now;
          }
        } catch (error) {
          entry.valid = false;
          entry.error = error.message;
        }
        if (!entry.currentValid && entry.lastValidNode && now - entry.lastValidAt < NODE_HOLD_MS) {
          entry.node = entry.lastValidNode;
          entry.valid = true;
        } else if (!entry.currentValid) {
          entry.node = node;
        }
        entry.title.textContent = `Node #${entry.node.nodeId} · ${entry.node.sensor_uid || "UID missing"}`;
        entry.context.textContent = `${entry.node.mount_site || "site?"} / ${PlanKOImu.trainingBody(entry.node) || "body?"} / ${entry.node.side || "unknown"}`;
      }
      refreshRecordPairOptions();
    }

    function refreshCaptureStats() {
      let total = 0;
      const mode = recModeSelect.value;
      const singleId = Number(recSingleNodeSelect.value);
      const upperId = Number(recUpperNodeSelect.value);
      const lowerId = Number(recLowerNodeSelect.value);
      for (const [key, entry] of captureNodes) {
        const stream = datasetRecorder.streams.get(key);
        const count = stream?.rows.length || 0;
        total += count;
        const holdAge = Date.now() - entry.lastValidAt;
        const live = !!entry.lastValidNode && holdAge < NODE_HOLD_MS;
        const current = live && entry.present && entry.currentValid;
        const recent = entry.rateWindow;
        const span = recent.length > 1 ? recent.at(-1).device_time_s - recent[0].device_time_s : 0;
        const rate = live && span > 0 ? ((recent.length - 1) / span).toFixed(1) + " Hz" : "— Hz";
        const nodeId = Number(entry.node.nodeId);
        const role = mode === "all"
          ? "เลือกบันทึก"
          : mode === "single"
            ? (nodeId === singleId ? "เลือกบันทึก" : "ไม่เลือก")
            : (nodeId === upperId ? "หลังบน" : nodeId === lowerId ? "หลังล่าง" : "ไม่เลือก");
        const linkState = current ? "LIVE" : live ? "HOLD" : "รอ IMU จริง";
        entry.status.textContent = `${linkState} · ${role} · ${rate} · ${count} samples · ขาด ${stream?.missing || 0} · ตัดซ้ำ ${stream?.duplicates || 0} · ไม่ครบ ${stream?.invalid || 0}`;
        entry.status.style.color = current ? "var(--mint)" : "var(--orange)";
        entry.status.textContent += ` · Gateway BLE dropped ${entry.node.dropped ?? "—"}`;
      }
      updateRecordPairStatus();
      if (isRecording) sampleCounterText.textContent = `${total.toLocaleString()} samples · ${datasetRecorder.streams.size} Node/contexts · ${((Date.now() - recordStartTime) / 1000).toFixed(1)}s`;
    }
    setInterval(refreshCaptureStats, 500);

    function recordingControls(active) {
      for (const id of ["gatewayFilter", "recSubjectId", "recSessionId", "recTrialId", "guidedTimerCheck", "recPhaseReviewed", "recClearBtn", "recUpperNodeSelect", "recLowerNodeSelect", "sharedActionLabel", "recModeSelect", "recSingleNodeSelect"]) {
        if ($(id)) $(id).disabled = active;
      }
      recStartBtn.disabled = active;
      recStopBtn.disabled = !active;
    }

    recStartBtn.addEventListener("click", () => {
      const mode = recModeSelect.value;
      const singleMode = mode === "single";
      const allMode = mode === "all";
      const singleId = Number(recSingleNodeSelect.value);
      const upperId = Number(recUpperNodeSelect.value);
      const lowerId = Number(recLowerNodeSelect.value);
      const liveEntries = liveCaptureNodeEntries();
      const liveById = new Map(liveEntries.map(entry => [Number(entry.node.nodeId), entry]));
      if (allMode && !liveEntries.length) {
        alert("รอข้อมูล IMU จริงจาก Gateway ก่อนเริ่มบันทึก"); return;
      }
      if (!allMode && !singleMode && (!Number.isInteger(upperId) || !Number.isInteger(lowerId) || upperId < 1 || lowerId < 1 || upperId === lowerId)) {
        if (Number.isInteger(singleId) && singleId >= 1) {
          alert("Pair mode needs two different live Nodes. Switch Recording mode to Single Node to record one."); return;
        }
        alert("เลือก Node หลังบนและหลังล่างให้เป็นคนละ Node ก่อนเริ่ม"); return;
      }
      if (singleMode && (!Number.isInteger(singleId) || singleId < 1)) {
        alert("Select one Node before starting the recording."); return;
      }
      const selectedNodeIds = allMode
        ? liveEntries.map(entry => Number(entry.node.nodeId))
        : singleMode
          ? [singleId]
          : [upperId, lowerId];
      const selectedEntries = selectedNodeIds.map(nodeId => liveById.get(nodeId));
      if (singleMode && !selectedEntries[0]) {
        alert("Wait for the selected Node to send live IMU samples, then try again."); return;
      }
      if (selectedEntries.some(entry => !entry)) {
        alert("รอข้อมูล IMU จริงของ Node หลังบนและหลังล่างจาก Gateway ก่อนเริ่ม"); return;
      }
      if (!sharedActionLabel.value) { alert("เลือก Case/Action Label กลางที่จะใช้กับทุก Node ก่อนเริ่ม"); return; }
      const metadata = {};
      for (const [key, id] of [["subject_id", "recSubjectId"], ["session_id", "recSessionId"], ["trial_id", "recTrialId"]]) {
        metadata[key] = $(id).value.trim();
        if (!metadata[key]) { alert("กรอก Subject, Session และ Trial ID ให้ครบ"); return; }
      }
      const gatewayId = $("gatewayFilter").value.trim();
      if (!gatewayId) { alert("เลือก Gateway ID ก่อนเริ่มบันทึก"); return; }
      metadata.action_label = sharedActionLabel.value;
      metadata.record_mode = allMode ? "all_nodes" : singleMode ? "single_node" : "paired_nodes";
      recordingNodeIds = new Set(selectedNodeIds);
      recordStartTime = Date.now();
      datasetRecorder.start(gatewayId, metadata, recordStartTime);
      isRecording = true;
      recordingControls(true);
      recStatusPill.className = "rec-badge recording";
      recStatusPill.textContent = `● ${sharedActionLabel.value} · Node #${upperId} + #${lowerId} · ${gatewayId}`;
      const selectedNodeLabel = selectedNodeIds.length === 1
        ? `Node #${selectedNodeIds[0]}` : `Nodes #${selectedNodeIds.join(" + #")}`;
      recStatusPill.textContent = `● ${sharedActionLabel.value} · ${selectedNodeLabel} · ${gatewayId}`;
      playBeep(660, 0.2);

      if (guidedTimerCheck.checked) {
        startGuidedSequence();
      }
    });

    recStopBtn.addEventListener("click", () => {
      stopRecordingAndExport();
    });

    function startGuidedSequence() {
      guidedStepIndex = 0;
      let totalTime = GUIDED_SEQUENCE.reduce((a, b) => a + b.duration, 0);
      let stepStartTime = Date.now();

      function updateGuided() {
        if (!isRecording) return;
        const now = Date.now();
        const currentStep = GUIDED_SEQUENCE[guidedStepIndex];
        const stepElapsed = (now - stepStartTime) / 1000;
        const stepRemaining = Math.max(0, currentStep.duration - stepElapsed);

        if (currentPhase !== currentStep.phase) {
          setPhase(currentStep.phase);
          playBeep(880, 0.15);
        }

        const totalElapsed = (now - recordStartTime) / 1000;
        const progress = Math.min(100, (totalElapsed / totalTime) * 100);
        guidedProgressBar.style.width = progress.toFixed(1) + "%";
        guidedText.textContent = `ช่วงที่ ${guidedStepIndex + 1}/${GUIDED_SEQUENCE.length}: [${currentStep.phase}] เหลือ ${stepRemaining.toFixed(1)} วิ (ทั้งหมด ${totalElapsed.toFixed(1)}s / ${totalTime}s)`;

        if (stepRemaining <= 0) {
          guidedStepIndex++;
          stepStartTime = Date.now();
          if (guidedStepIndex >= GUIDED_SEQUENCE.length) {
            playBeep(1100, 0.3);
            stopRecordingAndExport();
            return;
          }
        }
        guidedTimerId = setTimeout(updateGuided, 100);
      }
      updateGuided();
    }

    function addCaptureDownload(filename, content, type) {
      const url = URL.createObjectURL(new Blob([content], {type}));
      const link = document.createElement("a");
      link.className = "csv-link";
      link.href = url;
      link.download = filename;
      link.textContent = `?? ${filename}`;
      $("recDownloadList").prepend(link);
      captureDownloads.push({url, filename});
    }

    function stopRecordingAndExport() {
      if (!isRecording) return;
      isRecording = false;
      datasetRecorder.stop();
      recordingNodeIds.clear();
      if (guidedTimerId) clearTimeout(guidedTimerId);
      recordingControls(false);
      recStatusPill.className = "rec-badge";
      guidedProgressBar.style.width = "100%";
      const safe = value => String(value).replace(/[^a-zA-Z0-9_.-]/g, "_");
      const stamp = new Date(recordStartTime).toISOString().replace(/[:.]/g, "-");
      const base = `${safe(datasetRecorder.metadata.subject_id)}_${safe(datasetRecorder.metadata.session_id)}_${safe(datasetRecorder.metadata.trial_id)}_${stamp}`;
      const manifest = datasetRecorder.manifest();
      let total = 0, files = 0;
      [...datasetRecorder.streams.values()].forEach((stream, index) => {
        if (!stream.rows.length) return;
        total += stream.rows.length;
        files++;
        const name = `${base}_node${stream.nodeId}_${safe(stream.sensorUid)}_${safe(stream.mountSite)}_${index + 1}.csv`;
        manifest.streams[index].file = name;
        addCaptureDownload(name, PlanKOImu.csv(stream.rows), "text/csv;charset=utf-8");
      });
      if (!total) {
        recStatusPill.textContent = "????? sample ??????????????";
        guidedText.textContent = "??????? Gateway, Node ????????? IMU 6 ???";
        return;
      }
      addCaptureDownload(`${base}_manifest.json`, JSON.stringify(manifest, null, 2), "application/json");
      $("recDownloadAllBtn").disabled = false;
      recStatusPill.textContent = `?????????????? ${files} CSV`;
      sampleCounterText.textContent = `${total.toLocaleString()} samples ? ${files} CSV`;
      guidedText.textContent = "????????? CSV ??? Node ??? manifest ???????????????????????; Phase ??? timer ??????? review ??????? train phase";
      refreshCaptureStats();
    }

    $("recDownloadAllBtn").addEventListener("click", () => {
      // Browsers may ask the user to allow multiple downloads. Individual links remain available.
      for (const file of captureDownloads) {
        const link = document.createElement("a");
        link.href = file.url; link.download = file.filename;
        document.body.appendChild(link); link.click(); link.remove();
      }
    });
    $("recClearBtn").addEventListener("click", () => {
      if (isRecording) return;
      for (const file of captureDownloads.splice(0)) URL.revokeObjectURL(file.url);
      $("recDownloadList").replaceChildren();
      $("recDownloadAllBtn").disabled = true;
      datasetRecorder.streams.clear();
      sampleCounterText.textContent = "0 samples";
      recStatusPill.className = "rec-badge";
      recStatusPill.textContent = "??????????? (Ready)";
      refreshCaptureStats();
    });
    window.addEventListener("beforeunload", event => {
      if (isRecording || captureDownloads.length) { event.preventDefault(); event.returnValue = ""; }
    });

    // -------------------------------------------------------------
    // EXPERIMENTAL PLANK PHASE TRACKER (paired six-axis IMUs)
    // -------------------------------------------------------------
    const plankPhaseLabels = {
      normal: "Normal",
      phase1_approaching_error: "Phase 1 · Approaching Error",
      phase2_incorrect_posture: "Phase 2 · Incorrect Posture",
      phase3_recovery_trend: "Phase 3 · Recovery Trend",
      unknown: "unknown"
    };

    function updatePlankNodeOptions(nodes) {
      for (const id of ["plankUpperNode", "plankLowerNode"]) {
        const select = $(id);
        const selected = select.value;
        for (const option of select.options) {
          const node = nodes.find(item => Number(item.nodeId) === Number(option.value));
          option.textContent = node
            ? `Node #${node.nodeId} · ${node.mount_site || "site?"} / ${node.body_part || "part?"}${node.isMock ? " (mock)" : ""}`
            : `Node #${option.value} · offline`;
        }
        select.value = selected;
      }
    }

    function updateNodeBatteryData(nodes) {
      if (!Array.isArray(nodes)) return;
      const now = Date.now();
      for (const node of nodes) {
        const nodeId = Number(node?.nodeId);
        if (!Number.isInteger(nodeId) || nodeId < 1 || nodeId > 4) continue;
        let state = nodeBatteryStates.get(nodeId);
        if (!state) {
          state = {
            nodeId, connected: false, lastNodeAt: 0, batteryReceived: false, batteryValid: false,
            batteryPercent: null, batteryMv: null, lastBatteryAt: 0, mountSite: ""
          };
          nodeBatteryStates.set(nodeId, state);
        }

        state.connected = node.connected === true;
        state.lastNodeAt = now;
        state.mountSite = String(node.mount_site || node.body_part || "");
        const percent = Number(node.battery_percent);
        const millivolts = node.battery_mv == null ? NaN : Number(node.battery_mv);
        state.batteryReceived = (node.battery_received === true || node.battery_valid === true) &&
          Number.isFinite(millivolts);
        state.batteryValid = node.battery_valid === true &&
          Number.isFinite(percent) && percent >= 0 && percent <= 100 &&
          Number.isFinite(millivolts) && millivolts >= 2500 && millivolts <= 4500;
        if (state.batteryReceived) {
          state.batteryMv = millivolts;
          state.lastBatteryAt = now;
        }
        state.batteryPercent = state.batteryValid ? percent : null;
      }
    }

    function renderNodeBatteryData() {
      const now = Date.now();
      let connectedCount = 0;
      let packetCount = 0;
      let liveCount = 0;
      nodeBatteryList.replaceChildren();

      for (const nodeId of [...nodeBatteryStates.keys()].sort((a, b) => a - b)) {
        const state = nodeBatteryStates.get(nodeId);
        const nodeRecent = state.lastNodeAt > 0 && now - state.lastNodeAt < 3000;
        const connected = nodeRecent && state.connected;
        const batteryAge = state.lastBatteryAt > 0 ? now - state.lastBatteryAt : Infinity;
        const hasReading = state.batteryMv !== null && batteryAge < 30000;
        const batteryLive = connected && state.batteryReceived && state.batteryValid && hasReading;
        if (connected) connectedCount++;
        if (connected && state.batteryReceived) packetCount++;
        if (batteryLive) liveCount++;

        const row = document.createElement("div");
        row.className = "battery-node";
        const head = document.createElement("div");
        head.className = "battery-node-head";
        const title = document.createElement("strong");
        title.className = "battery-node-title";
        title.textContent = `Node #${nodeId}${state.mountSite ? ` · ${state.mountSite}` : ""}`;
        const status = document.createElement("span");
        status.className = `battery-node-state${batteryLive ? " live" : connected && state.batteryReceived && !state.batteryValid ? " invalid" : connected && !state.batteryReceived ? " waiting" : ""}`;
        if (batteryLive) status.textContent = "LIVE";
        else if (!state.lastNodeAt) status.textContent = "Waiting for Node";
        else if (!connected && hasReading) status.textContent = `OFFLINE · last ${Math.floor(batteryAge / 1000)}s ago`;
        else if (!connected) status.textContent = "DISCONNECTED";
        else if (state.batteryReceived && !state.batteryValid) status.textContent = "ADC RECEIVED · INVALID";
        else if (hasReading && !state.batteryReceived) status.textContent = `NO RECENT PACKET · last ${Math.floor(batteryAge / 1000)}s ago`;
        else status.textContent = "Waiting for battery packet";
        head.append(title, status);

        const reading = document.createElement("div");
        reading.className = "battery-reading";
        const percent = document.createElement("strong");
        percent.className = "battery-percent";
        percent.textContent = batteryLive ? `${Math.round(state.batteryPercent)}%` : "--%";
        const voltage = document.createElement("span");
        voltage.className = "battery-voltage";
        voltage.textContent = hasReading ? `${Math.round(state.batteryMv)} mV` : "-- mV";
        reading.append(percent, voltage);

        const meter = document.createElement("div");
        meter.className = "battery-meter";
        meter.setAttribute("role", "meter");
        meter.setAttribute("aria-label", `Node ${nodeId} battery level`);
        meter.setAttribute("aria-valuemin", "0");
        meter.setAttribute("aria-valuemax", "100");
        if (batteryLive) meter.setAttribute("aria-valuenow", String(Math.round(state.batteryPercent)));
        const fill = document.createElement("div");
        const level = batteryLive ? Math.max(0, Math.min(100, state.batteryPercent)) : 0;
        fill.className = `battery-fill${level <= 15 && batteryLive ? " low" : level < 40 && batteryLive ? " warn" : ""}${batteryLive ? "" : " stale"}`;
        fill.style.width = `${level}%`;
        meter.append(fill);

        row.append(head, reading, meter);
        nodeBatteryList.append(row);
      }

      batterySummary.textContent = connectedCount
        ? `Packets ${packetCount}/${connectedCount} · valid ${liveCount}`
        : "No Node online";
      batterySummary.className = `battery-summary${liveCount ? " online" : connectedCount ? " waiting" : ""}`;
    }

    renderNodeBatteryData();
    setInterval(renderNodeBatteryData, 1000);

    function resetPlankReadout(reason = "Select two connected IMUs, then calibrate while holding a correct plank for 2 seconds.") {
      $("plankOutPhase").textContent = "unknown";
      $("plankOutError").textContent = "—";
      $("plankOutScore").textContent = "—";
      $("plankOutTrend").textContent = "—";
      $("plankOutPitch").textContent = "—";
      $("plankOutRoll").textContent = "—";
      $("plankOutUpper").textContent = "unknown";
      $("plankUpperMeta").textContent = "—";
      $("plankOutLower").textContent = "unknown";
      $("plankLowerMeta").textContent = "—";
      $("plankSensorStatus").textContent = reason;
    }

    function setPlankOutput(result, frame = null) {
      $("plankOutPhase").textContent = plankPhaseLabels[result.phase] || result.phase || "unknown";
      $("plankOutError").textContent = [result.error_type, result.error_direction].filter(Boolean).join(" · ") || "—";
      $("plankOutScore").textContent = Number.isFinite(result.error_score) ? `${result.error_score.toFixed(2)} E` : "—";
      $("plankOutTrend").textContent = Number.isFinite(result.trend_per_second) ? `${result.trend_per_second.toFixed(2)} /s` : "warming up";
      $("plankOutPitch").textContent = Number.isFinite(result.delta_pitch_deg) ? `${result.delta_pitch_deg.toFixed(1)}°` : "—";
      $("plankOutRoll").textContent = Number.isFinite(result.delta_roll_deg) ? `${result.delta_roll_deg.toFixed(1)}°` : "—";
      for (const [part, phaseId, metaId] of [
        ["upper", "plankOutUpper", "plankUpperMeta"],
        ["lower", "plankOutLower", "plankLowerMeta"]
      ]) {
        const node = result.nodes?.[part];
        $(phaseId).textContent = node
          ? `Node #${node.node_id} · ${plankPhaseLabels[node.phase] || node.phase}`
          : "unknown";
        $(metaId).textContent = node
          ? `E ${Number.isFinite(node.error_score) ? node.error_score.toFixed(2) : "—"} · V ${Number.isFinite(node.trend_per_second) ? node.trend_per_second.toFixed(2) : "—"}/s · ${node.mount_site}`
          : "—";
      }
      if (result.reason && result.reason !== "experimental_rule_based_phase") {
        $("plankSensorStatus").textContent = `Plank monitor: ${result.reason}`;
      } else if (result.phase === "unknown") {
        $("plankSensorStatus").textContent = "Collecting a continuous paired 20 Hz trend window…";
      } else {
        $("plankSensorStatus").textContent = `${result.thresholds_validated ? "Configured thresholds" : "Provisional thresholds"} · rule-based output, not a trained classifier`;
      }
      if (plankEvaluationRecording && frame) recordPlankEvaluation(result, frame);
    }

    function recordPlankEvaluation(result, frame) {
      const upper = result.nodes?.upper || {};
      const lower = result.nodes?.lower || {};
      plankEvaluationRows.push({
        timestamp_iso: new Date().toISOString(), received_time_s: frame.received_time_s,
        subject_id: $("plankEvalSubject").value.trim() || "P01",
        session_id: $("plankEvalSession").value.trim() || "S01",
        trial_id: $("plankEvalTrial").value.trim() || "plank-trial",
        gateway_id: frame.gateway_id, mqtt_seq: frame.mqtt_seq,
        upper_node_id: frame.upper.node_id, upper_sensor_uid: frame.upper.sensor_uid,
        upper_mount_site: frame.upper.mount_site, upper_body_part: frame.upper.body_part,
        upper_seq: frame.upper.seq,
        lower_node_id: frame.lower.node_id, lower_sensor_uid: frame.lower.sensor_uid,
        lower_mount_site: frame.lower.mount_site, lower_body_part: frame.lower.body_part,
        lower_seq: frame.lower.seq,
        truth_pair: $("plankTruthPair").value, pred_pair: result.phase || "unknown",
        pair_error_type: result.error_type || "", pair_error_score: result.error_score ?? "",
        pair_trend_per_second: result.trend_per_second ?? "",
        truth_upper: $("plankTruthUpper").value, pred_upper: upper.phase || "unknown",
        upper_error_score: upper.error_score ?? "", upper_trend_per_second: upper.trend_per_second ?? "",
        truth_lower: $("plankTruthLower").value, pred_lower: lower.phase || "unknown",
        lower_error_score: lower.error_score ?? "", lower_trend_per_second: lower.trend_per_second ?? ""
      });
      $("plankEvalCount").textContent = `${plankEvaluationRows.length} prediction rows recorded`;
    }

    function finishPlankEvaluation() {
      if (!plankEvaluationRecording) return;
      plankEvaluationRecording = false;
      $("plankEvalStartBtn").disabled = !plankMonitorActive || !plankCalibrated;
      $("plankEvalStopBtn").disabled = true;
      if (!plankEvaluationRows.length) {
        $("plankEvalCount").textContent = "No prediction rows were recorded";
        return;
      }
      const columns = Object.keys(plankEvaluationRows[0]);
      const quote = value => {
        const text = String(value ?? "");
        return /[",\r\n]/.test(text) ? `"${text.replace(/"/g, '""')}"` : text;
      };
      const csv = [columns.join(","), ...plankEvaluationRows.map(row => columns.map(key => quote(row[key])).join(","))].join("\r\n");
      const blob = new Blob(["\uFEFF", csv], {type: "text/csv;charset=utf-8"});
      const url = URL.createObjectURL(blob);
      const trial = ($("plankEvalTrial").value.trim() || "plank-trial").replace(/[^a-z0-9_-]/gi, "_");
      const link = document.createElement("a");
      link.className = "csv-link";
      link.href = url;
      link.download = `${trial}_plank_phase_predictions.csv`;
      link.textContent = `Download ${link.download} (${plankEvaluationRows.length} rows)`;
      $("plankEvalDownloadList").prepend(link);
      link.click();
      $("plankEvalCount").textContent = `${plankEvaluationRows.length} rows saved · pair / upper / lower labels are evaluated separately`;
      plankEvaluationRows = [];
    }

    $("plankEvalStartBtn").addEventListener("click", () => {
      if (!plankMonitorActive || !plankCalibrated) return;
      plankEvaluationRows = [];
      plankEvaluationRecording = true;
      $("plankEvalStartBtn").disabled = true;
      $("plankEvalStopBtn").disabled = false;
      $("plankEvalCount").textContent = "Recording labeled predictions · update reference labels during the trial";
    });
    $("plankEvalStopBtn").addEventListener("click", finishPlankEvaluation);

    function plankNodePayload(node) {
      return {
        node_id: Number(node.nodeId), sensor_uid: node.sensor_uid || `node-${node.nodeId}`,
        mount_site: node.mount_site, body_part: node.body_part, side: node.side || "unknown",
        seq: Number(node.seq), pitch: Number(node.pitch), roll: Number(node.roll),
        accel: node.accel, gyro: node.gyro, valid: node.valid === true,
        isMock: node.isMock === true, calibrated: node.calibrated === true
      };
    }

    function plankApiBase() {
      return $("aiHost").value.trim().replace(/\/$/, "");
    }

    async function sendPlankCalibration() {
      const streamKey = plankRuntimeKey;
      const frames = plankCalibrationFrames.slice();
      plankCalibrating = false;
      plankCalibrationFrames = [];
      $("plankCalibrateBtn").disabled = true;
      $("plankSensorStatus").textContent = `Checking ${frames.length} baseline samples for a stable normal plank…`;
      try {
        const response = await fetch(`${plankApiBase()}/api/plank/calibrate`, {
          method: "POST", headers: {"Content-Type": "application/json"},
          body: JSON.stringify({stream_key: streamKey, frames}),
          signal: AbortSignal.timeout(5000)
        });
        const result = await response.json();
        if (!response.ok || !result.ready) throw new Error(result.reason || `HTTP ${response.status}`);
        plankCalibrated = true;
        $("plankSensorStatus").textContent = `Baseline saved · ${result.sample_count} samples, ${result.duration_seconds.toFixed(1)} s · pitch σ ${result.pitch_std_deg.toFixed(1)}°, roll σ ${result.roll_std_deg.toFixed(1)}°`;
        $("plankCalibrateBtn").disabled = false;
        $("plankEvalStartBtn").disabled = !plankMonitorActive;
      } catch (error) {
        plankCalibrated = false;
        $("plankSensorStatus").textContent = `Calibration failed: ${error.message}. Check sensor mounting and hold the correct plank still, then retry.`;
        $("plankCalibrateBtn").disabled = false;
        $("plankEvalStartBtn").disabled = true;
      }
    }

    async function sendPlankPrediction(frame, streamKey) {
      if (plankRequestInFlight) return;
      plankRequestInFlight = true;
      const serial = ++plankRequestSerial;
      try {
        const response = await fetch(`${plankApiBase()}/api/plank/frame`, {
          method: "POST", headers: {"Content-Type": "application/json"},
          body: JSON.stringify({stream_key: streamKey, frame}),
          signal: AbortSignal.timeout(1500)
        });
        const result = await response.json();
        if (!response.ok || !result.ready) {
          if (serial === plankRequestSerial) {
            plankCalibrated = false;
            $("plankEvalStartBtn").disabled = true;
            resetPlankReadout(`Plank phase unavailable: ${result.reason || `HTTP ${response.status}`}. Recalibrate if the sensor pair changed.`);
          }
          return;
        }
        if (serial === plankRequestSerial && plankMonitorActive) setPlankOutput(result, frame);
      } catch (error) {
        if (serial === plankRequestSerial) resetPlankReadout(`Plank API unavailable: ${error.message}`);
      } finally {
        plankRequestInFlight = false;
      }
    }

    function processPlankMessage(data) {
      if (!plankMonitorActive || !Array.isArray(data.nodes)) return;
      const upperId = Number($("plankUpperNode").value);
      const lowerId = Number($("plankLowerNode").value);
      if (upperId === lowerId) {
        resetPlankReadout("Choose two different IMU nodes for upper trunk and lumbar/pelvis.");
        return;
      }
      const upperRaw = data.nodes.find(item => Number(item.nodeId) === upperId);
      const lowerRaw = data.nodes.find(item => Number(item.nodeId) === lowerId);
      if (!upperRaw || !lowerRaw) {
        resetPlankReadout("Waiting for both selected IMUs to connect to the Gateway.");
        return;
      }
      const upper = plankNodePayload(upperRaw);
      const lower = plankNodePayload(lowerRaw);
      if (!upper.valid || !lower.valid || upper.isMock || lower.isMock ||
          !Number.isInteger(upper.seq) || !Number.isInteger(lower.seq) ||
          !Number.isFinite(upper.pitch) || !Number.isFinite(lower.pitch) ||
          !Number.isFinite(upper.roll) || !Number.isFinite(lower.roll) ||
          !Array.isArray(upper.accel) || !Array.isArray(lower.accel) ||
          !Array.isArray(upper.gyro) || !Array.isArray(lower.gyro)) {
        resetPlankReadout("Waiting for valid real six-axis samples and fused pitch/roll from both nodes.");
        return;
      }
      const gatewayId = String(data.gateway?.clientId || "gateway");
      const pairContext = [gatewayId, upper.sensor_uid, upper.mount_site, upper.body_part,
                           lower.sensor_uid, lower.mount_site, lower.body_part].join("|");
      const runtimeKey = `${plankPageSession}|${pairContext}`;
      if (runtimeKey !== plankRuntimeKey) {
        plankRuntimeKey = runtimeKey;
        plankLastPairSeq = null;
        plankLastPairSignature = "";
        plankCalibrated = false;
        plankCalibrating = false;
        plankCalibrationFrames = [];
        $("plankEvalStartBtn").disabled = true;
        if (plankEvaluationRecording) finishPlankEvaluation();
        $("plankCalibrateBtn").disabled = !plankMonitorActive;
        resetPlankReadout(`Sensor pair: upper ${upper.mount_site}/${upper.body_part}, lower ${lower.mount_site}/${lower.body_part}. Expected chest/upper_back + lumbar/pelvis.`);
      }
      const signature = `${upper.seq}:${lower.seq}`;
      if (signature === plankLastPairSignature) return;
      if (plankLastPairSeq && (upper.seq === plankLastPairSeq.upper || lower.seq === plankLastPairSeq.lower)) return;
      const frame = {
        gateway_id: gatewayId,
        mqtt_seq: Number(data.gateway?.mqttSeq),
        received_time_s: performance.now() / 1000,
        upper, lower
      };
      plankLastPairSeq = {upper: upper.seq, lower: lower.seq};
      plankLastPairSignature = signature;

      if (plankCalibrating) {
        if ((performance.now() - plankCalibrationStartedAt) > 5000) {
          plankCalibrating = false;
          plankCalibrationFrames = [];
          $("plankSensorStatus").textContent = "Calibration timed out before 2 seconds of paired samples arrived. Try again.";
          $("plankCalibrateBtn").disabled = false;
          return;
        }
        plankCalibrationFrames.push(frame);
        $("plankSensorStatus").textContent = `Hold a correct plank still · baseline ${plankCalibrationFrames.length}/40 paired samples`;
        if (plankCalibrationFrames.length >= 40) void sendPlankCalibration();
        return;
      }
      if (plankCalibrated) void sendPlankPrediction(frame, runtimeKey);
    }

    $("plankMonitorBtn").addEventListener("click", () => {
      plankMonitorActive = !plankMonitorActive;
      $("plankMonitorBtn").textContent = plankMonitorActive ? "Stop plank monitor" : "Start plank monitor";
      $("plankMonitorBtn").className = plankMonitorActive ? "btn danger" : "btn primary";
      $("plankCalibrateBtn").disabled = !plankMonitorActive || !plankRuntimeKey;
      $("plankEvalStartBtn").disabled = !plankMonitorActive || !plankCalibrated;
      plankLastPairSeq = null;
      plankLastPairSignature = "";
      if (!plankMonitorActive) {
        plankCalibrating = false;
        plankCalibrationFrames = [];
        if (plankEvaluationRecording) finishPlankEvaluation();
        resetPlankReadout("Plank monitor stopped.");
      } else {
        resetPlankReadout("Waiting for paired IMU samples from MQTT.");
      }
    });

    $("plankCalibrateBtn").addEventListener("click", () => {
      if (!plankMonitorActive) return;
      if (plankEvaluationRecording) finishPlankEvaluation();
      plankCalibrated = false;
      plankCalibrating = true;
      plankCalibrationFrames = [];
      plankCalibrationStartedAt = performance.now();
      plankLastPairSeq = null;
      plankLastPairSignature = "";
      $("plankCalibrateBtn").disabled = true;
      $("plankEvalStartBtn").disabled = true;
      resetPlankReadout("Calibration started. Hold a correct plank still while 40 paired samples are collected.");
      setTimeout(() => {
        if (plankCalibrating && (performance.now() - plankCalibrationStartedAt) >= 5000) {
          plankCalibrating = false;
          plankCalibrationFrames = [];
          $("plankSensorStatus").textContent = "Calibration timed out before enough paired samples arrived. Check both nodes and try again.";
          $("plankCalibrateBtn").disabled = false;
        }
      }, 5100);
    });

    for (const id of ["plankUpperNode", "plankLowerNode"]) {
      $(id).addEventListener("change", () => {
        plankRuntimeKey = "";
        plankLastPairSeq = null;
        plankLastPairSignature = "";
        plankCalibrated = false;
        plankCalibrating = false;
        plankCalibrationFrames = [];
        $("plankCalibrateBtn").disabled = true;
        $("plankEvalStartBtn").disabled = true;
        if (plankEvaluationRecording) finishPlankEvaluation();
        resetPlankReadout("Sensor pair changed. Calibrate the correct plank again.");
      });
    }

    // -------------------------------------------------------------
    // AI MODEL LIVE INFERENCE (Connecting to serve_model.py:8765)
    // -------------------------------------------------------------
    let aiPolling = false;
    let aiTimerId = null;
    let aiGeneration = 0;

    $("aiToggleBtn").addEventListener("click", () => {
      aiPolling = !aiPolling;
      aiGeneration++;
      clearTimeout(aiTimerId);
      if (aiPolling) {
        $("aiToggleBtn").textContent = "⏸ หยุดตรวจจับ";
        $("aiToggleBtn").className = "btn danger";
        pollAiServer();
      } else {
        $("aiToggleBtn").textContent = "▶ เริ่มตรวจจับสด (Live Inference)";
        $("aiToggleBtn").className = "btn primary";
        if (aiTimerId) clearTimeout(aiTimerId);
      }
    });

    async function pollAiServer(generation = aiGeneration) {
      if (!aiPolling) return;
      const host = $("aiHost").value.trim().replace(/\/$/, "");
      const site = recSiteSelect.value;
      const body = recBodyPartSelect.value;

      try {
        if (!modelMetadata || modelMetadata.host !== host) {
          const healthResponse = await fetch(`${host}/api/health`, { signal: AbortSignal.timeout(3000) });
          const metadata = await healthResponse.json();
          if (!aiPolling || generation !== aiGeneration) return;
          if (!healthResponse.ok || !metadata.ready) throw new Error(metadata.reason || "Model not ready");
          modelMetadata = {...metadata, host};
          inferenceWindow.seconds = metadata.window_seconds;
          inferenceWindow.gap = metadata.gap_reset_seconds;
          $("aiModelContract").textContent = `${metadata.feature_version}: ${metadata.feature_columns.length} features; sites: ${metadata.mount_sites.join(", ")}; body: ${metadata.body_parts.join(", ")}; actions: ${metadata.action_labels.join(", ")}; phase model: ${metadata.phase_available ? "available" : "unavailable"}`;
          $("aiServerBadge").textContent = "Model server ready; source: MQTT";
          $("aiReasonText").textContent = `Model: ${metadata.feature_version} | ${metadata.feature_columns.length} features | sites: ${metadata.mount_sites.join(", ")}`;
        }
        if (!latestSample || Date.now() - lastSampleArrival > 1500 || !inferenceWindow.ready()) {
          $("aiOutAction").textContent = "unknown";
          $("aiOutStage").textContent = "unknown";
          $("aiReasonText").textContent = "Waiting for a contiguous MQTT sensor window; check selected node/site and six axes";
          return;
        }
        const sample = latestSample;
        const key = `${sample.sensor_uid}|${sample.stream_epoch}|${sample.mount_site}|${sample.body_part}|${sample.side}|${sample.seq}`;
        if (key === lastInferenceKey) return;
        const rows = inferenceWindow.rows.slice();
        const res = await fetch(`${host}/api/predict-window`, {
          method: "POST", headers: {"Content-Type": "application/json"},
          body: JSON.stringify({rows, mount_site: sample.mount_site, body_part: sample.body_part,
            stream_key: `${sample.sensor_uid}|${sample.mount_site}|${sample.body_part}|${sample.side}`,
            device_time_s: sample.device_time_s, seq: sample.seq}),
          signal: AbortSignal.timeout(3000)
        });
        lastInferenceKey = key;
        if (res.ok) {
          const data = await res.json();
          if (!aiPolling || generation !== aiGeneration) return;
          $("aiServerBadge").className = "rec-badge";
          $("aiServerBadge").style.color = "var(--mint)";
          $("aiServerBadge").textContent = "● เซิร์ฟเวอร์ออนไลน์ (Online)";

          $("aiOutAction").textContent = data.action || "unknown";
          $("aiOutBody").textContent = data.body_part || body;
          $("aiOutStage").textContent = data.stage || "unknown";
          $("aiOutScore").textContent = data.score != null ? (data.score * 100).toFixed(1) + "%" : "—";
          $("aiOutHeld").textContent = (data.held_ms || 0) + " ms";
          $("aiOutCal").textContent = data.calibrated ? "Calibrated (Ready)" : "Uncalibrated";
          $("aiReasonText").textContent = data.reason ? `สถานะโมเดล: ${data.reason}` : "";
        } else {
          $("aiServerBadge").className = "rec-badge";
          $("aiServerBadge").style.color = "var(--orange)";
          $("aiServerBadge").textContent = `HTTP ${res.status}`;
        }
      } catch (err) {
        $("aiServerBadge").className = "rec-badge";
        $("aiServerBadge").style.color = "var(--red)";
        $("aiServerBadge").textContent = "ออฟไลน์ (Offline - ตรวจสอบพอร์ต 8765)";
      } finally {
        if (aiPolling && generation === aiGeneration) aiTimerId = setTimeout(() => pollAiServer(generation), 200);
      }
    }

    // -------------------------------------------------------------
    // MQTT CONNECTION LOGIC
    // -------------------------------------------------------------
    const mqttUrlParams = new URLSearchParams(location.search);
    const requestedMqttPort = Number(mqttUrlParams.get("mqttPort") || 9001);
    const MQTT_CONFIG = Object.freeze({
      host: mqttUrlParams.get("broker") || "192.168.1.160",
      port: Number.isInteger(requestedMqttPort) && requestedMqttPort > 0 && requestedMqttPort <= 65535
        ? requestedMqttPort : 9001,
      path: mqttUrlParams.get("mqttPath") || "/mqtt",
      useSSL: mqttUrlParams.get("mqttTls") === "1",
      topic: "planko/imu",
      pressureTopic: "planko/pressure"
    });
    const MQTT_TRANSPORT = MQTT_CONFIG.useSSL ? "WSS" : "WS";
    $("brokerHost").value = MQTT_CONFIG.host;
    $("brokerHost").title = `${MQTT_CONFIG.host}:${MQTT_CONFIG.port} (${MQTT_TRANSPORT})`;
    let mqttReconnectTimer = null;
    let mqttGeneration = 0;
    let mqttConnectAttempt = 0;
    let mqttRetryCount = 0;
    let mqttRxTotal = 0, mqttRxWindow = 0, mqttGaps = 0, mqttDuplicates = 0;
    const mqttSequences = new Map();

    function mqttConsole(eventName, details = {}) {
      console.info(`[PlanKO MQTT] ${new Date().toISOString()} ${eventName}`, details);
    }

    function mqttErrorDetails(error) {
      if (error == null) return { value: String(error) };
      const details = {};
      for (const key of ["errorCode", "errorMessage", "message", "name", "type"]) {
        if (error[key] !== undefined) details[key] = error[key];
      }
      return Object.keys(details).length ? details : { value: String(error) };
    }

    let mqttWsProbeStarted = false;
    function probeMqttWebSocketOnce() {
      if (mqttWsProbeStarted || typeof WebSocket === "undefined") return;
      mqttWsProbeStarted = true;
      const scheme = MQTT_CONFIG.useSSL ? "wss" : "ws";
      const url = `${scheme}://${MQTT_CONFIG.host}:${MQTT_CONFIG.port}${MQTT_CONFIG.path}`;
      const startedAt = Date.now();
      let probe;
      let opened = false;
      let closedByProbe = false;
      mqttConsole("native WebSocket probe started", {
        url,
        subprotocol: "mqtt",
        pageOrigin: location.origin
      });
      try {
        probe = new WebSocket(url, ["mqtt"]);
      } catch (error) {
        mqttConsole("native WebSocket probe could not start", mqttErrorDetails(error));
        return;
      }
      probe.onopen = () => {
        opened = true;
        mqttConsole("native WebSocket probe opened", {
          elapsedMs: Date.now() - startedAt,
          negotiatedProtocol: probe.protocol
        });
        window.setTimeout(() => {
          if (probe.readyState === WebSocket.OPEN) {
            closedByProbe = true;
            probe.close(1000, "diagnostic probe complete");
          }
        }, 250);
      };
      probe.onerror = (event) => {
        mqttConsole("native WebSocket probe error event", {
          elapsedMs: Date.now() - startedAt,
          eventType: event.type,
          readyState: probe.readyState,
          note: "Browsers hide the underlying TLS/network error details for WebSocket errors."
        });
      };
      probe.onclose = (event) => {
        mqttConsole("native WebSocket probe closed", {
          elapsedMs: Date.now() - startedAt,
          opened,
          code: event.code,
          reason: event.reason,
          wasClean: event.wasClean,
          closedByProbe
        });
      };
    }

    window.addEventListener("online", () => {
      mqttConsole("browser online event", { navigatorOnline: navigator.onLine });
    });
    window.addEventListener("offline", () => {
      mqttConsole("browser offline event", { navigatorOnline: navigator.onLine });
    });

    function renderPressureReading(raw, voltage, pressed, source) {
      const safeRaw = Math.min(4095, Math.max(0, Number(raw) || 0));
      const safeVoltage = Math.max(0, Number(voltage) || 0);
      pressRaw.textContent = safeRaw.toLocaleString();
      pressVolt.textContent = safeVoltage.toFixed(2) + " V";
      pressFill.style.width = (safeRaw / 4095 * 100).toFixed(1) + "%";
      pressBadge.className = pressed ? "badge-press active" : "badge-press";
      pressBadge.textContent = `${pressed ? "PRESSED" : "RELEASED"}${source ? " · " + source : ""}`;
      pressSource.textContent = source || "RFP602 pressure input";
    }

    function handlePressureMessage(msg) {
      try {
        const data = JSON.parse(msg.payloadString);
        if (data?.type === "rfp605_stream" && Array.isArray(data.samples)) {
          const selectedGateway = $("gatewayFilter").value.trim();
          if (selectedGateway && data.gatewayId && data.gatewayId !== selectedGateway) return;
          let latest = null;
          for (const sample of data.samples) {
            const channel = Number(sample.channel);
            const raw = Number(sample.pressure_raw);
            const voltageMv = Number(sample.voltage_mV);
            if (!Number.isInteger(channel) || channel < 0 || channel > 15 ||
                !Number.isFinite(raw) || !Number.isFinite(voltageMv)) continue;
            const state = {channel, raw, voltage: voltageMv / 1000, pressed: sample.pressed === true};
            rfp605ChannelStates.set(channel, state);
            latest = state;
          }
          if (latest) {
            const active = [...rfp605ChannelStates.values()]
              .filter(state => state.pressed)
              .map(state => `C${String(state.channel).padStart(2, "0")}`);
            const source = active.length
              ? `RFP605 · pressed ${active.join(", ")}`
              : `RFP605 · C${String(latest.channel).padStart(2, "0")}`;
            renderPressureReading(latest.raw, latest.voltage, active.length > 0, source);
          }
          return;
        }
        if (Number.isFinite(Number(data?.raw)) && Number.isFinite(Number(data?.voltage))) {
          renderPressureReading(data.raw, data.voltage, data.pressed === true, "RFP602");
        }
      } catch (error) {
        mqttConsole("invalid pressure payload", {message: error.message});
      }
    }

    function onMqttMessageArrived(msg) {
      if (msg.retained) return;
      if (msg.destinationName === MQTT_CONFIG.pressureTopic) {
        handlePressureMessage(msg);
        return;
      }
      if (msg.destinationName !== MQTT_CONFIG.topic) return;
      try {
        const data = JSON.parse(msg.payloadString);
        if (!data || data.type !== "imu_stream" || !Array.isArray(data.nodes)) return;
        const gateway = data.gateway || {};
        if (!gateway.clientId) return;
        if (!$("gatewayFilter").value.trim()) $("gatewayFilter").value = gateway.clientId;
        const filter = $("gatewayFilter").value.trim();
        if (gateway.clientId !== filter) return;
        mqttRxTotal++;
        mqttRxWindow++;
        const seq = gateway.mqttSeq;
        if (gateway.clientId && Number.isInteger(seq)) {
          const previous = mqttSequences.get(gateway.clientId);
          if (previous !== undefined) {
            if (seq === previous) mqttDuplicates++;
            else if (seq > previous + 1) mqttGaps += seq - previous - 1;
          }
          // Lower sequence starts a new observation segment (e.g. gateway reboot).
          mqttSequences.set(gateway.clientId, seq);
        }
        if (mqttRxTotal === 1 || mqttRxTotal % 100 === 0) {
          mqttConsole("message received", {
            count: mqttRxTotal,
            topic: msg.destinationName,
            payloadBytes: new TextEncoder().encode(msg.payloadString || "").length,
            nodeCount: data.nodes.length
          });
        }
        // Pause freezes the visualization, while receive accounting continues.
        handleImuMessage(msg.payloadString);
      } catch (error) {
        console.warn("[PlanKO MQTT] Ignored malformed MQTT payload", error);
      }
    }

    let mqttRateWindowStart = performance.now();
    setInterval(() => {
      const now = performance.now();
      const hz = mqttRxWindow * 1000 / (now - mqttRateWindowStart);
      $("mqttRxStats").textContent = `RX: ${mqttRxTotal} | ${hz.toFixed(1)} Hz | Gaps: ${mqttGaps} | Duplicates: ${mqttDuplicates}`;
      mqttRxWindow = 0;
      mqttRateWindowStart = now;
    }, 1000);
    $("gatewayFilter").addEventListener("change", () => {
      mqttSequences.clear();
      mqttRxTotal = mqttRxWindow = mqttGaps = mqttDuplicates = 0;
      mqttRateWindowStart = performance.now();
      captureNodes.clear();
      $("recNodeList").textContent = "???????? IMU ??? Gateway ?????????";
      refreshRecordPairOptions();
    });

    function connectMqtt() {
      clearTimeout(mqttReconnectTimer);
      mqttReconnectTimer = null;
      const generation = ++mqttGeneration;
      const attempt = ++mqttConnectAttempt;
      const attemptStartedAt = Date.now();
      const previous = mqttClient;
      mqttClient = null;
      if (previous) {
        previous.onConnectionLost = () => {};
        try { previous.disconnect(); } catch (error) {}
      }
      const retry = (message) => {
        if (generation !== mqttGeneration) return;
        mqttRetryCount++;
        mqttConsole("retry scheduled", {
          attempt,
          retryCount: mqttRetryCount,
          generation,
          retryAfterMs: 4000,
          reason: message,
          navigatorOnline: navigator.onLine
        });
        setMqttStatus("offline", message + " ? retry in 4s");
        clearTimeout(mqttReconnectTimer);
        mqttReconnectTimer = setTimeout(connectMqtt, 4000);
      };
      setMqttStatus("connecting", `Connecting ${MQTT_CONFIG.host}:${MQTT_CONFIG.port} (${MQTT_TRANSPORT})...`);
      try {
        const clientId = "PlanKO_Web_" + Date.now().toString(16) + Math.random().toString(16).slice(2, 10);
        mqttConsole("connect attempt", {
          attempt,
          generation,
          clientId,
          host: MQTT_CONFIG.host,
          port: MQTT_CONFIG.port,
          path: MQTT_CONFIG.path,
          transport: MQTT_TRANSPORT,
          topics: [MQTT_CONFIG.topic, MQTT_CONFIG.pressureTopic],
          mqttVersion: 4,
          timeoutSeconds: 10,
          navigatorOnline: navigator.onLine,
          pageProtocol: location.protocol,
          pageOrigin: location.origin
        });
        const client = new Paho.MQTT.Client(MQTT_CONFIG.host, MQTT_CONFIG.port, MQTT_CONFIG.path, clientId);
        mqttClient = client;
        client.onMessageArrived = (msg) => {
          if (generation === mqttGeneration) onMqttMessageArrived(msg);
        };
        client.onConnectionLost = (error) => {
          mqttConsole("connection lost", {
            attempt,
            generation,
            connected: client.isConnected(),
            ...mqttErrorDetails(error)
          });
          retry("MQTT disconnected: " + (error.errorMessage || "connection lost"));
        };
        client.connect({
          useSSL: MQTT_CONFIG.useSSL, timeout: 10, keepAliveInterval: 30,
          cleanSession: true, mqttVersion: 4,
          onSuccess: () => {
            if (generation !== mqttGeneration) return;
            mqttConsole("MQTT CONNECT accepted", {
              attempt,
              generation,
              elapsedMs: Date.now() - attemptStartedAt,
              connected: client.isConnected()
            });
            const topics = [MQTT_CONFIG.topic, MQTT_CONFIG.pressureTopic];
            let pendingSubscriptions = topics.length;
            for (const topic of topics) {
              client.subscribe(topic, {
                qos: 0,
                onSuccess: () => {
                  if (generation !== mqttGeneration) return;
                  mqttConsole("SUBSCRIBE accepted", {attempt, generation, topic, qos: 0});
                  pendingSubscriptions--;
                  if (pendingSubscriptions === 0) {
                    setMqttStatus("online", `Connected ${MQTT_CONFIG.host}:${MQTT_CONFIG.port} (${MQTT_TRANSPORT})`);
                    sourceLabel.textContent = `Topics: ${topics.join(" + ")} | IMU target 20 Hz`;
                  }
                },
                onFailure: (error) => {
                  mqttConsole("SUBSCRIBE rejected", {
                    attempt, generation, topic, ...mqttErrorDetails(error)
                  });
                  retry("Subscribe failed: " + (error.errorMessage || "unknown error"));
                }
              });
            }
          },
          onFailure: (error) => {
            mqttConsole("MQTT CONNECT failed", {
              attempt,
              generation,
              elapsedMs: Date.now() - attemptStartedAt,
              ...mqttErrorDetails(error)
            });
            probeMqttWebSocketOnce();
            retry("MQTT connect failed: " + (error.errorMessage || "unknown error"));
          }
        });
      } catch (error) {
        mqttConsole("MQTT initialization failed", {
          attempt,
          generation,
          ...mqttErrorDetails(error)
        });
        retry("MQTT initialization failed: " + error.message);
      }
    }

    function setMqttStatus(state, text) {
      mqttConsole("status", { state, text });
      mqttDot.className = "status-dot " + (state === "online" ? "online" : state === "offline" ? "offline" : "");
      mqttStatusText.textContent = text;
    }

    // --- Process planko/imu JSON Payload ---
    function handleImuMessage(jsonStr) {
      try {
        const data = JSON.parse(jsonStr);
        if (data.type !== "imu_stream") return;

        const now = Date.now();
        // Gateway uptime
        if (data.gateway && data.gateway.uptimeMs) {
          const sec = Math.floor(data.gateway.uptimeMs / 1000);
          uptimeText.textContent = `${Math.floor(sec / 60)}m ${sec % 60}s`;
        }

        // Pressure Sensor
        if (data.pressure) {
          renderPressureReading(data.pressure.raw, data.pressure.voltage,
            data.pressure.pressed === true, "RFP602");
        }

        // Process Nodes
        if (Array.isArray(data.nodes)) {
          updateCaptureNodes(data);
          if (isRecording) {
            const selectedData = {...data, nodes: data.nodes.filter(node => recordingNodeIds.has(Number(node.nodeId)))};
            datasetRecorder.ingest(selectedData, () => sharedActionLabel.value, {
              phase: currentPhase,
              source: guidedTimerCheck.checked ? "guided_timer_cue" : "manual_live",
              reviewed: !guidedTimerCheck.checked && $("recPhaseReviewed").checked
            }, now);
          }
          updatePlankNodeOptions(data.nodes);
          updateNodeBatteryData(data.nodes);
          processPlankMessage(data);
          const node = data.nodes.find(n => n.nodeId === selectedNodeId);
          if (node) {
            targetRoll = (node.roll || 0) - tareRoll;
            targetPitch = (node.pitch || 0) - tarePitch;
            targetYaw = (node.yaw || 0) - tareYaw;

            rollValue.textContent = targetRoll.toFixed(1);
            pitchValue.textContent = targetPitch.toFixed(1);
            yawValue.textContent = targetYaw.toFixed(1);

            let ax = 0, ay = 0, az = 0;
            if (node.accel && Array.isArray(node.accel) && node.accel.length >= 3) {
              ax = node.accel[0]; ay = node.accel[1]; az = node.accel[2];
              axValue.textContent = ax.toFixed(2);
              ayValue.textContent = ay.toFixed(2);
              azValue.textContent = az.toFixed(2);
            }

            seqText.textContent = node.seq ?? "—";
            sensorPill.textContent = node.isMock ? `Node #${node.nodeId} (Mock)` : `Node #${node.nodeId} (Real IMU)`;

            if (node.mount_site && POSE_TAXONOMY.mount_sites[node.mount_site]) {
              recSiteSelect.value = node.mount_site;
            }
            const mappedBody = PlanKOImu.trainingBody(node);
            if (mappedBody && POSE_TAXONOMY.body_parts[mappedBody]) {
              recBodyPartSelect.value = mappedBody;
            } else if (node.body_part && POSE_TAXONOMY.body_parts[node.body_part]) {
              recBodyPartSelect.value = node.body_part;
            }

            let row;
            try {
              row = PlanKOImu.sampleFromMessage(data, node, {
                mount_site: node.mount_site || recSiteSelect.value,
                body_part: node.body_part || recBodyPartSelect.value,
                side: node.side || $("recSideSelect").value
              });
              $("sampleContractStatus").textContent = "Six axes ready: m/s^2 + rad/s";
            } catch (error) {
              inferenceWindow.reset(); latestSample = null;
              $("sampleContractStatus").textContent = error.message;
              return;
            }
            if (!inferenceWindow.add(row)) return;
            latestSample = row;
            lastSampleArrival = Date.now();

          }
        }
      } catch (err) {
        console.warn("JSON parse error:", err);
      }
    }

    for (const id of ["nodeSelect", "recSiteSelect", "recBodyPartSelect", "recSideSelect", "gatewayFilter"]) {
      $(id).addEventListener("change", () => {
        inferenceWindow.reset(); latestSample = null; lastInferenceKey = "";
      });
    }

    // --- Event Listeners ---
    zeroBtn.addEventListener("click", () => {
      tareRoll += targetRoll;
      tarePitch += targetPitch;
      tareYaw += targetYaw;
      targetRoll = 0; targetPitch = 0; targetYaw = 0;
      currentRoll = 0; currentPitch = 0; currentYaw = 0;
    });

    pauseBtn.addEventListener("click", () => {
      isPaused = !isPaused;
      pauseBtn.textContent = isPaused ? "▶ เล่นต่อ" : "⏸ พักสตรีม";
      pauseBtn.style.color = isPaused ? "var(--mint)" : "var(--text)";
    });

    reconnectBtn.addEventListener("click", connectMqtt);

    nodeSelect.addEventListener("change", (e) => {
      selectedNodeId = parseInt(e.target.value, 10);
      tareRoll = 0; tarePitch = 0; tareYaw = 0;
    });

    // -------------------------------------------------------------
    // 3D RENDERING PIPELINE (Canvas Isometric Projection)
    // -------------------------------------------------------------
    function rotateEuler(v, r, p, y) {
      const cr = Math.cos(r), sr = Math.sin(r);
      const cp = Math.cos(p), sp = Math.sin(p);
      const cy = Math.cos(y), sy = Math.sin(y);

      const x1 = cy * v[0] - sy * v[1];
      const y1 = sy * v[0] + cy * v[1];
      const z1 = v[2];

      const x2 = cp * x1 + sp * z1;
      const y2 = y1;
      const z2 = -sp * x1 + cp * z1;

      const x3 = x2;
      const y3 = cr * y2 - sr * z2;
      const z3 = sr * y2 + cr * z2;

      return [x3, y3, z3];
    }

    function projectIso(v, cx, cy, scale) {
      const isoX = (v[0] - v[1]) * 0.866025;
      const isoY = (v[0] + v[1]) * 0.5 - v[2];
      return [cx + isoX * scale, cy + isoY * scale];
    }

    let renderCount = 0, renderWindowStart = performance.now();
    function drawScene() {
      renderCount++;
      const renderNow = performance.now();
      if (renderNow - renderWindowStart >= 1000) {
        fpsText.textContent = (renderCount * 1000 / (renderNow - renderWindowStart)).toFixed(1) + " FPS";
        renderCount = 0; renderWindowStart = renderNow;
      }
      if (!isPaused) {
        currentRoll  += (targetRoll  - currentRoll)  * 0.25;
        currentPitch += (targetPitch - currentPitch) * 0.25;
        currentYaw   += (targetYaw   - currentYaw)   * 0.25;
      }

      ctx.clearRect(0, 0, canvas.width, canvas.height);
      const w = canvas.width;
      const h = canvas.height;
      const cx = w * 0.5;
      const cy = h * 0.52;
      const scale = Math.min(w, h) * 0.0022;

      // Draw Grid
      ctx.strokeStyle = "rgba(25, 42, 56, 0.45)";
      ctx.lineWidth = 1;
      ctx.beginPath();
      for (let x = -200; x <= 200; x += 50) {
        const p1 = projectIso([x, -200, -80], cx, cy, scale);
        const p2 = projectIso([x, 200, -80], cx, cy, scale);
        ctx.moveTo(p1[0], p1[1]); ctx.lineTo(p2[0], p2[1]);
      }
      for (let y = -200; y <= 200; y += 50) {
        const p1 = projectIso([-200, y, -80], cx, cy, scale);
        const p2 = projectIso([200, y, -80], cx, cy, scale);
        ctx.moveTo(p1[0], p1[1]); ctx.lineTo(p2[0], p2[1]);
      }
      ctx.stroke();

      const rRad = currentRoll * DEG;
      const pRad = currentPitch * DEG;
      const yRad = currentYaw * DEG;

      // Board Dimensions
      const lx = 90, ly = 130, lz = 8;
      const rawVerts = [
        [-lx, -ly, -lz], [ lx, -ly, -lz], [ lx,  ly, -lz], [-lx,  ly, -lz],
        [-lx, -ly,  lz], [ lx, -ly,  lz], [ lx,  ly,  lz], [-lx,  ly,  lz]
      ];
      const transVerts = rawVerts.map(v => rotateEuler(v, rRad, pRad, yRad));
      const projVerts = transVerts.map(v => projectIso(v, cx, cy, scale));

      const faces = [
        { v: [4, 5, 6, 7], col: "#173024", norm: [0, 0, 1] },
        { v: [0, 1, 2, 3], col: "#0f1c24", norm: [0, 0, -1] },
        { v: [0, 1, 5, 4], col: "#12232e", norm: [0, -1, 0] },
        { v: [2, 3, 7, 6], col: "#12232e", norm: [0, 1, 0] },
        { v: [1, 2, 6, 5], col: "#152a36", norm: [1, 0, 0] },
        { v: [0, 3, 7, 4], col: "#152a36", norm: [-1, 0, 0] }
      ];

      faces.forEach(f => {
        const normRot = rotateEuler(f.norm, rRad, pRad, yRad);
        f.depth = normRot[0] * 0.5 + normRot[1] * 0.5 - normRot[2];
      });
      faces.sort((a, b) => a.depth - b.depth);

      // Draw Faces
      faces.forEach(f => {
        ctx.fillStyle = f.col;
        ctx.strokeStyle = "rgba(116, 240, 194, 0.4)";
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        f.v.forEach((vi, idx) => {
          const pt = projVerts[vi];
          if (idx === 0) ctx.moveTo(pt[0], pt[1]);
          else ctx.lineTo(pt[0], pt[1]);
        });
        ctx.closePath();
        ctx.fill();
        ctx.stroke();
      });

      // Board Axes
      const axisLen = 130;
      const axes = [
        { dir: [axisLen, 0, 0], col: "#ff7777", label: "X" },
        { dir: [0, axisLen, 0], col: "#74f0c2", label: "Y" },
        { dir: [0, 0, axisLen], col: "#6ca9ff", label: "Z" }
      ];
      const pCenter = projectIso(rotateEuler([0, 0, 0], rRad, pRad, yRad), cx, cy, scale);

      axes.forEach(ax => {
        const pEnd = projectIso(rotateEuler(ax.dir, rRad, pRad, yRad), cx, cy, scale);
        ctx.strokeStyle = ax.col;
        ctx.lineWidth = 3;
        ctx.beginPath();
        ctx.moveTo(pCenter[0], pCenter[1]);
        ctx.lineTo(pEnd[0], pEnd[1]);
        ctx.stroke();

        ctx.fillStyle = ax.col;
        ctx.font = "bold 13px Inter, sans-serif";
        ctx.fillText(ax.label, pEnd[0] + 6, pEnd[1] + 4);
      });

      // World Reference Axes in lower-left
      const wx = 55, wy = h - 55;
      const wAxes = [
        { dir: [35, 0, 0], col: "#ff7777", label: "Xw" },
        { dir: [0, 35, 0], col: "#74f0c2", label: "Yw" },
        { dir: [0, 0, 35], col: "#6ca9ff", label: "Zw" }
      ];
      wAxes.forEach(ax => {
        const end = projectIso(ax.dir, wx, wy, 0.9);
        ctx.strokeStyle = ax.col;
        ctx.lineWidth = 2;
        ctx.beginPath();
        ctx.moveTo(wx, wy);
        ctx.lineTo(end[0], end[1]);
        ctx.stroke();
      });

      requestAnimationFrame(drawScene);
    }

    function resizeCanvas() {
      const rect = canvas.getBoundingClientRect();
      const ratio = Math.min(window.devicePixelRatio || 1, 2);
      canvas.width = Math.max(1, Math.round(rect.width * ratio));
      canvas.height = Math.max(1, Math.round(rect.height * ratio));
    }

    window.addEventListener("resize", resizeCanvas);
    resizeCanvas();

    // Start 3D Loop & MQTT Connection
    requestAnimationFrame(drawScene);
    connectMqtt();
  </script>
</body>
</html>
```

### 6.20 ไฟล์ `WEB/index.js` — สคริปต์เสริมหน้าเว็บ (44 บรรทัด)
```javascript
var device;
var characteristic;

async function connect() {
  console.log("🔍 Searching device...");

  device = await navigator.bluetooth.requestDevice({
    filters: [{ namePrefix: "ESP32" }],
    optionalServices: ["12345678-1234-1234-1234-1234567890ab"],
  });

  console.log("📱 Device selected:", device.name);

  const server = await device.gatt.connect();
  console.log("🔗 GATT connected");

  const service = await server.getPrimaryService(
    "12345678-1234-1234-1234-1234567890ab",
  );
  console.log("📡 Service found");

  characteristic = await service.getCharacteristic(
    "abcd1234-5678-1234-5678-abcdef123456",
  );

  console.log("✅ Characteristic ready");
}

async function sendData() {
  if (!characteristic) {
    console.log("❌ ยังไม่ได้ connect");
    return;
  }

  const value = "APISIT164 @2.4G,09E2898E";
  const encoder = new TextEncoder();

  console.log("📤 Sending:", value);

  await characteristic.writeValueWithResponse(encoder.encode(value));

  console.log("✅ Send success");
}
```

### 6.21 ไฟล์ `model_test.html` — หน้าเว็บสำหรับทดสอบโมเดล AI (193 บรรทัด)
```html
<!doctype html>
<html lang="th">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>ทดสอบ IMU Pose Model</title>
  <style>
    :root{color-scheme:dark;--bg:#101820;--panel:#182631;--line:#2d4350;--muted:#9eb0bb;--text:#edf5f8;--mint:#74f0c2;--blue:#8dbbff;--red:#ff9292}
    *{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px/1.5 system-ui,"Segoe UI",sans-serif}
    main{max-width:1100px;margin:0 auto;padding:24px}h1{font-size:25px;margin:0}h2{font-size:16px;margin:0 0 12px}.muted{color:var(--muted);font-size:13px}
    .top,.panel{background:var(--panel);border:1px solid var(--line);border-radius:14px;padding:18px;margin-bottom:14px}.top{display:flex;justify-content:space-between;gap:12px;align-items:center}
    .grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(190px,1fr));gap:12px}.field{display:grid;gap:5px;color:var(--muted);font-size:12px}
    input,select,button{min-height:40px;border:1px solid var(--line);border-radius:8px;background:#0d151e;color:var(--text);padding:8px 10px;font:inherit}button{cursor:pointer;background:#213846}button.primary{background:#225a50;border-color:#347d70}button:disabled{opacity:.5;cursor:default}
    .actions{display:flex;gap:8px;flex-wrap:wrap;margin-top:12px}.status{padding:4px 10px;border-radius:99px;background:#20323d;color:var(--blue);font-size:12px;white-space:nowrap}
    .prediction{display:grid;grid-template-columns:repeat(auto-fit,minmax(145px,1fr));gap:10px}.metric{padding:12px;border:1px solid var(--line);border-radius:10px;background:#101b23}.metric small{display:block;color:var(--muted);font-size:11px}.metric strong{display:block;font-size:17px;margin-top:4px;overflow-wrap:anywhere}
    input[type=range]{width:100%;padding:0}.note{font-size:12px;color:var(--muted);margin:8px 0 0}.error{color:var(--red)}.ok{color:var(--mint)}
    #log{width:100%;min-height:120px;font:12px ui-monospace,monospace}.hidden{display:none}
    @media(max-width:600px){main{padding:12px}.top{align-items:flex-start;flex-direction:column}}
  </style>
</head>
<body>
<main>
  <header class="top"><div><h1>ทดสอบ IMU Pose Model</h1><div id="healthText" class="muted">กำลังโหลด model metadata…</div></div><span id="modeStatus" class="status">หยุด</span></header>

  <section class="panel">
    <h2>แหล่งข้อมูลและตำแหน่ง</h2>
    <div class="grid">
      <label class="field">โหมดข้อมูล<select id="sourceMode"><option value="csv">CSV replay</option><option value="live">Live จาก ESP</option></select></label>
      <label class="field">IP ของ ESP<input id="espIp" value="192.168.4.1" inputmode="decimal"></label>
      <label class="field">ตำแหน่งติด IMU<select id="site"></select></label>
      <label class="field">ส่วนร่างกาย<select id="bodyPart"></select></label>
      <label class="field">ข้าง<select id="side"><option value="right">ขวา</option><option value="left">ซ้าย</option><option value="unknown">ไม่ระบุ</option></select></label>
      <label class="field" id="csvField">ไฟล์ข้อมูล<input id="csvFile" type="file" accept=".csv,text/csv"></label>
    </div>
    <div class="actions"><button id="connect" class="primary">เริ่ม Live</button><button id="loadCsv">โหลด CSV</button><button id="play" disabled>เล่น</button><button id="pause" disabled>พัก</button><button id="downloadLog">ดาวน์โหลด prediction log</button></div>
    <p id="sourceNote" class="note">หน้าเว็บนี้ใช้ model บน PC; สถานะ threshold จาก firmware แสดงแยกจากผล ML</p>
  </section>

  <section class="panel"><h2>Prediction</h2><div class="prediction">
    <div class="metric"><small>ตำแหน่งร่างกาย</small><strong id="outBody">—</strong></div>
    <div class="metric"><small>ท่า</small><strong id="outAction">—</strong></div>
    <div class="metric"><small>ระยะ</small><strong id="outStage">unknown</strong></div>
    <div class="metric"><small>คะแนนโมเดล</small><strong id="outScore">—</strong></div>
    <div class="metric"><small>เวลาค้าง</small><strong id="outHeld">0 ms</strong></div>
    <div class="metric"><small>Calibration / model</small><strong id="outCal">—</strong></div>
  </div><p id="reason" class="note">ยังไม่มีข้อมูล</p><p id="firmwareStatus" class="note">Firmware: —</p></section>

  <section id="csvPanel" class="panel"><h2>เล่นข้อมูลตามเวลา</h2><input id="timeline" type="range" min="0" max="0" value="0" disabled><p id="csvInfo" class="note">เลือก CSV ที่เก็บจากหน้า recorder หรือ tools/capture.py</p></section>
  <section class="panel"><h2>Prediction log</h2><textarea id="log" readonly></textarea></section>
</main>
<script>
"use strict";
const $=id=>document.getElementById(id);let metadata=null, csvRows=[], timer=0, cursor=0, liveTimer=0, logRows=[], replayBusy=false;
function setStatus(text){$("modeStatus").textContent=text;}
function showPrediction(p){
  const actionEntry=metadata?.body_part_config?.[p.body_part]?.actions?.find(action=>action.id===p.action);
  $("outBody").textContent=p.body_part||"unknown";$("outAction").textContent=actionEntry?`${actionEntry.label} (${p.action})`:p.action||"unknown";
  $("outStage").textContent=p.stage||"unknown";$("outScore").textContent=p.score==null?"—":`${p.score} (${p.score_kind||"model score"})`;
  $("outHeld").textContent=(p.held_ms||0)+" ms";$("outCal").textContent=(p.calibrated?"ready":"not ready")+" / cal "+(p.calibration_id||"?")+" / method "+(p.calibration_method_version||"?")+" / uptime "+(p.calibration_time_boot_ms||"?")+" @ "+(p.calibration_boot_id||"?")+" / "+(p.model_version||"?");
  $("reason").textContent=p.reason||"";$("firmwareStatus").textContent=p.firmware_status?`Firmware status (แยกจาก ML): ${p.firmware_status}`:"Firmware status: —";
  if(!p.duplicate_sample)logRows.push({time:new Date().toISOString(),...p});$("log").value=JSON.stringify(logRows.slice(-100),null,2);
}
function parseCsv(text){
  const rows=[];let row=[],cell="",quoted=false;
  for(let i=0;i<text.length;i++){const c=text[i];if(quoted){if(c==='"'&&text[i+1]==='"'){cell+='"';i++;}else if(c==='"')quoted=false;else cell+=c;}else if(c==='"')quoted=true;else if(c===','){row.push(cell);cell="";}else if(c==='\n'){row.push(cell.replace(/\r$/,''));rows.push(row);row=[];cell="";}else cell+=c;}
  if(cell.length||row.length){row.push(cell);rows.push(row);}if(rows.length<2)return[];const headers=rows.shift().map(h=>h.trim());
  return rows.filter(r=>r.length===headers.length).map(r=>Object.fromEntries(headers.map((h,i)=>[h,r[i]])));
}
function toModelRow(r,index){
  const boot=Number(r.boot_ms||r.sample_ms);const host=Number(r.host_time_s||r.host_timestamp_s);
  const t=Number.isFinite(boot)&&boot>0?boot/1000:Number.isFinite(host)?host:index*1/(metadata?.sampling_hz||20);
  const accelerationUnit=String(r.unit_accel||r.accel_unit||"").toLowerCase();const accelScale=accelerationUnit==="g"||accelerationUnit.includes("(g)")?9.80665:1;
  const a=(k,legacy)=>{const value=r[k]??r[legacy];return value===""||value==null?Number.NaN:Number(value);};
  const calibrated=[true,"true","1",1].includes(r.calibrated);
  const recordedSide=String(r.side||"").trim().toLowerCase();
  const selectedSide=$("side").value;
  return {sensor_uid:r.sensor_uid||r.firmware_sensor_id||r.sensor_id||"csv",mount_site:r.mount_site||r.recorded_site||r.sensor_id||$("site").value,body_part:r.body_part||$("bodyPart").value,side:(!recordedSide||recordedSide==="unknown")?selectedSide:recordedSide,seq:r.seq==null||r.seq===""?index:Number(r.seq),device_time_s:t,host_time_s:Number.isFinite(host)?host:t,boot_ms:boot||Math.round(t*1000),stream_epoch:r.stream_epoch||0,calibration_id:r.calibration_id||"",calibration_method_version:Number(r.calibration_method_version||0),calibration_time_boot_ms:Number(r.calibration_time_boot_ms||0),calibration_boot_id:Number(r.calibration_boot_id||0),calibrated,relative_angle_valid:[true,"true","1",1].includes(r.relative_angle_valid),relative_orientation_valid:r.relative_orientation_valid==null||r.relative_orientation_valid===""?calibrated:[true,"true","1",1].includes(r.relative_orientation_valid),relative_roll_deg:Number(r.relative_roll_deg||0),relative_pitch_deg:Number(r.relative_pitch_deg??r.relative_tilt_deg??0),valid:![false,"false","0",0].includes(r.valid),accel_x:a("accel_x","ax")*accelScale,accel_y:a("accel_y","ay")*accelScale,accel_z:a("accel_z","az")*accelScale,gyro_x:a("gyro_x","gx"),gyro_y:a("gyro_y","gy"),gyro_z:a("gyro_z","gz")};
}
async function predictWindow(rows,context={}){const response=await fetch("/api/predict-window",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify({rows,mount_site:context.mount_site||$("site").value,body_part:context.body_part||$("bodyPart").value,stream_key:context.stream_key,stream_context:context.stream_context,device_time_s:context.device_time_s,seq:context.seq})});showPrediction(await response.json());}
async function replayAt(index){if(!csvRows.length)return;const now=csvRows[index].device_time_s,window=metadata?.window_seconds||1;let first=index;while(first>0&&now-csvRows[first-1].device_time_s<window)first--;const samples=csvRows.slice(first,index+1);$("timeline").value=index;$("csvInfo").textContent=`แถว ${index+1}/${csvRows.length} · ${now.toFixed(2)} s · ${samples.length} samples`;if(samples.length>2)await predictWindow(samples);else showPrediction({body_part:$("bodyPart").value,action:"unknown",stage:"unknown",reason:"warming_up",calibrated:false});}
async function loadMetadata(){try{const r=await fetch("/api/health");metadata=await r.json();if(!r.ok)throw Error(metadata.reason);$("healthText").textContent=`Model ${metadata.model_version} · ${metadata.action_labels.length} action classes · phase ${metadata.phase_available?metadata.phase_labels.join(", "):"ยังไม่มี label จึงเป็น unknown"}`;$("site").innerHTML=metadata.mount_sites.map(s=>`<option value="${s}">${s}</option>`).join("");$("bodyPart").innerHTML=metadata.body_parts.map(s=>`<option value="${s}">${metadata.body_part_config[s]?.label||s}</option>`).join("");$("side").value=metadata.sides.includes("unknown")?"unknown":metadata.sides[0]||"unknown";}catch(e){$("healthText").textContent=`Model ยังไม่พร้อม: ${e.message}`;setStatus("ไม่มี model");}}
$("sourceMode").addEventListener("change",()=>{const live=$("sourceMode").value==="live";$("csvField").classList.toggle("hidden",live);$("csvPanel").classList.toggle("hidden",live);$("connect").textContent=live?"เริ่ม Live":"หยุด Live";});
$("bodyPart").addEventListener("change",()=>{if(metadata){const body=metadata.body_part_config[$("bodyPart").value];if(body?.actions?.length)$("sourceNote").textContent=`ท่าที่กำหนด: ${body.actions.map(a=>a.label).join(" / ")}`;}});
$("connect").addEventListener("click",()=>{if(liveTimer){clearInterval(liveTimer);liveTimer=0;setStatus("หยุด");return;}if($("sourceMode").value!=="live"){setStatus("เลือก live mode ก่อน");return;}let livePullBusy=false;const pull=async()=>{if(livePullBusy)return;livePullBusy=true;try{const q=new URLSearchParams({ip:$ ("espIp").value.trim(),mount_site:$ ("site").value,body_part:$ ("bodyPart").value,side:$ ("side").value});const r=await fetch(`/api/live?${q}`);showPrediction(await r.json());setStatus(r.ok?"Live":"อ่าน ESP ไม่สำเร็จ");}catch(e){setStatus(e.message);}finally{livePullBusy=false;}};pull();liveTimer=setInterval(pull,100);});
$("loadCsv").addEventListener("click",async()=>{
  const file=$("csvFile").files[0];
  if(!file||!metadata)return;
  const raw=parseCsv(await file.text());
  const legacyAction=metadata.legacy_file_actions?.[file.name.toLowerCase()]||"";
  const mappedBody=legacyAction?Object.entries(metadata.body_part_config).find(([,body])=>body.actions?.some(action=>action.id===legacyAction))?.[0]:"";
  csvRows=raw.map((source,index)=>{
    const row=toModelRow(source,index);
    return Object.assign(row,{
      body_part:source.body_part||(mappedBody||row.body_part),
      action_label:source.action_label||source.pose_label||legacyAction,
      phase_label:source.phase_label||"",
      calibration_method_version:Number(source.calibration_method_version||0),
      calibration_time_boot_ms:Number(source.calibration_time_boot_ms||0),
      calibration_boot_id:Number(source.calibration_boot_id||0),
      trial_id:source.trial_id||source.recording_id||file.name,
      session_id:source.session_id||"",
      subject_id:source.subject_id||""
    });
  });
  let epoch=0,previous=null;
  for(const row of csvRows){
    const reboot=previous&&(row.seq<=previous.seq||row.device_time_s<=previous.device_time_s);
    const calibrationChanged=previous&&row.calibration_id!==previous.calibration_id;
    if(reboot||calibrationChanged)epoch++;
    row.stream_epoch=epoch;
    row.gap_before=!!(previous&&row.seq!==previous.seq+1);
    previous=row;
  }
  if(csvRows.length){
    for(const [select,value] of [[$("site"),csvRows[0].mount_site],[$("bodyPart"),csvRows[0].body_part]]){
      if(value&&!Array.from(select.options).some(option=>option.value===value)){
        const option=document.createElement("option");option.value=value;option.textContent=value+" (not seen in training)";select.appendChild(option);
      }
      if(value)select.value=value;
    }
  }
  cursor=0;$("timeline").max=Math.max(0,csvRows.length-1);$("timeline").value=0;
  $("timeline").disabled=!csvRows.length;$("play").disabled=!csvRows.length;
  $("csvInfo").textContent=file.name+": "+csvRows.length+" samples; trials "+new Set(csvRows.map(row=>row.trial_id)).size;
  if(csvRows.length)await replayAt(0);
  setStatus("CSV loaded");
});
$("timeline").addEventListener("input",()=>replayAt(Number($("timeline").value)));
$("play").addEventListener("click",()=>{
  if(timer||!csvRows.length)return;
  const interval=Math.max(20,Math.floor(1000/(metadata?.sampling_hz||20)));
  timer=setInterval(async()=>{
    if(replayBusy)return;
    if(cursor>=csvRows.length-1){clearInterval(timer);timer=0;setStatus("Playback complete");return;}
    cursor++;$("timeline").value=cursor;replayBusy=true;
    try{await replayAt(cursor);}finally{replayBusy=false;}
  },interval);
  setStatus("Playing CSV");
});
$("pause").addEventListener("click",()=>{clearInterval(timer);timer=0;setStatus("CSV paused");});
$("downloadLog").addEventListener("click",()=>{const blob=new Blob([JSON.stringify(logRows,null,2)],{type:"application/json"});const a=document.createElement("a");a.href=URL.createObjectURL(blob);a.download="imu_prediction_log.json";a.click();URL.revokeObjectURL(a.href);});
let replayTruth = {action:"",phase:""};
const replayStreamKey = `csv-replay-${crypto.randomUUID ? crypto.randomUUID() : Date.now()}`;
const baseShowPrediction = showPrediction;
showPrediction = function (prediction) {
  prediction.device_time_s = replayTruth.device_time_s ?? prediction.device_time_s ?? null;
  prediction.sample_seq = replayTruth.seq ?? prediction.seq ?? null;
  prediction.reference_calibration_id = replayTruth.calibration_id || prediction.reference_calibration_id || prediction.calibration_id || "";
  prediction.reference_action_label = replayTruth.action || "";
  prediction.reference_phase_label = replayTruth.phase || "";
  prediction.reference_trial_id = replayTruth.trial || "";
  prediction.reference_session_id = replayTruth.session || "";
  prediction.reference_subject_id = replayTruth.subject || "";
  prediction.reference_mount_site = replayTruth.site || "";
  baseShowPrediction(prediction);
  let truth = document.getElementById("outTruth");
  if (!truth) { const node = document.createElement("div"); node.className = "metric"; node.innerHTML = "<small>Label ใน CSV</small><strong id=\"outTruth\">—</strong>"; document.querySelector(".prediction").appendChild(node); truth = document.getElementById("outTruth"); }
  truth.textContent = replayTruth.action ? `${replayTruth.action} / ${replayTruth.phase || "ไม่ระบุ phase"}` : "—";
};
replayAt = async function (index) {
  if (!csvRows.length) return;
  cursor = index;
  const anchor = csvRows[index], now = anchor.device_time_s, duration = metadata?.window_seconds || 1;
  let first = index;
  while (first > 0) {
    const prior = csvRows[first - 1], next = csvRows[first];
    const gap = next.device_time_s - prior.device_time_s;
    if (prior.stream_epoch !== anchor.stream_epoch || prior.calibration_id !== anchor.calibration_id ||
        prior.trial_id !== anchor.trial_id || prior.session_id !== anchor.session_id ||
        prior.subject_id !== anchor.subject_id || prior.sensor_uid !== anchor.sensor_uid ||
        prior.mount_site !== anchor.mount_site || prior.body_part !== anchor.body_part || prior.side !== anchor.side ||
        prior.gap_before || next.gap_before || !prior.valid || !next.valid || prior.seq + 1 !== next.seq ||
        gap <= 0 || gap > (metadata?.gap_reset_seconds||0.15) || now - prior.device_time_s > duration + 1e-9) break;
    first--;
  }
  const rows = csvRows.slice(first, index + 1);
  replayTruth = {action: anchor.action_label || "", phase: anchor.phase_label || "",
    trial: anchor.trial_id || "", session: anchor.session_id || "", subject: anchor.subject_id || "", site: anchor.mount_site || "", device_time_s:anchor.device_time_s, seq:anchor.seq, calibration_id:anchor.calibration_id||""};
  $("timeline").value = index;
  $("csvInfo").textContent = `Sample ${index + 1}/${csvRows.length}; ${rows.length} in current window`;
  if (!anchor.valid) showPrediction({body_part: anchor.body_part, action:"unknown", stage:"unknown", score:null, held_ms:0, calibrated:anchor.calibrated, reason:"sample_invalid"});
  else if (rows.length > 2) await predictWindow(rows, {mount_site:anchor.mount_site, body_part:anchor.body_part,
    stream_key:replayStreamKey, device_time_s:anchor.device_time_s, seq:anchor.seq,
    stream_context:{sensor_uid:anchor.sensor_uid,mount_site:anchor.mount_site,body_part:anchor.body_part,side:anchor.side,
      subject_id:anchor.subject_id,session_id:anchor.session_id,trial_id:anchor.trial_id,stream_epoch:anchor.stream_epoch,
      calibration_id:anchor.calibration_id,calibration_method_version:anchor.calibration_method_version}});
  else showPrediction({body_part: anchor.body_part, action:"unknown", stage:"unknown", score:null, held_ms:0, calibrated:anchor.calibrated, reason:"window_too_short"});
};
loadMetadata();
</script>
</body>
</html>
```

### 6.22 ไฟล์ `config/pose_labels.json` — ตั้งค่าตำแหน่งติดตั้ง (mount_sites), ส่วนร่างกาย (body_parts) และท่าทาง (action_labels / case_labels) (251 บรรทัด)
```json
{
  "schema_version": 1,
  "sampling_hz": 20,
  "window_seconds": [
    0.5,
    1.0
  ],
  "phases": [
    "neutral",
    "approaching",
    "holding",
    "returning"
  ],
  "case_labels": [
    {"id": "case_neutral", "label": "ท่าปกติ (neutral)"},
    {"id": "case_flexion", "label": "ก้ม/งอ (flexion)"},
    {"id": "case_extension", "label": "แอ่น/เหยียด (extension)"},
    {"id": "case_lateral_left", "label": "เอียงซ้าย (lateral left)"},
    {"id": "case_lateral_right", "label": "เอียงขวา (lateral right)"},
    {"id": "elbow_neutral", "label": "Forearm plank neutral"},
    {"id": "elbow_flexion", "label": "Forearm plank flexion"},
    {"id": "elbow_extension", "label": "Forearm plank extension"},
    {"id": "hand_neutral", "label": "Hand plank neutral"},
    {"id": "hand_flexion", "label": "Hand plank flexion"},
    {"id": "hand_extension", "label": "Hand plank extension"}
  ],
  "mount_sites": {
    "wrist": {
      "label": "หลังมือ/ข้อมือ",
      "body_axis_profile": "wrist_back_of_hand"
    },
    "right_forearm": {
      "label": "ท่อนแขนขวา",
      "body_axis_profile": "right_forearm"
    },
    "left_forearm": {
      "label": "ท่อนแขนซ้าย",
      "body_axis_profile": "left_forearm"
    },
    "right_upper_arm": {
      "label": "ต้นแขนขวา",
      "body_axis_profile": "right_upper_arm"
    },
    "left_upper_arm": {
      "label": "ต้นแขนซ้าย",
      "body_axis_profile": "left_upper_arm"
    },
    "chest": {
      "label": "หน้าอก",
      "body_axis_profile": "chest"
    },
    "neck": {
      "label": "?????",
      "body_axis_profile": "neck"
    },
    "head": {
      "label": "ศีรษะ",
      "body_axis_profile": "head"
    },
    "lumbar": {
      "label": "???????????????",
      "body_axis_profile": "lumbar"
    },
    "pelvis": {
      "label": "????????/?????",
      "body_axis_profile": "pelvis"
    },
    "left_thigh": {
      "label": "ต้นขาซ้าย",
      "body_axis_profile": "left_thigh"
    },
    "right_thigh": {
      "label": "ต้นขาขวา",
      "body_axis_profile": "right_thigh"
    },
    "left_shin": {
      "label": "หน้าแข้งซ้าย",
      "body_axis_profile": "left_shin"
    },
    "right_shin": {
      "label": "หน้าแข้งขวา",
      "body_axis_profile": "right_shin"
    },
    "shoulder": {
      "label": "หัวไหล่",
      "body_axis_profile": "shoulder"
    },
    "elbow": {
      "label": "ข้อศอก",
      "body_axis_profile": "elbow"
    },
    "other": {
      "label": "อื่น ๆ",
      "body_axis_profile": "manual"
    },
    "upper_back": {
      "label": "หลังส่วนบน (Upper Back)",
      "body_axis_profile": "upper_back"
    }
  },
  "body_parts": {
    "back": {
      "label": "หลัง",
      "actions": []
    },
    "wrist": {
      "label": "ข้อมือ",
      "actions": [
        {
          "id": "wrist_up",
          "label": "งัดขึ้น",
          "legacy_files": [
            "up.csv"
          ]
        },
        {
          "id": "wrist_down",
          "label": "งัดลง",
          "legacy_files": [
            "down.csv"
          ]
        },
        {
          "id": "wrist_neutral",
          "label": "วางปกติ",
          "legacy_files": [
            "nomal.csv",
            "normal.csv"
          ]
        }
      ]
    },
    "elbow": {
      "label": "ข้อศอก",
      "actions": [
        {
          "id": "elbow_raise_arm",
          "label": "ยกแขน",
          "legacy_files": []
        },
        {
          "id": "elbow_release_arm",
          "label": "ปล่อยแขน",
          "legacy_files": []
        }
      ]
    },
    "shoulder": {
      "label": "หัวไหล่",
      "actions": []
    },
    "neck": {
      "label": "?????",
      "actions": [
        {
          "id": "neck_neutral",
          "label": "?????",
          "legacy_files": []
        },
        {
          "id": "neck_flexion",
          "label": "?????",
          "legacy_files": []
        },
        {
          "id": "neck_extension",
          "label": "?????",
          "legacy_files": []
        },
        {
          "id": "neck_lateral_flexion_left",
          "label": "???????????",
          "legacy_files": []
        },
        {
          "id": "neck_lateral_flexion_right",
          "label": "??????????",
          "legacy_files": []
        }
      ]
    },
    "lumbar": {
      "label": "???????????????",
      "actions": [
        {
          "id": "lumbar_neutral",
          "label": "???????",
          "legacy_files": []
        },
        {
          "id": "lumbar_flexion",
          "label": "????????",
          "legacy_files": []
        },
        {
          "id": "lumbar_extension",
          "label": "????????",
          "legacy_files": []
        },
        {
          "id": "lumbar_lateral_flexion_left",
          "label": "??????????????",
          "legacy_files": []
        },
        {
          "id": "lumbar_lateral_flexion_right",
          "label": "?????????????",
          "legacy_files": []
        }
      ]
    },
    "pelvis": {
      "label": "???????? (???????????)",
      "actions": [
        {
          "id": "pelvis_neutral",
          "label": "????????????",
          "legacy_files": []
        },
        {
          "id": "pelvis_anterior_tilt",
          "label": "???????????????????",
          "legacy_files": []
        },
        {
          "id": "pelvis_posterior_tilt",
          "label": "???????????????????",
          "legacy_files": []
        },
        {
          "id": "pelvis_lateral_tilt_left",
          "label": "?????????????????",
          "legacy_files": []
        },
        {
          "id": "pelvis_lateral_tilt_right",
          "label": "????????????????",
          "legacy_files": []
        }
      ]
    }
  },
  "prediction": {
    "minimum_action_score": 0.55,
    "minimum_phase_score": 0.55,
    "smoothing_windows": 3,
    "holding_stable_seconds": 0.5,
    "gap_reset_seconds": 0.15
  }
}
```

### 6.23 ไฟล์ `config/plank_model.json` — ตั้งค่าเกณฑ์มุมสัมพัทธ์และเวลาของ Plank Phase Tracker (42 บรรทัด)
```json
{
  "schema_version": 1,
  "model_version": "planko-6axis-phase-pilot-v1",
  "description": "Experimental rule-based plank phase estimator. This is not a trained classifier.",
  "sampling_hz": 20,
  "calibration": {
    "duration_seconds": 2.0,
    "minimum_samples": 20,
    "maximum_relative_angle_std_deg": 5.0
  },
  "sensor_pair": {
    "upper_sites": ["chest", "upper_back"],
    "lower_sites": ["lumbar", "pelvis"],
    "axes_used": ["pitch", "roll"],
    "yaw_used": false,
    "requires_six_axis_sample": true
  },
  "score_thresholds": {
    "sagittal_deg": 12.0,
    "lateral_deg": 12.0,
    "validated": false
  },
  "phase_thresholds": {
    "normal_max": 0.6,
    "approaching_min": 0.7,
    "incorrect_min": 1.0,
    "trend_deadband_per_second": 0.05,
    "approaching_confirm_seconds": 0.3,
    "incorrect_confirm_seconds": 0.4,
    "recovery_confirm_seconds": 0.3,
    "normal_confirm_seconds": 0.5,
    "trend_window_seconds": 0.5,
    "gap_reset_seconds": 0.15
  },
  "capabilities": {
    "hip_high": "Requires a validated signed pelvis-to-thorax angle threshold and correctly mounted upper/lower sensors.",
    "load_distribution": "Unavailable: current hardware has one pressure channel, not arm and foot force channels.",
    "lateral_balance": "Unavailable as a validated balance classifier: requires bilateral sensors and labeled trials.",
    "excessive_arch": "Requires a validated signed pelvis-to-thorax angle threshold and correctly mounted upper/lower sensors."
  }
}
```

### 6.24 ไฟล์ `listen_esp32.py` — สคริปต์รับข้อมูลจาก ESP32 ผ่าน Serial/UDP (36 บรรทัด)
```python
import ctypes, time, sys, io
from serial import win32

# Set stdout to UTF-8
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')

print("Connecting to COM3...")
h = win32.CreateFile(r'\\.\COM3', win32.GENERIC_READ | win32.GENERIC_WRITE, 0, None, win32.OPEN_EXISTING, 0, 0)
if h == win32.INVALID_HANDLE_VALUE:
    print("Failed to open COM3:", ctypes.WinError())
    sys.exit(1)

print("COM3 Opened successfully! Listening for ESP32...")

timeouts = win32.COMMTIMEOUTS()
timeouts.ReadIntervalTimeout = 50
timeouts.ReadTotalTimeoutConstant = 200
timeouts.ReadTotalTimeoutMultiplier = 0
win32.SetCommTimeouts(h, ctypes.byref(timeouts))

buf = ctypes.create_string_buffer(1024)
read = win32.DWORD()
start_time = time.time()

try:
    while time.time() - start_time < 30: # ฟังต่อเนื่อง 30 วินาที
        if win32.ReadFile(h, buf, 1024, ctypes.byref(read), None) and read.value > 0:
            text = buf.raw[:read.value].decode('utf-8', errors='replace')
            sys.stdout.write(text)
            sys.stdout.flush()
            start_time = time.time() # ยืดเวลาถอยหลังถ้ามีข้อมูลส่งเข้ามา
        time.sleep(0.02)
finally:
    win32.CloseHandle(h)
    print("\nSession finished.")
```

### 6.25 ไฟล์ `tools/start_mosquitto.ps1` — สคริปต์ PowerShell เปิดใช้งาน Docker Mosquitto (57 บรรทัด)
```powershell
$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $projectRoot

$serverVersion = & docker version --format '{{.Server.Version}}' 2>$null
if ($LASTEXITCODE -ne 0 -or -not $serverVersion) {
    throw "Docker Engine is not running. Start Docker Desktop, then run tools\start_mosquitto.ps1 again."
}

& docker compose up -d
if ($LASTEXITCODE -ne 0) {
    throw "Could not start the Mosquitto container. See the Docker Compose error above."
}

Write-Host ""
Write-Host "Mosquitto container status:"
& docker compose ps

$lanAddressList = [System.Collections.Generic.List[string]]::new()
foreach ($adapter in [System.Net.NetworkInformation.NetworkInterface]::GetAllNetworkInterfaces()) {
    if ($adapter.OperationalStatus -ne "Up" -or $adapter.NetworkInterfaceType -eq "Loopback") {
        continue
    }

    $properties = $adapter.GetIPProperties()
    $gateway = $properties.GatewayAddresses |
        Where-Object { $_.Address.AddressFamily -eq [System.Net.Sockets.AddressFamily]::InterNetwork } |
        Select-Object -First 1
    if (-not $gateway) { continue }

    foreach ($address in $properties.UnicastAddresses) {
        if ($address.Address.AddressFamily -ne [System.Net.Sockets.AddressFamily]::InterNetwork) {
            continue
        }
        $candidate = $address.Address.IPAddressToString
        if ($candidate -and $candidate -ne "127.0.0.1" -and $candidate -notlike "169.254.*") {
            $lanAddressList.Add($candidate)
        }
    }
}
$lanAddresses = @($lanAddressList | Select-Object -Unique)

Write-Host ""
Write-Host "Broker listeners:"
Write-Host "  MQTT:       <this-PC-LAN-IP>:1883"
Write-Host "  WebSocket:  ws://<this-PC-LAN-IP>:9001/mqtt"
Write-Host "  Topic:      planko/imu (created automatically when first used)"
if ($lanAddresses.Count -gt 0) {
    Write-Host "LAN IPv4 address candidates on this PC:"
    $lanAddresses | ForEach-Object { Write-Host "  $_" }
}
Write-Host ""
Write-Host "Gateway must use this PC's LAN IP on the same Wi-Fi."
Write-Host "Index on this PC can use localhost; add ?broker=<LAN-IP> if it runs on another device."
Write-Host "Follow logs with: docker compose logs -f mosquitto"
```

### 6.26 ไฟล์ `tools/watch_mosquitto_topic.ps1` — สคริปต์ดูข้อความ MQTT สดจาก Mosquitto (11 บรรทัด)
```powershell
$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $projectRoot

Write-Host "Watching MQTT topic planko/imu on the local Docker broker. Press Ctrl+C to stop."
& docker compose exec mosquitto mosquitto_sub -h 127.0.0.1 -p 1883 -t "planko/imu" -v
if ($LASTEXITCODE -ne 0) {
    throw "Could not subscribe. Start the broker first with tools\start_mosquitto.ps1."
}
```

### 6.27 ไฟล์ `tools/capture.py` — เครื่องมือบันทึกข้อมูล IMU ผ่านพอร์ต Serial/HTTP (243 บรรทัด)
```python
"""Capture one firmware IMU stream from USB serial with explicit metadata."""

from __future__ import annotations

import argparse
import csv
import json
import re
import time
from datetime import datetime, timezone
from pathlib import Path

from view_imu import estimate_angles, read_capture


ROOT = Path(__file__).resolve().parents[1]
CONFIG = json.loads((ROOT / "config" / "pose_labels.json").read_text(encoding="utf-8"))
MOUNT_SITES = tuple(CONFIG["mount_sites"])
BODY_PARTS = tuple(CONFIG["body_parts"])
FIELDS = [
    "sensor_uid", "sensor_id", "mount_site", "body_part", "side", "subject_id",
    "session_id", "trial_id", "action_label", "phase_label", "phase_elapsed_ms",
    "calibration_id", "calibration_method_version", "calibration_time_boot_ms", "calibration_boot_id",
    "calibrated", "relative_angle_valid", "relative_orientation_valid", "unit_accel", "unit_gyro",
    "host_time_iso", "host_time_s", "boot_ms", "seq", "stream_epoch", "gap_before",
    "accel_x", "accel_y", "accel_z", "gyro_x", "gyro_y", "gyro_z",
    "corrected_gyro_x", "corrected_gyro_y", "corrected_gyro_z", "roll_deg", "pitch_deg",
    "relative_roll_deg", "relative_pitch_deg", "relative_tilt_deg", "valid", "firmware_status",
]


def parse_line(line: str):
    values = line.split(",")
    if len(values) in (21, 23, 24) and re.fullmatch(r"[0-9a-fA-F]{12}", values[0]):
        try:
            boot, seq = int(values[1]), int(values[2])
            ax, ay, az, gx, gy, gz = map(float, values[3:9])
            calibrated = values[10].strip().lower() == "true"
            roll, pitch = map(float, values[12:14])
            cgx, cgy, cgz = map(float, values[14:17])
            rel_roll, rel_pitch = map(float, values[17:19])
            relative_valid = values[19].strip().lower() == "true"
            orientation_valid = values[20].strip().lower() == "true"
            calibration_method_version = int(values[21]) if len(values) == 23 and values[21] else (1 if calibrated else "")
            calibration_time_boot_ms = int(values[22]) if len(values) == 23 and values[22] else ""
            if len(values) == 24:
                calibration_method_version = int(values[21]) if values[21] else (1 if calibrated else "")
                calibration_time_boot_ms = int(values[22]) if values[22] else ""
                calibration_boot_id = int(values[23]) if values[23] else ""
            else:
                calibration_boot_id = ""
        except ValueError:
            return None
        return {"sensor_uid": values[0].lower(), "mount_site": None, "boot_ms": boot, "seq": seq,
                "accel": (ax * 9.80665, ay * 9.80665, az * 9.80665), "gyro": (gx, gy, gz),
                "corrected_gyro": (cgx, cgy, cgz), "calibration_id": values[9],
                "calibrated": calibrated, "status": values[11], "roll": roll, "pitch": pitch,
                "relative_roll": rel_roll, "relative_pitch": rel_pitch,
                "calibration_method_version": calibration_method_version,
                "calibration_time_boot_ms": calibration_time_boot_ms,
                "calibration_boot_id": calibration_boot_id,
                "relative_valid": relative_valid, "orientation_valid": orientation_valid, "format": "current"}
    if len(values) not in (9, 12):
        return None
    try:
        boot, seq = int(values[1]), int(values[2])
        accel = tuple(map(float, values[3:6]))
        gyro = tuple(map(float, values[6:9]))
        if len(values) == 12:
            [float(value) for value in values[9:] if value]
    except ValueError:
        return None
    # Legacy firmwares sent m/s^2, while current firmware emits a 12-hex MAC UID.
    return {"sensor_uid": values[0], "mount_site": values[0], "boot_ms": boot, "seq": seq,
            "accel": accel, "gyro": gyro, "corrected_gyro": ("", "", ""),
            "calibration_id": "", "calibrated": False, "status": "unknown",
            "calibration_method_version": "", "calibration_time_boot_ms": "",
            "calibration_boot_id": "",
            "roll": "", "pitch": "", "relative_roll": "", "relative_pitch": "",
            "relative_valid": False, "orientation_valid": False, "format": "legacy"}


def make_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="COM port, such as COM5")
    parser.add_argument("--output", type=Path, default=Path("imu_output"))
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--seconds", type=float, default=0, help="0 records until Ctrl+C")
    parser.add_argument("--mount-site", choices=MOUNT_SITES, default="wrist")
    parser.add_argument("--body-part", choices=BODY_PARTS, default="wrist")
    parser.add_argument("--side", choices=("left", "right", "unknown"), default="unknown")
    parser.add_argument("--subject-id", default="P01")
    parser.add_argument("--session-id", default="")
    parser.add_argument("--trial-id", default="")
    parser.add_argument("--action-label", default="", help="Action ID from config/pose_labels.json")
    parser.add_argument("--phase-label", choices=("", "neutral", "approaching", "holding", "returning"), default="")
    return parser


def main():
    args = make_parser().parse_args()
    if args.seconds < 0:
        raise SystemExit("--seconds must be non-negative")
    if args.action_label and args.action_label not in {
        action["id"] for body in CONFIG["body_parts"].values() for action in body.get("actions", [])
    }:
        raise SystemExit("Unknown --action-label; see config/pose_labels.json")
    if args.action_label and args.action_label not in {
        action["id"] for action in CONFIG["body_parts"][args.body_part].get("actions", [])
    }:
        raise SystemExit(f"Action {args.action_label!r} does not belong to body part {args.body_part!r}")
    side = args.side
    if side == "unknown" and args.mount_site.startswith("left_"):
        side = "left"
    elif side == "unknown" and args.mount_site.startswith("right_"):
        side = "right"
    try:
        import serial
    except ImportError as exc:
        raise SystemExit("Install serial dependencies with: python -m pip install pyserial") from exc

    args.output.mkdir(parents=True, exist_ok=True)
    started = datetime.now(timezone.utc)
    session_id = args.session_id or started.strftime("%Y%m%dT%H%M%SZ")
    trial_id = args.trial_id or session_id + "_001"
    session_stamp = started.strftime("%Y%m%dT%H%M%SZ")
    file_part = 1
    stream_epoch = 0
    count = 0
    duplicates = 0
    first_seq = last_seq = first_boot = last_boot = None
    previous_key = None
    previous_calibration = None
    global_previous_seq = global_previous_boot = None
    handle = writer = None
    paths: list[Path] = []

    def open_segment(segment: int):
        nonlocal handle, writer
        path = args.output / f"{session_stamp}_{args.mount_site}_{args.action_label or 'unlabeled'}_part{segment:02d}.csv"
        handle = path.open("w", newline="", encoding="utf-8")
        writer = csv.DictWriter(handle, fieldnames=FIELDS)
        writer.writeheader()
        paths.append(path)
        print(f"Writing {path}")

    try:
        with serial.Serial(args.port, args.baud, timeout=1) as source:
            print(f"Reading {args.port}; stop with Ctrl+C")
            open_segment(file_part)
            deadline = time.monotonic() + args.seconds if args.seconds else None
            while deadline is None or time.monotonic() < deadline:
                raw = source.readline()
                received = datetime.now(timezone.utc)
                if not raw:
                    continue
                line = raw.decode("ascii", errors="replace").strip()
                if not line or line.startswith("#") or line.startswith("sensor_uid,") or line.startswith("sensor_id,"):
                    continue
                sample = parse_line(line)
                if sample is None:
                    continue
                if sample["mount_site"] and sample["mount_site"] != args.mount_site:
                    print(f"Ignoring {sample['mount_site']}: configured mount site is {args.mount_site}")
                    continue
                key = (sample["boot_ms"], sample["seq"])
                if key == previous_key:
                    duplicates += 1
                    continue
                reboot = global_previous_boot is not None and sample["boot_ms"] < global_previous_boot
                if not reboot and global_previous_seq is not None and sample["seq"] <= global_previous_seq:
                    duplicates += 1
                    continue
                previous_key = key
                global_previous_seq = sample["seq"]
                global_previous_boot = sample["boot_ms"]
                calibration_id = sample["calibration_id"]
                if count and calibration_id != previous_calibration:
                    handle.close()
                    file_part += 1
                    stream_epoch += 1
                    trial_id = f"{args.trial_id or session_id + '_001'}_cal{file_part}"
                    open_segment(file_part)
                    first_seq = last_seq = first_boot = last_boot = None
                gap = last_seq is not None and sample["seq"] != last_seq + 1
                if reboot:
                    stream_epoch += 1
                row = {
                    "sensor_uid": sample["sensor_uid"], "sensor_id": sample["sensor_uid"],
                    "mount_site": args.mount_site, "body_part": args.body_part, "side": side,
                    "subject_id": args.subject_id, "session_id": session_id, "trial_id": trial_id,
                    "action_label": args.action_label, "phase_label": args.phase_label,
                    "phase_elapsed_ms": "", "calibration_id": calibration_id,
                    "calibration_method_version": sample["calibration_method_version"],
                    "calibration_time_boot_ms": sample["calibration_time_boot_ms"],
                    "calibration_boot_id": sample["calibration_boot_id"],
                    "calibrated": sample["calibrated"], "relative_angle_valid": sample["relative_valid"],
                    "relative_orientation_valid": sample["orientation_valid"],
                    "unit_accel": "m/s^2", "unit_gyro": "rad/s",
                    "host_time_iso": received.isoformat(), "host_time_s": received.timestamp(),
                    "boot_ms": sample["boot_ms"], "seq": sample["seq"], "stream_epoch": stream_epoch,
                    "gap_before": bool(gap), "valid": True, "firmware_status": sample["status"],
                    "roll_deg": sample["roll"], "pitch_deg": sample["pitch"],
                    "relative_roll_deg": sample["relative_roll"], "relative_pitch_deg": sample["relative_pitch"],
                    "relative_tilt_deg": sample["relative_pitch"],
                    "corrected_gyro_x": sample["corrected_gyro"][0],
                    "corrected_gyro_y": sample["corrected_gyro"][1],
                    "corrected_gyro_z": sample["corrected_gyro"][2],
                }
                for axis, value in zip("xyz", sample["accel"]):
                    row[f"accel_{axis}"] = value
                for axis, value in zip("xyz", sample["gyro"]):
                    row[f"gyro_{axis}"] = value
                writer.writerow(row)
                count += 1
                if count % 100 == 0:
                    handle.flush()
                if first_seq is None:
                    first_seq, first_boot = sample["seq"], sample["boot_ms"]
                last_seq, last_boot = sample["seq"], sample["boot_ms"]
                previous_calibration = calibration_id
    except KeyboardInterrupt:
        pass
    finally:
        if handle and not handle.closed:
            handle.close()
        if paths:
            print(f"Saved {count} samples in {len(paths)} calibration segment(s); duplicate samples ignored: {duplicates}")
            if first_seq is not None and last_boot and last_boot > first_boot:
                print(f"Estimated rate: {(last_seq-first_seq)*1000/(last_boot-first_boot):.2f} Hz")
            if len(paths) == 1 and count:
                try:
                    angles = estimate_angles(read_capture(paths[0]))
                    out = paths[0].with_name(paths[0].stem + "_orientation.json")
                    out.write_text(json.dumps(angles, indent=2) + "\n", encoding="utf-8")
                    print(f"Saved orientation rows to {out}")
                except Exception as error:
                    print(f"Orientation sidecar skipped: {error}")


if __name__ == "__main__":
    main()
```

### 6.28 ไฟล์ `tools/merge.py` — เครื่องมือรวมไฟล์ CSV (105 บรรทัด)
```python
"""Join actual IMU samples by receive time without resampling or cloning rows."""

from __future__ import annotations

import argparse
import csv
import glob
from bisect import bisect_left
from pathlib import Path

from capture import MOUNT_SITES


SIGNALS = tuple(f"{signal}_{axis}" for signal in ("accel", "gyro", "corrected_gyro")
                for axis in "xyz")
METADATA = ("sensor_uid", "body_part", "side", "subject_id", "session_id", "trial_id",
            "action_label", "phase_label", "phase_elapsed_ms", "calibration_id",
            "calibration_method_version", "calibration_time_boot_ms", "calibration_boot_id", "calibrated",
            "relative_angle_valid", "relative_orientation_valid", "unit_accel", "unit_gyro",
            "boot_ms", "seq", "stream_epoch", "gap_before", "valid", "firmware_status",
            "roll_deg", "pitch_deg", "relative_roll_deg", "relative_pitch_deg", "relative_tilt_deg")


def load_recording(path: Path):
    with path.open(newline="", encoding="utf-8-sig") as stream:
        rows = list(csv.DictReader(stream))
    if not rows:
        raise ValueError(f"Empty recording: {path}")
    sites = {row.get("mount_site") or row.get("recorded_site") or row.get("sensor_id") for row in rows}
    sites.discard(None)
    if len(sites) != 1 or next(iter(sites)) not in MOUNT_SITES:
        raise ValueError(f"Expected exactly one configured mount_site in {path}: {sites}")
    for row in rows:
        if not row.get("host_time_s"):
            raise ValueError(f"Missing host_time_s in {path}")
        row["host_time_s"] = float(row["host_time_s"])
    rows.sort(key=lambda row: row["host_time_s"])
    return next(iter(sites)), rows


def nearest_unused(rows, timestamps, used, target, tolerance):
    index = bisect_left(timestamps, target)
    candidates = [i for i in range(max(0, index - 1), min(len(rows), index + 2)) if i not in used]
    if not candidates:
        return None
    best = min(candidates, key=lambda i: abs(timestamps[i] - target))
    if abs(timestamps[best] - target) > tolerance:
        return None
    used.add(best)
    return rows[best]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="+", help="One recording per sensor; wildcards accepted")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--reference-site", default="", help="Site whose actual timestamps define output rows")
    parser.add_argument("--tolerance-ms", type=float, default=35)
    args = parser.parse_args()
    if args.tolerance_ms < 0:
        parser.error("--tolerance-ms must be nonnegative")
    paths = [Path(match) for pattern in args.inputs for match in glob.glob(pattern)]
    if not paths:
        parser.error("No input CSV files matched")
    recordings = {}
    for path in paths:
        site, rows = load_recording(path)
        if site in recordings:
            parser.error(f"Multiple files for mount_site={site}; select one per synchronized trial")
        recordings[site] = rows
    reference = args.reference_site or max(recordings, key=lambda site: len(recordings[site]))
    if reference not in recordings:
        parser.error(f"Reference site {reference!r} is not among the inputs")

    fields = ["host_time_s", "reference_site"]
    for site in sorted(recordings):
        fields.extend(f"{name}_{site}" for name in (*METADATA, *SIGNALS))
    timestamps = {site: [row["host_time_s"] for row in rows] for site, rows in recordings.items()}
    used = {site: set() for site in recordings}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    count = 0
    with args.output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        for reference_index, ref_row in enumerate(recordings[reference]):
            target = ref_row["host_time_s"]
            output = {"host_time_s": f"{target:.6f}", "reference_site": reference}
            for site, rows in recordings.items():
                match = ref_row if site == reference else nearest_unused(
                    rows, timestamps[site], used[site], target, args.tolerance_ms / 1000.0)
                if site == reference:
                    used[site].add(reference_index)
                if match is None:
                    continue
                for field in (*METADATA, *SIGNALS):
                    output[f"{field}_{site}"] = match.get(field, "")
            writer.writerow(output)
            count += 1
    print(f"Wrote {count} rows at actual {reference} samples; unmatched sensor fields remain blank.")
    print("No interpolation or repeated samples were generated.")


if __name__ == "__main__":
    main()
```

### 6.29 ไฟล์ `tools/simulate_imu.py` — สคริปต์จำลองการส่งข้อมูล IMU เข้า MQTT (105 บรรทัด)
```python
"""Generate a deterministic Nano 33 BLE Sense-like IMU CSV without hardware."""

import argparse
import csv
import json
import math
import time
from pathlib import Path

from capture import FIELDS, POINTS
from view_imu import estimate_angles, read_capture, rotation_matrix


GRAVITY = 9.80665
MAG_FIELD_WORLD_UT = (20.0, 0.0, 42.0)


def motion(time_s):
    """Return sensor-frame Euler angles and rates for a smooth multi-axis turn."""
    roll_amp = math.radians(38.0)
    pitch_amp = math.radians(26.0)
    yaw_sway_amp = math.radians(24.0)
    roll_hz = 0.20
    pitch_hz = 0.14
    yaw_sway_hz = 0.11
    yaw_rate = math.radians(30.0)

    roll = roll_amp * math.sin(2.0 * math.pi * roll_hz * time_s)
    pitch = pitch_amp * math.sin(2.0 * math.pi * pitch_hz * time_s)
    yaw = yaw_rate * time_s + yaw_sway_amp * math.sin(2.0 * math.pi * yaw_sway_hz * time_s)

    roll_dot = roll_amp * 2.0 * math.pi * roll_hz * math.cos(2.0 * math.pi * roll_hz * time_s)
    pitch_dot = pitch_amp * 2.0 * math.pi * pitch_hz * math.cos(2.0 * math.pi * pitch_hz * time_s)
    yaw_dot = yaw_rate + yaw_sway_amp * 2.0 * math.pi * yaw_sway_hz \
        * math.cos(2.0 * math.pi * yaw_sway_hz * time_s)

    # Euler-angle derivatives converted to angular velocity in the sensor frame.
    gyro = (
        roll_dot - yaw_dot * math.sin(pitch),
        pitch_dot * math.cos(roll) + yaw_dot * math.sin(roll) * math.cos(pitch),
        -pitch_dot * math.sin(roll) + yaw_dot * math.cos(roll) * math.cos(pitch),
    )
    return roll, pitch, yaw, gyro


def sensor_vector(rotation, world_vector):
    """Transform a world-frame vector into the rotating sensor frame."""
    return [
        sum(rotation[row][column] * world_vector[row] for row in range(3))
        for column in range(3)
    ]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("imu_output/simulated_chest.csv"))
    parser.add_argument("--sensor-id", choices=sorted(POINTS), default="chest")
    parser.add_argument("--seconds", type=float, default=8.0)
    parser.add_argument("--hz", type=float, default=100.0)
    parser.add_argument("--mag-hz", type=float, default=10.0)
    args = parser.parse_args()
    if args.seconds <= 0 or args.hz <= 0 or args.mag_hz <= 0:
        parser.error("--seconds, --hz, and --mag-hz must all be positive")

    sample_count = int(round(args.seconds * args.hz)) + 1
    mag_interval = max(1, int(round(args.hz / args.mag_hz)))
    host_start = time.time()
    args.output.parent.mkdir(parents=True, exist_ok=True)

    with args.output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=FIELDS)
        writer.writeheader()
        for sequence in range(sample_count):
            time_s = sequence / args.hz
            roll, pitch, yaw, gyro = motion(time_s)
            rotation = rotation_matrix(roll, pitch, yaw)
            acceleration = sensor_vector(rotation, (0.0, 0.0, GRAVITY))
            magnetic = sensor_vector(rotation, MAG_FIELD_WORLD_UT)
            mag_values = magnetic if sequence % mag_interval == 0 else (None, None, None)

            row = {
                "sensor_id": args.sensor_id,
                "host_time_s": f"{host_start + time_s:.6f}",
                "boot_ms": round(time_s * 1000),
                "seq": sequence,
            }
            for axis, value in zip("xyz", acceleration):
                row[f"accel_{axis}"] = f"{value:.6f}"
            for axis, value in zip("xyz", gyro):
                row[f"gyro_{axis}"] = f"{value:.6f}"
            for axis, value in zip("xyz", mag_values):
                row[f"mag_{axis}"] = "" if value is None else f"{value:.6f}"
            writer.writerow(row)

    angles = estimate_angles(read_capture(args.output))
    orientation_path = args.output.with_name(args.output.stem + "_orientation.json")
    numeric_rows = [[round(value, 6) for value in row] for row in angles]
    orientation_path.write_text(json.dumps(numeric_rows, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {sample_count} simulated IMU rows to {args.output}")
    print(f"Wrote numeric [roll, pitch, yaw] radians to {orientation_path}")


if __name__ == "__main__":
    main()
```

### 6.30 ไฟล์ `tools/simulate_neck.py` — สคริปต์จำลองข้อมูลเซ็นเซอร์ลำคอ/หลัง (220 บรรทัด)
```python
"""Generate a synthetic one-IMU neck-plank recording for pipeline prototyping.

The CSV is illustrative only. It is not participant data and does not reproduce
the Xsens measurements or classifier reported by Chen et al.
"""

import argparse
import csv
import math
import random
from pathlib import Path


G = 9.80665
RATE_HZ = 20  # Match the current ESP32 firmware's 50 ms sample interval.
CALIBRATION_S = 2.0
ANGLE_LIMIT_DEG = 12.0  # Same provisional threshold as src/main.cpp.
STATUS_HOLD_S = 0.5
FLEXION_SIGN = -1.0

# Time bounds are seconds. Head flexion maps to negative sensor tilt for the
# mounting convention documented in src/main.cpp; extension maps to positive.
PHASES = [
    (0.0, 2.0, "calibration", "CAL", 0.0, 0.0),
    (2.0, 12.0, "normal_01", "PC", 0.0, 0.0),
    (12.0, 12.6, "transition_01", "transition", 0.0, -18.0),
    (12.6, 22.6, "htfn_01", "HTFN", -18.0, -18.0),
    (22.6, 23.2, "transition_02", "transition", -18.0, 18.0),
    (23.2, 33.2, "htbn_01", "HTBN", 18.0, 18.0),
    (33.2, 33.8, "transition_03", "transition", 18.0, 0.0),
    (33.8, 43.8, "normal_02", "PC", 0.0, 0.0),
]


def smoothstep(value):
    value = max(0.0, min(1.0, value))
    return value * value * (3.0 - 2.0 * value)


def phase_at(time_s):
    for start, end, trial_id, label, start_deg, end_deg in PHASES:
        if start <= time_s < end:
            progress = (time_s - start) / (end - start)
            angle = start_deg + (end_deg - start_deg) * smoothstep(progress)
            return start, end, trial_id, label, angle
    return PHASES[-1][0], PHASES[-1][1], *PHASES[-1][2:5]


def wrap_deg(angle):
    return math.degrees(math.atan2(math.sin(math.radians(angle)),
                                   math.cos(math.radians(angle))))


def collect_samples(seed):
    rng = random.Random(seed)
    count = round(PHASES[-1][1] * RATE_HZ)
    samples = []
    previous_angle = 0.0

    for seq in range(count):
        time_s = seq / RATE_HZ
        start, end, trial_id, label, target_deg = phase_at(time_s)
        # Small simulated postural sway plus sensor noise.
        sway = 0.7 * math.sin(2.0 * math.pi * 0.22 * time_s)
        true_angle = target_deg + sway
        sensor_angle = true_angle + rng.gauss(0.0, 0.25)
        dt = 1.0 / RATE_HZ
        # Gyro measures physical rotation; don't differentiate accelerometer noise.
        gyro_x_dps = (true_angle - previous_angle) / dt + rng.gauss(0.0, 0.45)
        previous_angle = true_angle

        angle_rad = math.radians(sensor_angle)
        ax_g = rng.gauss(0.0, 0.004)
        ay_g = math.sin(angle_rad) + rng.gauss(0.0, 0.004)
        az_g = math.cos(angle_rad) + rng.gauss(0.0, 0.004)
        gx = math.radians(gyro_x_dps)
        gy = math.radians(rng.gauss(0.0, 0.25))
        gz = math.radians(rng.gauss(0.0, 0.25))
        accel_norm_g = math.sqrt(ax_g * ax_g + ay_g * ay_g + az_g * az_g)
        gyro_norm_dps = math.degrees(math.sqrt(gx * gx + gy * gy + gz * gz))
        tilt_deg = math.degrees(math.atan2(ay_g, az_g))
        trial_elapsed = time_s - start

        samples.append({
            "sensor_id": "neck",
            "trial_id": trial_id,
            "host_time_s": f"{time_s:.3f}",
            "boot_ms": round(time_s * 1000),
            "seq": seq + 1,
            "label": label,
            "accel_x": ax_g * G,
            "accel_y": ay_g * G,
            "accel_z": az_g * G,
            "gyro_x": gx,
            "gyro_y": gy,
            "gyro_z": gz,
            "tilt_deg": tilt_deg,
            "accel_norm_g": accel_norm_g,
            "gyro_norm_dps": gyro_norm_dps,
            "trial_elapsed_s": trial_elapsed,
            "steady": accel_norm_g >= 0.90 and accel_norm_g <= 1.10 and gyro_norm_dps <= 10.0,
        })

    calibration_count = round(CALIBRATION_S * RATE_HZ)
    sin_sum = sum(math.sin(math.radians(row["tilt_deg"]))
                  for row in samples[:calibration_count])
    cos_sum = sum(math.cos(math.radians(row["tilt_deg"]))
                  for row in samples[:calibration_count])
    neutral_deg = math.degrees(math.atan2(sin_sum, cos_sum))

    candidate_status = "normal"
    candidate_count = 0
    status = "calibrating"
    for row in samples:
        row["neutral_tilt_deg"] = neutral_deg
        row["neck_flex_deg"] = FLEXION_SIGN * wrap_deg(row["tilt_deg"] - neutral_deg)
        time_s = row["boot_ms"] / 1000.0
        row["calibration_samples"] = min(row["seq"], calibration_count)
        row["calibrated"] = time_s >= CALIBRATION_S

        if not row["calibrated"]:
            status = "calibrating"
            candidate_count = 0
        elif not row["steady"]:
            status = "moving"
            candidate_count = 0
        else:
            flex = row["neck_flex_deg"]
            candidate = "neck_flexion" if flex >= ANGLE_LIMIT_DEG else (
                "neck_extension" if flex <= -ANGLE_LIMIT_DEG else "normal")
            if candidate == candidate_status:
                candidate_count = min(candidate_count + 1, round(STATUS_HOLD_S * RATE_HZ))
            else:
                candidate_status = candidate
                candidate_count = 1
            if candidate_count >= round(STATUS_HOLD_S * RATE_HZ):
                status = candidate

        row["status"] = status
        row["valid"] = True
        row["study_window"] = (
            row["label"] in {"PC", "HTFN", "HTBN"}
            and 4.0 <= row["trial_elapsed_s"] < 7.0
        )

    return samples


def save_csv(samples, path):
    fields = [
        "sensor_id", "trial_id", "host_time_s", "boot_ms", "seq", "label",
        "accel_x", "accel_y", "accel_z", "gyro_x", "gyro_y", "gyro_z",
        "tilt_deg", "neutral_tilt_deg", "neck_flex_deg", "accel_norm_g",
        "gyro_norm_dps", "trial_elapsed_s", "steady", "calibrated",
        "calibration_samples", "valid", "status", "study_window",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(output, fieldnames=fields)
        writer.writeheader()
        for row in samples:
            writer.writerow({key: row[key] for key in fields})


def save_plot(samples, path):
    import matplotlib.pyplot as plt

    times = [float(row["host_time_s"]) for row in samples]
    angles = [row["neck_flex_deg"] for row in samples]
    ay = [row["accel_y"] / G for row in samples]
    az = [row["accel_z"] / G for row in samples]
    path.parent.mkdir(parents=True, exist_ok=True)

    fig, axes = plt.subplots(2, 1, figsize=(11, 6.5), sharex=True,
                             constrained_layout=True)
    axes[0].plot(times, angles, color="#245f86", linewidth=1.4,
                 label="Simulated neck_flex_deg")
    axes[0].axhline(ANGLE_LIMIT_DEG, color="#b44e3c", linestyle="--",
                    label="Prototype threshold (+/-12 deg)")
    axes[0].axhline(-ANGLE_LIMIT_DEG, color="#b44e3c", linestyle="--")
    axes[0].set_ylabel("Angle relative to neutral (deg)")
    axes[0].set_title("Synthetic neck IMU: normal and two instructed deviations")
    axes[0].legend(loc="upper right", frameon=False)
    axes[0].grid(alpha=0.2)

    axes[1].plot(times, ay, linewidth=1.1, label="accel_y (g)")
    axes[1].plot(times, az, linewidth=1.1, label="accel_z (g)")
    axes[1].set_xlabel("Time since boot (s)")
    axes[1].set_ylabel("Acceleration (g)")
    axes[1].legend(loc="upper right", frameon=False)
    axes[1].grid(alpha=0.2)
    for start, end, _, label, _, _ in PHASES:
        if label in {"PC", "HTFN", "HTBN"}:
            for axis in axes:
                axis.axvspan(start, end, color="#8aa98c", alpha=0.10, linewidth=0)
    fig.savefig(path, dpi=150)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--csv", type=Path,
                        default=Path("imu_output/simulated_neck.csv"))
    parser.add_argument("--plot", type=Path,
                        default=Path("imu_output/simulated_neck.png"))
    parser.add_argument("--seed", type=int, default=2026)
    args = parser.parse_args()

    samples = collect_samples(args.seed)
    save_csv(samples, args.csv)
    save_plot(samples, args.plot)
    print(f"Wrote {len(samples)} synthetic samples at {RATE_HZ} Hz to {args.csv}")
    print(f"Wrote plot to {args.plot}")
    print("Labels: PC=normal, HTFN=forward neck bend, HTBN=backward neck bend")
    print("All values are synthetic; they are not measured or copied from the paper.")


if __name__ == "__main__":
    main()
```

### 6.31 ไฟล์ `tools/view_imu.py` — โปรแกรมแสดงกราฟข้อมูล IMU แบบเรียลไทม์ (260 บรรทัด)
```python
"""Animate the orientation measured by one IMU capture CSV."""

import argparse
import bisect
import csv
import json
import math
from pathlib import Path


G = 9.80665
AXES = "xyz"


def read_capture(path):
    with path.open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream))
    if not rows:
        raise ValueError(f"No samples in {path}")

    samples = []
    previous_device_ms = None
    elapsed = 0.0
    first_host_time = None
    for row in rows:
        try:
            acceleration = [float(row[f"accel_{axis}"]) for axis in AXES]
            gyro = [float(row[f"gyro_{axis}"]) for axis in AXES]
        except (KeyError, TypeError, ValueError):
            continue

        if row.get("boot_ms"):
            device_ms = int(row["boot_ms"])
            if previous_device_ms is not None:
                elapsed += ((device_ms - previous_device_ms) & 0xFFFFFFFF) / 1000.0
            previous_device_ms = device_ms
            timestamp = elapsed
        else:
            host_time = float(row["host_time_s"])
            if first_host_time is None:
                first_host_time = host_time
            timestamp = host_time - first_host_time

        try:
            mag = [float(row[f"mag_{axis}"]) for axis in AXES]
        except (KeyError, TypeError, ValueError):
            mag = None

        samples.append({
            "time": timestamp,
            "accel": acceleration,
            "gyro": gyro,
            "mag": mag,
        })

    if not samples:
        raise ValueError("CSV needs accel_x/y/z and gyro_x/y/z columns")
    return samples


def wrap(angle):
    return (angle + math.pi) % (2.0 * math.pi) - math.pi


def accel_angles(accel):
    ax, ay, az = accel
    return math.atan2(ay, az), math.atan2(-ax, math.sqrt(ay * ay + az * az))


def magnetic_yaw(mag, roll, pitch):
    mx, my, mz = mag
    mx_horizontal = mx * math.cos(pitch) + mz * math.sin(pitch)
    my_horizontal = (mx * math.sin(roll) * math.sin(pitch)
                     + my * math.cos(roll)
                     - mz * math.sin(roll) * math.cos(pitch))
    return math.atan2(-my_horizontal, mx_horizontal)


def estimate_angles(samples):
    roll, pitch = accel_angles(samples[0]["accel"])
    latest_mag = samples[0]["mag"]
    yaw = magnetic_yaw(latest_mag, roll, pitch) if latest_mag else 0.0
    angles = [[roll, pitch, yaw]]

    for previous, sample in zip(samples, samples[1:]):
        dt = max(0.0, sample["time"] - previous["time"])
        gx, gy, gz = sample["gyro"]  # Captured in rad/s.
        cos_pitch = math.cos(pitch)
        safe_cos_pitch = math.copysign(max(abs(cos_pitch), 0.05), cos_pitch or 1.0)
        tangent_pitch = math.sin(pitch) / safe_cos_pitch
        roll_rate = gx + math.sin(roll) * tangent_pitch * gy \
            + math.cos(roll) * tangent_pitch * gz
        pitch_rate = math.cos(roll) * gy - math.sin(roll) * gz
        yaw_rate = (math.sin(roll) * gy + math.cos(roll) * gz) / safe_cos_pitch
        roll = wrap(roll + roll_rate * dt)
        pitch = wrap(pitch + pitch_rate * dt)
        yaw = wrap(yaw + yaw_rate * dt)

        # Gravity corrects tilt only when acceleration is close to 1 g.
        accel = sample["accel"]
        accel_norm = math.sqrt(sum(value * value for value in accel))
        if 0.75 * G <= accel_norm <= 1.25 * G:
            measured_roll, measured_pitch = accel_angles(accel)
            correction = 1.0 - math.exp(-dt / 0.45)
            roll = wrap(roll + correction * wrap(measured_roll - roll))
            pitch = wrap(pitch + correction * wrap(measured_pitch - pitch))

        if sample["mag"] is not None:
            latest_mag = sample["mag"]
        if latest_mag and math.sqrt(sum(value * value for value in latest_mag)) > 1e-6:
            measured_yaw = magnetic_yaw(latest_mag, roll, pitch)
            correction = 1.0 - math.exp(-dt / 1.5)
            yaw = wrap(yaw + correction * wrap(measured_yaw - yaw))

        angles.append([roll, pitch, yaw])
    return angles


def rotation_matrix(roll, pitch, yaw):
    cr, sr = math.cos(roll), math.sin(roll)
    cp, sp = math.cos(pitch), math.sin(pitch)
    cy, sy = math.cos(yaw), math.sin(yaw)
    return [
        [cy * cp, cy * sp * sr - sy * cr, cy * sp * cr + sy * sr],
        [sy * cp, sy * sp * sr + cy * cr, sy * sp * cr - cy * sr],
        [-sp, cp * sr, cp * cr],
    ]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path, help="Per-board CSV from tools/capture.py")
    parser.add_argument("--speed", type=float, default=1.0,
                        help="Playback speed multiplier (default: 1)")
    parser.add_argument("--json-output", type=Path,
                        help="Write numeric [roll, pitch, yaw] rows in radians")
    parser.add_argument("--export-only", action="store_true",
                        help="Write JSON without opening the animation window")
    parser.add_argument("--autoplay", action="store_true",
                        help="Start playback as soon as the animation window opens")
    args = parser.parse_args()
    if args.speed <= 0:
        parser.error("--speed must be positive")
    if args.export_only and not args.json_output:
        parser.error("--export-only requires --json-output")

    try:
        samples = read_capture(args.csv)
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    angles = estimate_angles(samples)
    times = [sample["time"] for sample in samples]
    has_mag = any(sample["mag"] is not None for sample in samples)

    if args.json_output:
        args.json_output.parent.mkdir(parents=True, exist_ok=True)
        numeric_rows = [[round(value, 6) for value in row] for row in angles]
        args.json_output.write_text(json.dumps(numeric_rows, indent=2) + "\n",
                                    encoding="utf-8")
        print(f"Wrote {len(numeric_rows)} numeric [roll, pitch, yaw] rows (radians) to {args.json_output}")
    if args.export_only:
        return

    try:
        import matplotlib.pyplot as plt
        import numpy as np
        from matplotlib.widgets import Button, Slider
        from mpl_toolkits.mplot3d.art3d import Poly3DCollection
    except ImportError as exc:
        raise SystemExit("Install plotting dependencies: python -m pip install matplotlib numpy") from exc

    half = (0.48, 0.30, 0.10)
    vertices = np.array([
        [-half[0], -half[1], -half[2]], [half[0], -half[1], -half[2]],
        [half[0], half[1], -half[2]], [-half[0], half[1], -half[2]],
        [-half[0], -half[1], half[2]], [half[0], -half[1], half[2]],
        [half[0], half[1], half[2]], [-half[0], half[1], half[2]],
    ])
    faces = [[0, 1, 2, 3], [4, 7, 6, 5], [0, 4, 5, 1],
             [1, 5, 6, 2], [2, 6, 7, 3], [3, 7, 4, 0]]

    fig = plt.figure(figsize=(9, 7))
    ax = fig.add_subplot(111, projection="3d")
    slider_ax = fig.add_axes([0.17, 0.11, 0.68, 0.035])
    play_ax = fig.add_axes([0.04, 0.095, 0.10, 0.065])
    slider = Slider(slider_ax, "Time (s)", 0.0, max(times[-1], 0.01), valinit=0.0)
    button = Button(play_ax, "Pause" if args.autoplay else "Play")
    playing = args.autoplay
    previous_tick = [None]

    def draw(time_value):
        play_time = max(0.0, min(times[-1], float(time_value)))
        right = bisect.bisect_right(times, play_time)
        left = max(0, right - 1)
        right = min(len(samples) - 1, right)
        span = times[right] - times[left]
        fraction = (play_time - times[left]) / span if span > 0 else 0.0
        roll, pitch, yaw = [
            angles[left][axis] + fraction * wrap(angles[right][axis] - angles[left][axis])
            for axis in range(3)
        ]
        roll, pitch, yaw = wrap(roll), wrap(pitch), wrap(yaw)
        matrix = np.array(rotation_matrix(roll, pitch, yaw))
        rotated = vertices @ matrix.T
        ax.clear()
        ax.add_collection3d(Poly3DCollection(
            [[rotated[i] for i in face] for face in faces],
            facecolors="#5ca8e8", edgecolors="#18334b", alpha=0.85,
        ))
        for axis, color, label in zip(matrix.T, ("r", "g", "b"), ("X", "Y", "Z")):
            ax.quiver(0, 0, 0, *(axis * 0.85), color=color, linewidth=2)
            ax.text(*(axis * 0.98), label, color=color, fontsize=11)
        ax.set(xlim=(-1.1, 1.1), ylim=(-1.1, 1.1), zlim=(-1.1, 1.1),
               xlabel="World X", ylabel="World Y", zlabel="World Z",
               title=(f"IMU orientation — {play_time:.2f} s\n"
                      f"Roll {math.degrees(roll):.1f}°   "
                      f"Pitch {math.degrees(pitch):.1f}°   "
                      f"Yaw {math.degrees(yaw):.1f}°"))
        ax.set_box_aspect((1, 1, 1))
        ax.view_init(elev=24, azim=35)
        fig.canvas.draw_idle()

    def toggle_play(_event):
        nonlocal playing
        playing = not playing
        previous_tick[0] = None
        button.label.set_text("Pause" if playing else "Play")

    def tick():
        if not playing:
            return
        import time
        now = time.monotonic()
        current_time = float(slider.val)
        if previous_tick[0] is None:
            previous_tick[0] = now
            return
        target_time = current_time + (now - previous_tick[0]) * args.speed
        previous_tick[0] = now
        if target_time >= times[-1]:
            slider.set_val(times[-1])
            toggle_play(None)
            return
        slider.set_val(target_time)

    slider.on_changed(draw)
    button.on_clicked(toggle_play)
    timer = fig.canvas.new_timer(interval=30)
    timer.add_callback(tick)
    timer.start()
    draw(0)
    note = "Magnetic heading correction enabled" if has_mag else \
        "No magnetometer samples: yaw can drift over time"
    fig.text(0.17, 0.045, note, fontsize=9)
    plt.show()


if __name__ == "__main__":
    main()
```

### 6.32 ไฟล์ `tools/view_body.py` — โปรแกรมจำลองการวางตัวของร่างกาย 3 มิติด้วย Python (118 บรรทัด)
```python
"""Show the simulated body and IMU magnitude agreement at each recorded point."""

import argparse
import csv
import math
from pathlib import Path


EDGES = [
    ("head", "neck"), ("neck", "chest"), ("chest", "lumbar"),
    ("lumbar", "pelvis"),
    ("chest", "left_upper_arm"), ("left_upper_arm", "left_forearm"),
    ("chest", "right_upper_arm"), ("right_upper_arm", "right_forearm"),
    ("pelvis", "left_thigh"), ("left_thigh", "left_shin"),
    ("pelvis", "right_thigh"), ("right_thigh", "right_shin"),
]


def read_csv(path):
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def vector(row, prefix, point):
    try:
        return [float(row[f"{prefix}_{point}_{axis}"]) for axis in "xyz"]
    except (KeyError, ValueError):
        return None


def norm(values):
    return math.sqrt(sum(value * value for value in values))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--simulation", required=True, type=Path,
                        help="Simulation CSV containing pos_* and accel_*/gyro_* columns")
    parser.add_argument("--hardware", required=True, type=Path,
                        help="Wide CSV produced by merge.py")
    parser.add_argument("--offset", type=float, default=0,
                        help="Simulation time corresponding to hardware time zero, in seconds")
    args = parser.parse_args()

    try:
        import matplotlib.pyplot as plt
        from matplotlib.widgets import Slider
    except ImportError as exc:
        raise SystemExit("Install matplotlib first: python -m pip install matplotlib") from exc

    sim = read_csv(args.simulation)
    hw = read_csv(args.hardware)
    if not sim or not hw:
        parser.error("Both CSV files must contain samples")
    sim_times = [float(row["time_s"]) for row in sim]
    hw_times = [float(row["time_s"]) for row in hw]
    hw_points = {key[len("accel_"):-2] for key in hw[0]
                 if key.startswith("accel_") and key.endswith("_x")}
    sim_points = {key[len("pos_"):-2] for key in sim[0]
                  if key.startswith("pos_") and key.endswith("_x")}
    points = sorted(hw_points & sim_points)
    if not points:
        parser.error("No matching hardware point and simulation pos_* columns")

    fig = plt.figure(figsize=(11, 7))
    ax = fig.add_axes([0.05, 0.16, 0.65, 0.78], projection="3d")
    note = fig.add_axes([0.73, 0.20, 0.25, 0.70])
    note.axis("off")
    slider_ax = fig.add_axes([0.15, 0.07, 0.70, 0.03])
    slider = Slider(slider_ax, "HW time (s)", hw_times[0], hw_times[-1],
                    valinit=hw_times[0])

    def nearest_index(values, target):
        return min(range(len(values)), key=lambda i: abs(values[i] - target))

    def draw(hw_time):
        hw_row = hw[nearest_index(hw_times, hw_time)]
        sim_time = hw_time + args.offset
        sim_row = sim[nearest_index(sim_times, sim_time)]
        ax.clear()
        locations = {point: vector(sim_row, "pos", point) for point in sim_points}
        for first, second in EDGES:
            a, b = locations.get(first), locations.get(second)
            if a and b:
                ax.plot([a[0], b[0]], [a[1], b[1]], [a[2], b[2]],
                        color="0.55", linewidth=3)
        lines = []
        for point in points:
            pos = locations[point]
            measured = vector(hw_row, "accel", point)
            expected = vector(sim_row, "accel", point)
            if pos is None or measured is None or expected is None:
                continue
            difference = abs(norm(measured) - norm(expected))
            color = "tab:green" if difference < 2 else "tab:orange" if difference < 5 else "tab:red"
            ax.scatter(*pos, color=color, s=90)
            lines.append(f"{point}: |accel| diff {difference:.1f} m/s²")
        ax.set(xlabel="X (m)", ylabel="Y (m)", zlabel="Z (m)",
               title=f"Simulation body at {sim_time:.2f} s")
        ax.set_xlim(-1.2, 1.2)
        ax.set_ylim(-0.5, 2.2)
        ax.set_zlim(0, 2.2)
        note.clear()
        note.axis("off")
        note.text(0, 1, "Hardware vs simulation\n" + "\n".join(lines) +
                  "\n\nGreen <2, orange <5, red ≥5 m/s²\n"
                  "Markers use simulated positions.\n"
                  "Align motions with --offset.", va="top", fontsize=9)
        fig.canvas.draw_idle()

    slider.on_changed(draw)
    draw(hw_times[0])
    plt.show()


if __name__ == "__main__":
    main()
```

### 6.33 ไฟล์ `tools/watch_imu_rates.py` — เครื่องมือวัดความถี่ (Hz) และอัตราการตกหล่นของแพ็กเก็ต IMU (222 บรรทัด)
```python
#!/usr/bin/env python3
"""Measure Gateway MQTT and per-Node IMU sequence rates without publishing."""

from __future__ import annotations

import argparse
import json
import time
from dataclasses import dataclass

import paho.mqtt.client as mqtt


@dataclass
class NodeStats:
    observations: int = 0
    valid_frames: int = 0
    invalid_frames: int = 0
    valid_unique_samples: int = 0
    duplicate_seq_frames: int = 0
    sequence_updates: int = 0
    missing_seq_numbers: int = 0
    backward_or_reset: int = 0
    absent_gateway_frames: int = 0
    first_seq: int | None = None
    last_seq: int | None = None
    first_device_s: float | None = None
    last_device_s: float | None = None
    last_valid_seq: int | None = None
    last_valid_host_mono: float | None = None
    max_valid_interval_ms: float = 0.0

    def observe(self, node: dict, now: float) -> None:
        seq = node.get("seq")
        device_ms = node.get("ts")
        valid = node.get("valid") is True and node.get("isMock") is not True
        if not isinstance(seq, int) or not isinstance(device_ms, (int, float)):
            return

        device_s = float(device_ms) / 1000.0
        self.observations += 1
        if self.first_seq is None:
            self.first_seq = seq
            self.first_device_s = device_s

        if self.last_seq is not None:
            delta = (seq - self.last_seq) & 0xFFFF
            if delta == 0:
                self.duplicate_seq_frames += 1
            elif delta < 0x8000:
                self.sequence_updates += 1
                self.missing_seq_numbers += max(0, delta - 1)
            else:
                self.backward_or_reset += 1
        self.last_seq = seq
        self.last_device_s = device_s

        if not valid:
            self.invalid_frames += 1
            return

        self.valid_frames += 1
        if self.last_valid_seq != seq:
            self.valid_unique_samples += 1
            if self.last_valid_host_mono is not None:
                gap_ms = (now - self.last_valid_host_mono) * 1000.0
                self.max_valid_interval_ms = max(self.max_valid_interval_ms, gap_ms)
            self.last_valid_seq = seq
            self.last_valid_host_mono = now


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="10.178.191.76", help="MQTT TCP broker host")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--topic", default="planko/imu")
    parser.add_argument("--gateway", default="", help="Gateway clientId; auto-select first by default")
    parser.add_argument("--duration", type=float, default=30.0, help="measurement seconds")
    args = parser.parse_args()

    state = {
        "gateway_id": args.gateway or None,
        "started_mono": None,
        "first_mqtt_seq": None,
        "last_mqtt_seq": None,
        "mqtt_frames": 0,
        "mqtt_duplicates": 0,
        "mqtt_missing_seq": 0,
        "mqtt_resets": 0,
        "malformed": 0,
        "nodes": {},
    }

    client = mqtt.Client(
        callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
        client_id=f"PlanKO_RateCheck_{int(time.time())}",
        protocol=mqtt.MQTTv311,
    )

    def on_connect(mqtt_client, userdata, flags, reason_code, properties):
        if reason_code.is_failure:
            print(f"MQTT connect rejected: {reason_code}")
            return
        result, _ = mqtt_client.subscribe(args.topic, qos=0)
        if result != mqtt.MQTT_ERR_SUCCESS:
            print(f"MQTT subscribe failed: rc={result}")
        else:
            print(f"Subscribed to {args.topic} on {args.host}:{args.port}")

    def on_message(_client, _userdata, message):
        now = time.monotonic()
        try:
            data = json.loads(message.payload.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            state["malformed"] += 1
            return
        if data.get("type") != "imu_stream" or not isinstance(data.get("nodes"), list):
            return

        gateway = data.get("gateway") or {}
        gateway_id = gateway.get("clientId")
        if not gateway_id:
            return
        if state["gateway_id"] is None:
            state["gateway_id"] = gateway_id
            print(f"Measuring Gateway {gateway_id} for {args.duration:g}s")
        if gateway_id != state["gateway_id"]:
            return

        if state["started_mono"] is None:
            state["started_mono"] = now
        state["mqtt_frames"] += 1

        mqtt_seq = gateway.get("mqttSeq")
        if isinstance(mqtt_seq, int):
            if state["first_mqtt_seq"] is None:
                state["first_mqtt_seq"] = mqtt_seq
            if state["last_mqtt_seq"] is not None:
                delta = (mqtt_seq - state["last_mqtt_seq"]) & 0xFFFFFFFF
                if delta == 0:
                    state["mqtt_duplicates"] += 1
                elif delta < 0x80000000:
                    state["mqtt_missing_seq"] += max(0, delta - 1)
                else:
                    state["mqtt_resets"] += 1
            state["last_mqtt_seq"] = mqtt_seq

        seen = set()
        for node in data["nodes"]:
            if not isinstance(node, dict) or not isinstance(node.get("nodeId"), int):
                continue
            node_id = node["nodeId"]
            seen.add(node_id)
            stats = state["nodes"].setdefault(node_id, NodeStats())
            stats.observe(node, now)
        for node_id, stats in state["nodes"].items():
            if node_id not in seen:
                stats.absent_gateway_frames += 1

    client.on_connect = on_connect
    client.on_message = on_message

    try:
        client.connect(args.host, args.port, keepalive=30)
        client.loop_start()
        deadline = time.monotonic() + args.duration
        while time.monotonic() < deadline:
            time.sleep(min(0.2, max(0.0, deadline - time.monotonic())))
    except KeyboardInterrupt:
        pass
    except OSError as exc:
        print(f"Could not connect to {args.host}:{args.port}: {exc}")
        return 2
    finally:
        try:
            client.disconnect()
            client.loop_stop()
        except Exception:
            pass

    elapsed = args.duration
    if state["started_mono"] is not None:
        elapsed = max(0.001, time.monotonic() - state["started_mono"])
    print(f"\nGateway: {state['gateway_id'] or '(no messages received)'}")
    print(
        f"MQTT: {state['mqtt_frames']} frames / {elapsed:.1f}s "
        f"({state['mqtt_frames'] / elapsed:.1f} Hz), "
        f"missing mqttSeq={state['mqtt_missing_seq']}, "
        f"duplicate mqttSeq={state['mqtt_duplicates']}, resets={state['mqtt_resets']}"
    )
    if not state["nodes"]:
        print("No imu_stream nodes received.")
        return 1

    for node_id, stats in sorted(state["nodes"].items()):
        device_span = 0.0
        sequence_span = 0
        if stats.first_device_s is not None and stats.last_device_s is not None:
            device_span = max(0.0, stats.last_device_s - stats.first_device_s)
        if stats.first_seq is not None and stats.last_seq is not None:
            sequence_span = (stats.last_seq - stats.first_seq) & 0xFFFF
        generated_rate = sequence_span / device_span if device_span > 0 else 0.0
        print(
            f"Node #{node_id}: observations={stats.observations} "
            f"({stats.observations / elapsed:.1f}/s), valid frames={stats.valid_frames}, "
            f"invalid={stats.invalid_frames}, unique valid samples={stats.valid_unique_samples} "
            f"({stats.valid_unique_samples / elapsed:.1f} Hz), "
            f"duplicate seq={stats.duplicate_seq_frames}, "
            f"missing seq={stats.missing_seq_numbers}, "
            f"absent frames={stats.absent_gateway_frames}, "
            f"seq progress={generated_rate:.1f} Hz, "
            f"max valid interval={stats.max_valid_interval_ms:.0f} ms, "
            f"backward/reset={stats.backward_or_reset}"
        )
    if state["malformed"]:
        print(f"Malformed JSON payloads: {state['malformed']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

### 6.34 ไฟล์ `tools/compare_gateway_rates.py` — เครื่องมือเปรียบเทียบประสิทธิภาพการส่งข้อมูลของ Gateway (240 บรรทัด)
```python
#!/usr/bin/env python3
"""Compare raw BLE samples buffered by the Gateway with its MQTT stream.

This is a read-only diagnostic. It polls the Gateway's /samples endpoint and
subscribes to the configured MQTT topic; it does not publish or change device
state.
"""

from __future__ import annotations

import argparse
import json
import threading
import time
from dataclasses import dataclass
from urllib.error import URLError
from urllib.parse import urlencode
from urllib.request import urlopen

import paho.mqtt.client as mqtt


@dataclass
class StreamStats:
    frames: int = 0
    duplicate_seq: int = 0
    missing_seq: int = 0
    unique_valid: int = 0
    last_seq: int | None = None
    observations: int = 0


@dataclass
class GatewayStats:
    samples: int = 0
    missing_seq: int = 0
    last_seq: int | None = None
    boot_id: int | None = None
    connected: bool = False
    total_dropped: int = 0
    name: str = ""


def seq_delta(new: int, old: int, bits: int = 16) -> int:
    return (new - old) & ((1 << bits) - 1)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gateway-url", required=True, help="Gateway base URL, e.g. http://10.0.0.5")
    parser.add_argument("--mqtt-host", default="10.178.191.76")
    parser.add_argument("--mqtt-port", type=int, default=1883)
    parser.add_argument("--topic", default="planko/imu")
    parser.add_argument("--gateway-id", default="PlanKO_GW_5C6E27AC")
    parser.add_argument("--duration", type=float, default=15.0)
    parser.add_argument("--poll-ms", type=int, default=250)
    args = parser.parse_args()

    lock = threading.Lock()
    streams: dict[int, StreamStats] = {}
    mqtt_state = {"frames": 0, "missing": 0, "duplicates": 0, "last_seq": None}
    connected_event = threading.Event()
    selected_gateway: str | None = None

    client = mqtt.Client(
        callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
        client_id=f"PlanKO_Compare_{int(time.time())}",
        protocol=mqtt.MQTTv311,
    )

    def on_connect(c, _userdata, _flags, reason_code, _properties):
        if reason_code.is_failure:
            print(f"MQTT connect rejected: {reason_code}")
            return
        c.subscribe(args.topic, qos=0)
        connected_event.set()

    def on_message(_client, _userdata, message):
        nonlocal selected_gateway
        try:
            data = json.loads(message.payload.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            return
        if data.get("type") != "imu_stream" or not isinstance(data.get("nodes"), list):
            return
        gateway = data.get("gateway") or {}
        gateway_id = gateway.get("clientId")
        if not gateway_id:
            return
        if selected_gateway is None:
            selected_gateway = gateway_id
        if gateway_id != args.gateway_id:
            return

        with lock:
            mqtt_state["frames"] += 1
            mqtt_seq = gateway.get("mqttSeq")
            if isinstance(mqtt_seq, int):
                old = mqtt_state["last_seq"]
                if old is not None:
                    delta = seq_delta(mqtt_seq, old, 32)
                    if delta == 0:
                        mqtt_state["duplicates"] += 1
                    elif delta < 0x80000000:
                        mqtt_state["missing"] += max(0, delta - 1)
                mqtt_state["last_seq"] = mqtt_seq

            for node in data["nodes"]:
                if not isinstance(node, dict) or not isinstance(node.get("nodeId"), int):
                    continue
                node_id = node["nodeId"]
                stat = streams.setdefault(node_id, StreamStats())
                stat.observations += 1
                seq = node.get("seq")
                if not isinstance(seq, int):
                    continue
                if node.get("valid") is True and node.get("isMock") is not True:
                    if stat.last_seq == seq:
                        stat.duplicate_seq += 1
                    else:
                        stat.unique_valid += 1
                        if stat.last_seq is not None:
                            delta = seq_delta(seq, stat.last_seq)
                            if 0 < delta < 0x8000:
                                stat.missing_seq += max(0, delta - 1)
                        stat.last_seq = seq

    client.on_connect = on_connect
    client.on_message = on_message
    try:
        client.connect(args.mqtt_host, args.mqtt_port, keepalive=30)
        client.loop_start()
        if not connected_event.wait(8):
            print(f"MQTT connection timed out: {args.mqtt_host}:{args.mqtt_port}")
            return 2

        base = args.gateway_url.rstrip("/")
        initial_url = f"{base}/samples"
        with urlopen(initial_url, timeout=5) as response:
            initial = json.loads(response.read().decode("utf-8"))
        gateway_stats: dict[int, GatewayStats] = {}
        for node in initial.get("nodes", []):
            if not isinstance(node, dict) or not isinstance(node.get("nodeId"), int):
                continue
            node_id = node["nodeId"]
            history = node.get("samples", [])
            last_seq = history[-1].get("seq") if history else None
            gateway_stats[node_id] = GatewayStats(
                last_seq=last_seq if isinstance(last_seq, int) else None,
                boot_id=node.get("boot_id"),
                connected=node.get("connected") is True,
                total_dropped=int(node.get("total_dropped") or 0),
                name=str(node.get("name") or ""),
            )

        print(f"Comparing Gateway {args.gateway_id} on {args.gateway_url} and {args.mqtt_host}:{args.mqtt_port}/{args.topic}")
        started = time.monotonic()
        deadline = started + args.duration
        failures = 0
        while time.monotonic() < deadline:
            query: dict[str, str] = {}
            for node_id, stat in gateway_stats.items():
                if stat.last_seq is not None and stat.boot_id is not None:
                    query[f"n{node_id}_boot_id"] = str(stat.boot_id)
                    query[f"n{node_id}_after_seq"] = str(stat.last_seq)
            url = f"{base}/samples?{urlencode(query)}" if query else initial_url
            try:
                with urlopen(url, timeout=5) as response:
                    payload = json.loads(response.read().decode("utf-8"))
                failures = 0
            except (OSError, URLError, TimeoutError, json.JSONDecodeError):
                failures += 1
                if failures == 3:
                    print("Warning: three consecutive Gateway /samples polls failed")
                time.sleep(min(args.poll_ms / 1000.0, max(0.0, deadline - time.monotonic())))
                continue

            with lock:
                for node in payload.get("nodes", []):
                    if not isinstance(node, dict) or not isinstance(node.get("nodeId"), int):
                        continue
                    node_id = node["nodeId"]
                    stat = gateway_stats.setdefault(node_id, GatewayStats())
                    stat.connected = node.get("connected") is True
                    stat.total_dropped = int(node.get("total_dropped") or 0)
                    stat.name = str(node.get("name") or stat.name)
                    current_boot = node.get("boot_id")
                    if current_boot != stat.boot_id:
                        stat.boot_id = current_boot
                        stat.last_seq = None
                    for sample in node.get("samples", []):
                        seq = sample.get("seq")
                        if not isinstance(seq, int):
                            continue
                        if stat.last_seq is None:
                            stat.last_seq = seq
                            continue
                        delta = seq_delta(seq, stat.last_seq)
                        if 0 < delta < 0x8000:
                            stat.samples += 1
                            stat.missing_seq += max(0, delta - 1)
                            stat.last_seq = seq

            time.sleep(min(args.poll_ms / 1000.0, max(0.0, deadline - time.monotonic())))

        elapsed = max(0.001, time.monotonic() - started)
        with lock:
            print(
                f"MQTT: {mqtt_state['frames']} frames / {elapsed:.1f}s "
                f"({mqtt_state['frames'] / elapsed:.1f} Hz), "
                f"missing mqttSeq={mqtt_state['missing']}, duplicate mqttSeq={mqtt_state['duplicates']}"
            )
            all_ids = sorted(set(gateway_stats) | set(streams))
            for node_id in all_ids:
                gw = gateway_stats.get(node_id, GatewayStats())
                stream = streams.get(node_id, StreamStats())
                print(
                    f"Node #{node_id} {gw.name}: Gateway /samples={gw.samples} "
                    f"({gw.samples / elapsed:.1f} Hz), missing seq={gw.missing_seq}, "
                    f"BLE telemetry drops={gw.total_dropped}, connected={gw.connected}; "
                    f"MQTT observations={stream.observations}, unique valid="
                    f"{stream.unique_valid} ({stream.unique_valid / elapsed:.1f} Hz), "
                    f"duplicate snapshots={stream.duplicate_seq}, "
                    f"missing seq={stream.missing_seq}"
                )
        return 0
    except (OSError, URLError, TimeoutError, json.JSONDecodeError) as exc:
        print(f"Gateway /samples unavailable: {exc}")
        return 2
    finally:
        try:
            client.disconnect()
            client.loop_stop()
        except Exception:
            pass


if __name__ == "__main__":
    raise SystemExit(main())
```

### 6.35 ไฟล์ `tools/imu_features.py` — โมดูลสกัดฟีเจอร์ทางสถิติจากหน้าต่างเวลา 1.0 วินาที (Feature Extraction) (258 บรรทัด)
```python
"""Shared causal window feature extraction for IMU training and inference."""

from __future__ import annotations

import math
from collections import deque
from typing import Iterable, Mapping

import numpy as np

RAW_CHANNELS = tuple(f"{kind}_{axis}" for kind in ("accel", "gyro") for axis in "xyz")
FEATURE_VERSION = "imu-window-v2-site-gated"


def _finite(row: Mapping, key: str) -> float:
    value = row.get(key)
    try:
        number = float(value)
    except (TypeError, ValueError):
        return float("nan")
    return number if math.isfinite(number) else float("nan")


def window_features(rows: Iterable[Mapping], site: str, expected_hz: float = 20.0,
                    use_calibrated_angles: bool = False, body_part: str = "other",
                    side: str = "unknown") -> dict[str, float]:
    """Compute one feature row from already validated, time-ordered samples.

    Raw accelerometer values are m/s^2 and gyro values are rad/s. Continuous
    channels are gated by mount site so different installation axes never share
    one feature column.
    Calibrated relative angles are used only when every row has a valid profile.
    """
    values = list(rows)
    if len(values) < 3:
        raise ValueError("A window needs at least three samples")
    times = np.asarray([_finite(row, "device_time_s") for row in values], dtype=float)
    if not np.all(np.isfinite(times)) or np.any(np.diff(times) <= 0):
        raise ValueError("Window has invalid or non-increasing device timestamps")
    duration = times[-1] - times[0]
    if duration <= 0 or duration > max(1.25, len(values) / expected_hz * 1.8):
        raise ValueError("Window duration is inconsistent with its sample count")

    features: dict[str, float] = {}
    arrays: dict[str, np.ndarray] = {}
    for channel in RAW_CHANNELS:
        data = np.asarray([_finite(row, channel) for row in values], dtype=float)
        if not np.all(np.isfinite(data)):
            raise ValueError(f"Window has missing {channel} values")
        arrays[channel] = data
        features[f"{channel}_mean"] = float(np.mean(data))
        features[f"{channel}_std"] = float(np.std(data))
        features[f"{channel}_rms"] = float(np.sqrt(np.mean(np.square(data))))
        features[f"{channel}_range"] = float(np.max(data) - np.min(data))
        features[f"{channel}_slope_per_s"] = float(np.polyfit(times - times[0], data, 1)[0])

    for kind in ("accel", "gyro"):
        magnitude = np.sqrt(sum(np.square(arrays[f"{kind}_{axis}"]) for axis in "xyz"))
        features[f"{kind}_magnitude_mean"] = float(np.mean(magnitude))
        features[f"{kind}_magnitude_std"] = float(np.std(magnitude))
        features[f"{kind}_magnitude_rms"] = float(np.sqrt(np.mean(np.square(magnitude))))

    features[f"site__{site}"] = 1.0
    features[f"body_part__{body_part or 'other'}"] = 1.0
    features[f"side__{side or 'unknown'}"] = 1.0

    calibrated = use_calibrated_angles and all(
        str(row.get("calibrated", "")).lower() in ("true", "1")
        and str(row.get("relative_orientation_valid", row.get("calibrated", ""))).lower() in ("true", "1")
        for row in values
    )
    if calibrated:
        angles = {}
        for angle in ("relative_roll_deg", "relative_pitch_deg"):
            data = np.asarray([_finite(row, angle) for row in values], dtype=float)
            if not np.all(np.isfinite(data)):
                raise ValueError(f"Calibrated window is missing {angle}")
            angles[angle] = data
        for angle, data in angles.items():
            features[f"{angle}_mean"] = float(np.mean(data))
            features[f"{angle}_std"] = float(np.std(data))
            features[f"{angle}_range"] = float(np.max(data) - np.min(data))
            features[f"{angle}_slope_per_s"] = float(np.polyfit(times - times[0], data, 1)[0])
        features["relative_orientation_valid"] = 1.0
    else:
        features["relative_orientation_valid"] = 0.0
    context = {name: value for name, value in features.items()
               if name.startswith(("site__", "body_part__", "side__"))
               or name == "relative_orientation_valid"}
    scoped = {f"{name}__at_{site}": value for name, value in features.items()
              if name not in context}
    return {**scoped, **context}


def feature_names(site_names: Iterable[str], use_calibrated_angles: bool,
                  body_parts: Iterable[str] = (), sides: Iterable[str] = ()) -> list[str]:
    base = []
    for channel in RAW_CHANNELS:
        base.extend(f"{channel}_{stat}" for stat in ("mean", "std", "rms", "range", "slope_per_s"))
    for kind in ("accel", "gyro"):
        base.extend(f"{kind}_magnitude_{stat}" for stat in ("mean", "std", "rms"))
    if use_calibrated_angles:
        for angle in ("relative_roll_deg", "relative_pitch_deg"):
            base.extend(f"{angle}_{stat}" for stat in ("mean", "std", "range", "slope_per_s"))
    names = [f"{feature}__at_{site}" for site in sorted(set(site_names)) for feature in base]
    names.extend(f"site__{name}" for name in sorted(set(site_names)))
    names.extend(f"body_part__{name}" for name in sorted(set(body_parts)))
    names.extend(f"side__{name}" for name in sorted(set(sides or ("unknown",))))
    names.append("relative_orientation_valid")
    return names


def samples_to_windows(rows: list[dict], window_seconds: float, expected_hz: float,
                       gap_reset_seconds: float = 0.15,
                       use_calibrated_angles: bool = False) -> list[dict]:
    """Extract non-overlapping windows; never cross trial, boot, gap or calibration."""
    if window_seconds <= 0 or expected_hz <= 0:
        raise ValueError("window_seconds and expected_hz must be positive")
    if not rows:
        return []
    # Input order is capture order; sorting boot-time clocks would hide a reboot.
    result: list[dict] = []
    current: list[dict] = []
    for row in rows:
        if not row.get("valid", True):
            current = []
            continue
        if current:
            prev = current[-1]
            boundary = any(row.get(key) != prev.get(key) for key in
                           ("subject_id", "session_id", "trial_id", "stream_epoch",
                            "calibration_id", "calibration_method_version", "sensor_uid",
                            "mount_site", "body_part", "side"))
            gap = float(row["device_time_s"]) - float(prev["device_time_s"])
            seq_delta = (int(row["seq"]) - int(prev["seq"])) if (row.get("seq") is not None and prev.get("seq") is not None) else 1
            seq_gap = seq_delta != 1
            if boundary or seq_gap or row.get("gap_before") in (True, "true", 1, "1") or gap <= 0 or gap > gap_reset_seconds:
                current = []
        current.append(row)
        if float(row["device_time_s"]) - float(current[0]["device_time_s"]) + 1e-9 < window_seconds:
            continue
        window_rows = current[:]
        while (len(window_rows) > 3 and
               float(window_rows[-1]["device_time_s"]) - float(window_rows[0]["device_time_s"])
               > window_seconds + 1e-9):
            window_rows.pop(0)
        duration = float(window_rows[-1]["device_time_s"]) - float(window_rows[0]["device_time_s"])
        expected_count = max(3, int(round(duration * expected_hz)))
        if len(window_rows) < expected_count * 0.65:
            current = []
            continue
        site = str(window_rows[-1].get("mount_site") or "other")
        try:
            features = window_features(window_rows, site, expected_hz, use_calibrated_angles,
                                       str(window_rows[-1].get("body_part") or "other"),
                                       str(window_rows[-1].get("side") or "unknown"))
        except ValueError:
            current = []
            continue
        action = window_rows[-1].get("action_label")
        phase = window_rows[-1].get("phase_label")
        # Keep only windows whose annotation is consistent from start to finish.
        if action and all(row.get("action_label") == action for row in window_rows):
            if phase and all(row.get("phase_label") == phase for row in window_rows):
                result.append({
                    "features": features,
                    "action_label": action,
                    "phase_label": phase,
                    "body_part": window_rows[-1].get("body_part", ""),
                    "mount_site": site,
                    "subject_id": window_rows[-1].get("subject_id", ""),
                    "session_id": window_rows[-1].get("session_id", ""),
                    "trial_id": window_rows[-1].get("trial_id", ""),
                    "stream_epoch": window_rows[-1].get("stream_epoch", 0),
                    "sensor_uid": window_rows[-1].get("sensor_uid", ""),
                    "calibration_id": window_rows[-1].get("calibration_id", ""),
                    "side": window_rows[-1].get("side") or "unknown",
                    "calibration_method_version": window_rows[-1].get("calibration_method_version", 0),
                    "calibrated": window_rows[-1].get("calibrated", False),
                })
            elif not phase:
                # Legacy recordings can train an action-only prototype.
                result.append({
                    "features": features,
                    "action_label": action,
                    "phase_label": "",
                    "body_part": window_rows[-1].get("body_part", ""),
                    "mount_site": site,
                    "subject_id": window_rows[-1].get("subject_id", ""),
                    "session_id": window_rows[-1].get("session_id", ""),
                    "trial_id": window_rows[-1].get("trial_id", ""),
                    "stream_epoch": window_rows[-1].get("stream_epoch", 0),
                    "sensor_uid": window_rows[-1].get("sensor_uid", ""),
                    "calibration_id": window_rows[-1].get("calibration_id", ""),
                    "side": window_rows[-1].get("side") or "unknown",
                    "calibration_method_version": window_rows[-1].get("calibration_method_version", 0),
                    "calibrated": window_rows[-1].get("calibrated", False),
                })
        # Stride is about half a window; samples are still causal.
        while len(current) > 1 and float(current[-1]["device_time_s"]) - float(current[1]["device_time_s"]) >= window_seconds * 0.5:
            current.pop(0)
    return result


class StreamBuffer:
    """Per-stream ring buffer that resets on duplicates, gaps and stream changes."""

    def __init__(self, window_seconds: float, expected_hz: float, gap_reset_seconds: float = 0.15):
        self.window_seconds = window_seconds
        self.expected_hz = expected_hz
        self.gap_reset_seconds = gap_reset_seconds
        self.rows: deque[dict] = deque(maxlen=max(8, int(math.ceil(window_seconds * expected_hz * 2))))
        self.last_seq = None

    def add(self, row: dict) -> bool:
        if not row.get("valid", True):
            self.reset()
            return False
        seq = int(row["seq"])
        if self.last_seq is not None and seq == self.last_seq:
            # Polling clients often ask faster than the sensor samples. Ignore
            # the repeated observation without disturbing smoothing state.
            return False
        if self.last_seq is not None and seq < self.last_seq:
            self.reset()
        if self.rows:
            prev = self.rows[-1]
            gap = float(row["device_time_s"]) - float(prev["device_time_s"])
            if gap <= 0 or gap > self.gap_reset_seconds or any(
                row.get(key) != prev.get(key) for key in
                ("stream_epoch", "calibration_id", "calibration_method_version", "sensor_uid",
                 "mount_site", "body_part", "side")
            ):
                self.reset()
            elif int(row["seq"]) != int(prev["seq"]) + 1:
                self.reset()
        self.rows.append(row)
        self.last_seq = seq
        return True

    def reset(self):
        self.rows.clear()
        self.last_seq = None

    def features(self, site: str, use_calibrated_angles: bool = False):
        if len(self.rows) < 3:
            return None
        if float(self.rows[-1]["device_time_s"]) - float(self.rows[0]["device_time_s"]) < self.window_seconds:
            return None
        rows = list(self.rows)
        while (len(rows) > 3 and
               float(rows[-1]["device_time_s"]) - float(rows[0]["device_time_s"])
               > self.window_seconds + 1e-9):
            rows.pop(0)
        return window_features(rows, site, self.expected_hz, use_calibrated_angles,
                               str(rows[-1].get("body_part") or "other"),
                               str(rows[-1].get("side") or "unknown"))
```

### 6.36 ไฟล์ `tools/plank_phase.py` — โมดูลคำนวณมุมสัมพัทธ์และติดตามจังหวะการทำแพลงก์ 5 ขั้นตอน (Plank Phase Tracker) (384 บรรทัด)
```python
"""Experimental six-axis relative-angle score and temporal plank phase logic.

This module deliberately does not claim to be a trained posture classifier.
The current hardware has no magnetometer, so only the fused pitch/roll channels
from the MPU-6050 complementary filter are used. A per-session baseline is
captured in a verified normal plank before phase tracking starts.
"""

from __future__ import annotations

import json
import math
from collections import deque
from pathlib import Path
from typing import Mapping

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
CONFIG_PATH = ROOT / "config" / "plank_model.json"


class PlankInputError(ValueError):
    """Bad, stale, unsupported or incomplete paired IMU data."""


def load_config() -> dict:
    return json.loads(CONFIG_PATH.read_text(encoding="utf-8"))


def wrap_degrees(value: float) -> float:
    return (value + 180.0) % 360.0 - 180.0


def _finite(value, label: str) -> float:
    try:
        result = float(value)
    except (TypeError, ValueError):
        raise PlankInputError(f"missing_{label}") from None
    if not math.isfinite(result):
        raise PlankInputError(f"invalid_{label}")
    return result


def _validate_node(node: Mapping, name: str, allowed_sites: list[str]) -> dict:
    if not isinstance(node, Mapping):
        raise PlankInputError(f"missing_{name}_node")
    site = str(node.get("mount_site") or "").strip().lower()
    if site not in allowed_sites:
        raise PlankInputError(f"{name}_site_must_be_one_of_{'_or_'.join(allowed_sites)}")
    if node.get("valid") is not True or node.get("isMock") is True:
        raise PlankInputError(f"{name}_node_not_a_valid_real_sample")
    try:
        seq = int(node["seq"])
        node_id = int(node["node_id"])
    except (KeyError, TypeError, ValueError):
        raise PlankInputError(f"invalid_{name}_identity_or_sequence") from None
    if not 0 <= seq <= 65535 or node_id < 1:
        raise PlankInputError(f"invalid_{name}_identity_or_sequence")
    axes = {}
    for sensor in ("accel", "gyro"):
        values = node.get(sensor)
        if not isinstance(values, list) or len(values) != 3:
            raise PlankInputError(f"missing_{name}_{sensor}_six_axis_data")
        axes[sensor] = [_finite(value, f"{name}_{sensor}") for value in values]
    return {
        "node_id": node_id,
        "sensor_uid": str(node.get("sensor_uid") or f"node-{node_id}"),
        "mount_site": site,
        "body_part": str(node.get("body_part") or "unknown").strip().lower(),
        "side": str(node.get("side") or "unknown").strip().lower(),
        "seq": seq,
        "pitch": _finite(node.get("pitch"), f"{name}_pitch"),
        "roll": _finite(node.get("roll"), f"{name}_roll"),
        "accel": axes["accel"],
        "gyro": axes["gyro"],
        "calibrated": node.get("calibrated") is True,
    }


def validate_pair(frame: Mapping, config: dict | None = None) -> dict:
    config = config or load_config()
    pair_config = config["sensor_pair"]
    if not isinstance(frame, Mapping):
        raise PlankInputError("invalid_frame")
    upper = _validate_node(frame.get("upper"), "upper", pair_config["upper_sites"])
    lower = _validate_node(frame.get("lower"), "lower", pair_config["lower_sites"])
    if upper["node_id"] == lower["node_id"] or upper["sensor_uid"] == lower["sensor_uid"]:
        raise PlankInputError("upper_and_lower_must_be_two_distinct_imus")
    if upper["side"] != lower["side"] and "unknown" not in (upper["side"], lower["side"]):
        raise PlankInputError("upper_and_lower_side_metadata_mismatch")
    return {"upper": upper, "lower": lower}


def relative_angles(pair: Mapping) -> tuple[float, float]:
    upper, lower = pair["upper"], pair["lower"]
    return (
        wrap_degrees(lower["pitch"] - upper["pitch"]),
        wrap_degrees(lower["roll"] - upper["roll"]),
    )


def _circular_mean(values: list[float]) -> float:
    radians = np.deg2rad(values)
    return wrap_degrees(math.degrees(math.atan2(float(np.sin(radians).mean()),
                                                 float(np.cos(radians).mean()))))


def _circular_std(values: list[float]) -> float:
    radians = np.deg2rad(values)
    resultant = math.hypot(float(np.sin(radians).mean()), float(np.cos(radians).mean()))
    return math.degrees(math.sqrt(max(0.0, -2.0 * math.log(max(resultant, 1e-12)))))


def make_calibration(frames: list[Mapping], config: dict | None = None) -> dict:
    config = config or load_config()
    rules = config["calibration"]
    minimum_samples = int(rules["minimum_samples"])
    if not isinstance(frames, list) or len(frames) < minimum_samples:
        raise PlankInputError(f"need_at_least_{minimum_samples}_calibration_samples")
    validated = [validate_pair(frame, config) for frame in frames]
    pair_context = None
    pitch_values, roll_values, times = [], [], []
    node_pitch = {"upper": [], "lower": []}
    node_roll = {"upper": [], "lower": []}
    for frame, pair in zip(frames, validated):
        context = tuple((pair[part]["sensor_uid"], pair[part]["node_id"], pair[part]["mount_site"])
                        for part in ("upper", "lower"))
        if pair_context is not None and context != pair_context:
            raise PlankInputError("sensor_pair_changed_during_calibration")
        pair_context = context
        pitch, roll = relative_angles(pair)
        pitch_values.append(pitch)
        roll_values.append(roll)
        for part in ("upper", "lower"):
            node_pitch[part].append(pair[part]["pitch"])
            node_roll[part].append(pair[part]["roll"])
        times.append(_finite(frame.get("received_time_s"), "calibration_time"))
    if any(right <= left for left, right in zip(times, times[1:])):
        raise PlankInputError("calibration_samples_out_of_order")
    duration = times[-1] - times[0]
    if duration < float(rules["duration_seconds"]) * 0.75:
        raise PlankInputError("calibration_duration_too_short")
    if max(np.diff(times)) > float(config["phase_thresholds"]["gap_reset_seconds"]) * 2:
        raise PlankInputError("calibration_stream_has_a_gap")
    angle_std = {
        "relative_pitch": _circular_std(pitch_values),
        "relative_roll": _circular_std(roll_values),
        "upper_pitch": _circular_std(node_pitch["upper"]),
        "upper_roll": _circular_std(node_roll["upper"]),
        "lower_pitch": _circular_std(node_pitch["lower"]),
        "lower_roll": _circular_std(node_roll["lower"]),
    }
    maximum_std = float(rules["maximum_relative_angle_std_deg"])
    if any(value > maximum_std for value in angle_std.values()):
        raise PlankInputError("calibration_not_stable_hold_correct_plank_and_retry")
    return {
        "baseline_pitch_deg": _circular_mean(pitch_values),
        "baseline_roll_deg": _circular_mean(roll_values),
        "node_baselines": {
            part: {
                "pitch_deg": _circular_mean(node_pitch[part]),
                "roll_deg": _circular_mean(node_roll[part]),
                "pitch_std_deg": round(angle_std[f"{part}_pitch"], 3),
                "roll_std_deg": round(angle_std[f"{part}_roll"], 3),
            }
            for part in ("upper", "lower")
        },
        "pitch_std_deg": round(angle_std["relative_pitch"], 3),
        "roll_std_deg": round(angle_std["relative_roll"], 3),
        "sample_count": len(frames),
        "duration_seconds": round(duration, 3),
        "pair_context": pair_context,
    }


def _new_phase_tracker() -> dict:
    return {
        "history": deque(), "phase": "unknown", "candidate": None,
        "candidate_since": None, "phase_since": None, "last_seen": None,
    }


def create_phase_state(calibration: dict) -> dict:
    return {
        "calibration": calibration,
        "pair_tracker": _new_phase_tracker(),
        "node_trackers": {"upper": _new_phase_tracker(), "lower": _new_phase_tracker()},
        "last_sequences": None,
        "context": calibration["pair_context"],
    }


def _trend(history: deque, now: float, window: float) -> float | None:
    current = [score for stamp, score in history if now - window <= stamp <= now]
    previous = [score for stamp, score in history if now - 2 * window <= stamp < now - window]
    if len(current) < 3 or len(previous) < 3:
        return None
    current_mean = sum(current) / len(current)
    previous_mean = sum(previous) / len(previous)
    return (current_mean - previous_mean) / window


def _set_candidate(state: dict, target: str, now: float, confirmation: float) -> None:
    if target == state["phase"]:
        state["candidate"] = None
        state["candidate_since"] = None
        return
    if state["candidate"] != target:
        state["candidate"] = target
        state["candidate_since"] = now
        return
    if now - float(state["candidate_since"]) >= confirmation:
        state["phase"] = target
        state["phase_since"] = now
        state["candidate"] = None
        state["candidate_since"] = None


def _phase_result(tracker: dict, error_score: float, error_type: str, error_direction: str,
                  delta_pitch: float, delta_roll: float, sagittal_score: float,
                  lateral_score: float, calibration_samples: int, config: dict,
                  now: float) -> dict:
    phase_config = config["phase_thresholds"]
    if tracker["last_seen"] is not None and now - tracker["last_seen"] > float(phase_config["gap_reset_seconds"]):
        tracker["history"].clear()
        tracker["phase"] = "unknown"
        tracker["candidate"] = None
        tracker["candidate_since"] = None
        tracker["phase_since"] = None
    tracker["last_seen"] = now

    history: deque = tracker["history"]
    history.append((now, error_score))
    window = float(phase_config["trend_window_seconds"])
    while history and now - history[0][0] > 2 * window + 0.1:
        history.popleft()
    trend = _trend(history, now, window)
    warmed = len([stamp for stamp, _ in history if now - 2 * window <= stamp <= now]) >= 6
    if not warmed:
        return {
            "phase": "unknown", "error_type": error_type, "error_direction": error_direction,
            "error_score": round(error_score, 4), "trend_per_second": None,
            "delta_pitch_deg": round(delta_pitch, 3), "delta_roll_deg": round(delta_roll, 3),
            "calibrated": True, "calibration_samples": calibration_samples,
            "reason": "warming_up_for_trend_window",
        }

    old_phase = tracker["phase"]
    positive = trend is not None and trend > float(phase_config["trend_deadband_per_second"])
    negative = trend is not None and trend < -float(phase_config["trend_deadband_per_second"])
    if (old_phase == "phase2_incorrect_posture"
            and error_score < float(phase_config["normal_max"]) and negative):
        target, confirm = "phase3_recovery_trend", float(phase_config["recovery_confirm_seconds"])
    elif error_score < float(phase_config["normal_max"]):
        target, confirm = "normal", float(phase_config["normal_confirm_seconds"])
    elif old_phase in {"phase2_incorrect_posture", "phase3_recovery_trend"} and negative:
        target, confirm = "phase3_recovery_trend", float(phase_config["recovery_confirm_seconds"])
    elif error_score >= float(phase_config["incorrect_min"]):
        target, confirm = "phase2_incorrect_posture", float(phase_config["incorrect_confirm_seconds"])
    elif error_score >= float(phase_config["approaching_min"]) and positive:
        target, confirm = "phase1_approaching_error", float(phase_config["approaching_confirm_seconds"])
    elif old_phase in {"phase1_approaching_error", "phase2_incorrect_posture", "phase3_recovery_trend"}:
        # Preserve the current state through the 0.6–0.7 hysteresis band and while
        # the error is stable or falling, until a new transition is clear.
        target, confirm = old_phase, 0.0
    elif old_phase == "unknown":
        target, confirm = "unknown", 0.0
    else:
        target, confirm = "normal", 0.0
    _set_candidate(tracker, target, now, confirm)
    if target == tracker["phase"]:
        tracker["candidate"] = None
        tracker["candidate_since"] = None

    held_ms = 0 if tracker["phase_since"] is None else max(0, int((now - tracker["phase_since"]) * 1000))
    score_config = config["score_thresholds"]
    return {
        "phase": tracker["phase"], "error_type": error_type, "error_direction": error_direction,
        "error_score": round(error_score, 4),
        "trend_per_second": None if trend is None else round(trend, 4),
        "delta_pitch_deg": round(delta_pitch, 3), "delta_roll_deg": round(delta_roll, 3),
        "sagittal_score": round(sagittal_score, 4), "lateral_score": round(lateral_score, 4),
        "held_ms": held_ms, "calibrated": True,
        "calibration_samples": calibration_samples,
        "thresholds_validated": bool(score_config.get("validated", False)),
        "trained_classifier": False,
        "reason": "experimental_rule_based_phase",
    }


def advance_phase(state: dict, frame: Mapping, config: dict | None = None,
                  now: float | None = None) -> dict:
    config = config or load_config()
    now = time_now() if now is None else float(now)
    pair = validate_pair(frame, config)
    context = tuple((pair[part]["sensor_uid"], pair[part]["node_id"], pair[part]["mount_site"])
                    for part in ("upper", "lower"))
    if context != state["context"]:
        raise PlankInputError("sensor_pair_changed_recalibrate")
    sequences = (pair["upper"]["seq"], pair["lower"]["seq"])
    if state["last_sequences"] is not None:
        for current, previous in zip(sequences, state["last_sequences"]):
            advance = (current - previous) & 0xFFFF
            if advance == 0:
                raise PlankInputError("duplicate_sensor_pair_frame")
            if advance >= 0x8000:
                for tracker in (state["pair_tracker"], *state["node_trackers"].values()):
                    tracker["history"].clear()
                    tracker["phase"] = "unknown"
                    tracker["candidate"] = None
                raise PlankInputError("sensor_sequence_reset_recalibrate")
    state["last_sequences"] = sequences

    measured_pitch, measured_roll = relative_angles(pair)
    delta_pitch = wrap_degrees(measured_pitch - state["calibration"]["baseline_pitch_deg"])
    delta_roll = wrap_degrees(measured_roll - state["calibration"]["baseline_roll_deg"])
    score_config = config["score_thresholds"]
    sagittal_score = abs(delta_pitch) / float(score_config["sagittal_deg"])
    lateral_score = abs(delta_roll) / float(score_config["lateral_deg"])
    error_score = max(sagittal_score, lateral_score)
    if sagittal_score >= lateral_score:
        error_type = "sagittal_angle_deviation"
        error_direction = "positive_pitch" if delta_pitch >= 0 else "negative_pitch"
    else:
        error_type = "roll_angle_deviation"
        error_direction = "positive_roll" if delta_roll >= 0 else "negative_roll"
    calibration = state["calibration"]
    sample_count = calibration["sample_count"]
    pair_result = _phase_result(state["pair_tracker"], error_score, error_type,
                                error_direction, delta_pitch, delta_roll,
                                sagittal_score, lateral_score, sample_count, config, now)

    node_results = {}
    for part in ("upper", "lower"):
        node = pair[part]
        baseline = calibration["node_baselines"][part]
        node_delta_pitch = wrap_degrees(node["pitch"] - baseline["pitch_deg"])
        node_delta_roll = wrap_degrees(node["roll"] - baseline["roll_deg"])
        node_sagittal = abs(node_delta_pitch) / float(score_config["sagittal_deg"])
        node_lateral = abs(node_delta_roll) / float(score_config["lateral_deg"])
        node_score = max(node_sagittal, node_lateral)
        if node_sagittal >= node_lateral:
            node_type = "local_pitch_deviation"
            node_direction = "positive_pitch" if node_delta_pitch >= 0 else "negative_pitch"
        else:
            node_type = "local_roll_deviation"
            node_direction = "positive_roll" if node_delta_roll >= 0 else "negative_roll"
        result = _phase_result(state["node_trackers"][part], node_score,
                               node_type, node_direction, node_delta_pitch,
                               node_delta_roll, node_sagittal, node_lateral,
                               sample_count, config, now)
        result.update({"node_id": node["node_id"], "sensor_uid": node["sensor_uid"],
                       "mount_site": node["mount_site"], "body_part": node["body_part"]})
        node_results[part] = result

    pair_result["pair"] = {
        "phase": pair_result["phase"], "error_type": pair_result["error_type"],
        "error_direction": pair_result["error_direction"],
        "error_score": pair_result["error_score"],
        "trend_per_second": pair_result["trend_per_second"],
    }
    pair_result["nodes"] = node_results
    return pair_result


def time_now() -> float:
    import time
    return time.monotonic()


def capabilities(config: dict | None = None) -> dict:
    config = config or load_config()
    return {
        "ready": True,
        "model_version": config["model_version"],
        "trained_classifier": False,
        "sensor_pair": config["sensor_pair"],
        "calibration": config["calibration"],
        "score_thresholds": config["score_thresholds"],
        "phase_thresholds": config["phase_thresholds"],
        "capabilities": config["capabilities"],
    }
```

### 6.37 ไฟล์ `tools/prepare_plank_5stage_dataset.py` — สคริปต์เตรียมชุดข้อมูลแพลงก์ 5 ขั้นตอนจาก Guided Timer (207 บรรทัด)
```python
"""Derive a five-stage, single-node plank phase dataset from guided-timer CSVs.

Sensor values are copied from the input recordings. The script only adds a
five-stage sequence label and a mapping to the current four-state phase model;
it does not synthesize IMU measurements or mark cue labels as reviewed.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
from collections import Counter
from pathlib import Path


EXPECTED_RUNS = ["neutral", "approaching", "holding", "returning", "neutral"]
STAGES = [
    (1, "normal_start", "normal"),
    (2, "approaching_error", "phase1_approaching_error"),
    (3, "incorrect_posture_hold", "phase2_incorrect_posture"),
    (4, "recovery_to_normal", "phase3_recovery_trend"),
    (5, "normal_end", "normal"),
]
STAGE_FOR_RUN = {
    "neutral": (1, "normal_start", "normal"),
    "approaching": (2, "approaching_error", "phase1_approaching_error"),
    "holding": (3, "incorrect_posture_hold", "phase2_incorrect_posture"),
    "returning": (4, "recovery_to_normal", "phase3_recovery_trend"),
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_recording(path: Path) -> tuple[list[str], list[dict[str, str]]]:
    with path.open("r", newline="", encoding="utf-8-sig") as stream:
        reader = csv.DictReader(stream)
        if not reader.fieldnames:
            raise ValueError(f"{path}: CSV is missing a header")
        rows = list(reader)
    if not rows:
        raise ValueError(f"{path}: CSV has no samples")
    required = {"phase_label", "mount_site", "action_label"}
    missing = required - set(reader.fieldnames)
    if missing:
        raise ValueError(f"{path}: missing required columns {sorted(missing)}")
    return list(reader.fieldnames), rows


def phase_runs(rows: list[dict[str, str]]) -> list[tuple[str, int, int]]:
    runs: list[tuple[str, int, int]] = []
    start = 0
    for index in range(1, len(rows) + 1):
        if index == len(rows) or rows[index].get("phase_label", "").strip() != rows[start].get("phase_label", "").strip():
            label = rows[start].get("phase_label", "").strip()
            runs.append((label, start, index))
            start = index
    return runs


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="+", type=Path, help="Single-node guided-timer CSV recordings")
    parser.add_argument(
        "--output-dir", type=Path,
        default=Path(__file__).resolve().parents[1] / "datasets" / "plank_phase_5stage_single_node",
    )
    args = parser.parse_args()

    recordings: list[tuple[Path, list[str], list[dict[str, str]], str]] = []
    seen_hashes: set[str] = set()
    for path in args.inputs:
        path = path.expanduser().resolve()
        if not path.is_file():
            parser.error(f"Input file does not exist: {path}")
        digest = sha256(path)
        if digest in seen_hashes:
            print(f"Skip duplicate file: {path.name}")
            continue
        seen_hashes.add(digest)
        header, rows = read_recording(path)
        sites = {row.get("mount_site", "").strip().lower() for row in rows}
        if sites != {"lumbar"}:
            raise ValueError(f"{path}: expected one lumbar node; found mount sites {sorted(sites)}")
        run_labels = [label for label, _, _ in phase_runs(rows)]
        if run_labels != EXPECTED_RUNS:
            raise ValueError(
                f"{path}: expected phase order {EXPECTED_RUNS}, found {run_labels}"
            )
        recordings.append((path, header, rows, digest))

    if not recordings:
        parser.error("No unique input recordings were provided")

    extra_columns = [
        "source_recording", "source_trial_id", "derived_trial_id",
        "phase_stage_id", "phase_stage_label", "model_phase_label",
        "sensor_data_origin", "phase_label_status",
    ]
    fieldnames = list(dict.fromkeys([*recordings[0][1], *extra_columns]))
    all_rows: list[dict[str, str]] = []
    stage_counts: Counter[str] = Counter()
    phase_counts: Counter[str] = Counter()
    source_manifest = []
    session_ids: set[str] = set()
    subjects: set[str] = set()

    for path, header, rows, digest in recordings:
        if header != recordings[0][1]:
            raise ValueError(f"{path}: source CSV headers differ; cannot combine recordings safely")
        session_ids.add(rows[0].get("session_id", ""))
        subjects.add(rows[0].get("subject_id", ""))
        runs = phase_runs(rows)
        run_stage = {
            0: STAGES[0], 1: STAGES[1], 2: STAGES[2], 3: STAGES[3], 4: STAGES[4]
        }
        for run_index, (run_label, start, stop) in enumerate(runs):
            stage_id, stage_label, model_label = run_stage[run_index]
            for row in rows[start:stop]:
                output_row = dict(row)
                output_row["source_recording"] = path.name
                output_row["source_trial_id"] = row.get("trial_id", "")
                output_row["derived_trial_id"] = path.stem
                # Unique trial/epoch values prevent windows crossing file boundaries.
                output_row["trial_id"] = path.stem
                output_row["stream_epoch"] = f"{row.get('stream_epoch', '')}:{path.stem}"
                output_row["phase_stage_id"] = str(stage_id)
                output_row["phase_stage_label"] = stage_label
                output_row["model_phase_label"] = model_label
                output_row["sensor_data_origin"] = "captured_example"
                output_row["phase_label_status"] = "guided_timer_unreviewed"
                all_rows.append(output_row)
                stage_counts[stage_label] += 1
                phase_counts[run_label] += 1
        source_manifest.append({
            "file": path.name,
            "sha256": digest,
            "rows": len(rows),
            "session_id": rows[0].get("session_id", ""),
            "subject_id": rows[0].get("subject_id", ""),
            "action_label": rows[0].get("action_label", ""),
            "phase_runs": [
                {"source_label": label, "stage_label": STAGES[index][1],
                 "start_row_zero_based": start, "end_row_exclusive": stop}
                for index, (label, start, stop) in enumerate(runs)
            ],
        })

    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    csv_path = output_dir / "plank_phase_5stage_single_node.csv"
    with csv_path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(all_rows)

    manifest = {
        "dataset_name": "plank_phase_5stage_single_node",
        "schema_version": 1,
        "sensor_data": "copied from captured lumbar-node CSV examples; no IMU values synthesized",
        "sensor_count": 1,
        "mount_site": "lumbar",
        "stage_order": [
            {"stage_id": stage_id, "stage_label": stage_label,
             "model_phase_label": model_label}
            for stage_id, stage_label, model_label in STAGES
        ],
        "rows": len(all_rows),
        "recording_count": len(recordings),
        "subjects": sorted(value for value in subjects if value),
        "session_ids": sorted(value for value in session_ids if value),
        "phase_stage_counts": dict(stage_counts),
        "source_phase_label_counts": dict(phase_counts),
        "labels_reviewed": False,
        "limitations": [
            "phase_stage_label is derived from guided-timer boundaries; inspect video and review labels before treating it as ground truth",
            "initial and final normal stages map to the same model state because the posture is the same; sequence context distinguishes them",
            "one lumbar sensor cannot evaluate the current paired upper/lower Plank Phase Pilot",
            "this dataset alone does not establish model accuracy or generalization to new people",
        ],
        "sources": source_manifest,
    }
    (output_dir / "manifest.json").write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    print(json.dumps({
        "csv": str(csv_path),
        "manifest": str(output_dir / "manifest.json"),
        "recordings": len(recordings),
        "rows": len(all_rows),
        "phase_stage_counts": dict(stage_counts),
        "session_count": len(session_ids),
        "labels_reviewed": False,
    }, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

### 6.38 ไฟล์ `tools/evaluate_plank_predictions.py` — สคริปต์ประเมินผลการทำนายช่วงจังหวะแพลงก์ (107 บรรทัด)
```python
"""Score manually labeled live Plank Phase Pilot prediction CSV files."""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path

from sklearn.metrics import (accuracy_score, balanced_accuracy_score, confusion_matrix,
                             f1_score, precision_recall_fscore_support)

PHASES = [
    "normal",
    "phase1_approaching_error",
    "phase2_incorrect_posture",
    "phase3_recovery_trend",
]
PREDICTIONS = PHASES + ["unknown"]
TARGETS = {
    "pair": ("truth_pair", "pred_pair"),
    "upper_node": ("truth_upper", "pred_upper"),
    "lower_node": ("truth_lower", "pred_lower"),
}


def evaluate(rows: list[dict], truth_key: str, prediction_key: str) -> dict:
    truth, prediction = [], []
    for row in rows:
        expected = str(row.get(truth_key) or "").strip().lower()
        if expected not in PHASES:
            continue
        observed = str(row.get(prediction_key) or "unknown").strip().lower()
        if observed not in PHASES:
            observed = "unknown"
        truth.append(expected)
        prediction.append(observed)
    if not truth:
        return {"available": False, "reason": f"No labeled rows in {truth_key}", "sample_count": 0}

    precision, recall, f1, support = precision_recall_fscore_support(
        truth, prediction, labels=PHASES, zero_division=0
    )
    known_prediction_count = sum(value != "unknown" for value in prediction)
    return {
        "available": True,
        "sample_count": len(truth),
        "unknown_prediction_count": len(truth) - known_prediction_count,
        "coverage_percent": round(100.0 * known_prediction_count / len(truth), 2),
        # Unknown predictions remain in the denominator and count as incorrect.
        "accuracy_percent": round(100.0 * accuracy_score(truth, prediction), 2),
        "balanced_accuracy_percent": round(100.0 * balanced_accuracy_score(truth, prediction), 2),
        "macro_f1_percent": round(100.0 * f1_score(
            truth, prediction, labels=PHASES, average="macro", zero_division=0
        ), 2),
        "per_phase": {
            label: {
                "precision_percent": round(100.0 * float(precision[index]), 2),
                "recall_percent": round(100.0 * float(recall[index]), 2),
                "f1_percent": round(100.0 * float(f1[index]), 2),
                "support": int(support[index]),
            }
            for index, label in enumerate(PHASES)
        },
        "confusion_matrix": {
            "labels": PHASES + ["unknown"],
            "rows_true_columns_predicted": confusion_matrix(
                truth, prediction, labels=PREDICTIONS
            ).tolist(),
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="+", type=Path,
                        help="CSV files exported by the Plank Pilot labeled capture")
    parser.add_argument("--output", type=Path,
                        help="Optional JSON report path; the report is also printed")
    args = parser.parse_args()
    rows = []
    for path in args.inputs:
        with path.open("r", newline="", encoding="utf-8-sig") as stream:
            rows.extend(csv.DictReader(stream))
    report = {
        "evaluation": "manual_ground_truth_vs_live_rule_based_predictions",
        "prediction_model": "planko-6axis-phase-pilot-v1",
        "files": [str(path) for path in args.inputs],
        "row_count": len(rows),
        "metrics": {
            name: evaluate(rows, *columns)
            for name, columns in TARGETS.items()
        },
        "note": ("E and V are not accuracy. Use only held-out labeled trials for a final score; "
                 "do not tune thresholds on the same trials reported here."),
    }
    rendered = json.dumps(report, indent=2, ensure_ascii=False)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered + "\n", encoding="utf-8")
    print(rendered)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

### 6.39 ไฟล์ `tools/train_model.py` — สคริปต์เทรนและประเมินผลโมเดล Machine Learning (SVM-RBF & Random Forest) (438 บรรทัด)
```python
"""Train and evaluate site-aware IMU action and phase classifiers."""

from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path

import joblib
import numpy as np
from sklearn.ensemble import RandomForestClassifier
from sklearn.metrics import (classification_report, confusion_matrix, f1_score,
                             precision_recall_fscore_support)
from sklearn.model_selection import StratifiedGroupKFold
from sklearn.pipeline import make_pipeline
from sklearn.preprocessing import StandardScaler
from sklearn.svm import SVC

from imu_features import FEATURE_VERSION, feature_names, samples_to_windows

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CONFIG = ROOT / "config" / "pose_labels.json"
DEFAULT_DATASET_DIR = ROOT / "dataset" / "multinode"


def load_config(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def label_index(config: dict):
    return {action["id"]: body for body, value in config["body_parts"].items()
            for action in value.get("actions", [])}


def case_label_index(config: dict) -> set[str]:
    """Labels shared by all sensors in one case, independent of body part."""
    return {entry["id"] for entry in config.get("case_labels", [])}


def legacy_map(config: dict):
    return {filename.lower(): action["id"]
            for body in config["body_parts"].values()
            for action in body.get("actions", [])
            for filename in action.get("legacy_files", [])}


def read_recording(path: Path, config: dict) -> list[dict]:
    with path.open("r", newline="", encoding="utf-8-sig") as stream:
        source = list(csv.DictReader(stream))
    if not source:
        return []
    action_bodies = label_index(config)
    action = legacy_map(config).get(path.name.lower())
    rows = []
    epoch = 0
    previous_seq = None
    previous_boot = None
    for index, raw in enumerate(source):
        sensor_uid = raw.get("sensor_uid") or raw.get("firmware_sensor_id") or raw.get("sensor_id") or "unknown"
        site = raw.get("mount_site") or raw.get("recorded_site") or raw.get("sensor_id") or "other"
        site = str(site).strip().lower()
        body_part = raw.get("body_part") or (action_bodies.get(action) if action else None) or "other"
        action_label = raw.get("action_label") or raw.get("pose_label") or action or ""
        body_part = str(body_part).strip().lower()
        action_label = str(action_label).strip()
        if site not in config["mount_sites"]:
            raise ValueError(f"{path} row {index + 2}: unknown mount_site {site!r}")
        if action_label in action_bodies and action_bodies[action_label] != body_part:
            raise ValueError(f"{path} row {index + 2}: action {action_label!r} belongs to "
                             f"{action_bodies[action_label]!r}, not body_part {body_part!r}")
        phase_label = raw.get("phase_label") or ""
        reviewed_value = str(raw.get("phase_annotation_reviewed") or "").strip().lower()
        has_review_metadata = "phase_annotation_reviewed" in raw and reviewed_value != ""
        phase_annotation_reviewed = (reviewed_value in ("true", "1", "yes")
                                     if has_review_metadata else None)
        # New recorder cue times are provisional until the annotator reviews
        # their boundaries. Older CSVs have no review column and retain their
        # previous explicit-label behavior.
        if has_review_metadata and not phase_annotation_reviewed:
            phase_label = ""
        try:
            calibration_method_version = int(raw.get("calibration_method_version") or 0)
            calibration_time_boot_ms = int(raw.get("calibration_time_boot_ms") or 0)
        except ValueError:
            calibration_method_version = calibration_time_boot_ms = 0
        try:
            boot_ms = int(float(raw.get("boot_ms") or raw.get("sample_ms") or 0))
            seq = int(float(raw["seq"])) if raw.get("seq") not in (None, "") else index
            host_time = float(raw.get("host_time_s") or raw.get("host_timestamp_s") or index / config["sampling_hz"])
        except (TypeError, ValueError):
            continue
        if previous_seq is not None and (seq < previous_seq or boot_ms < previous_boot):
            epoch += 1
        previous_seq, previous_boot = seq, boot_ms
        # Keep arrival order; a reboot begins a new stream epoch and cannot be
        # accidentally sorted back together by the reset boot-time clock.
        has_device_clock = any(raw.get(key) not in (None, "") for key in ("boot_ms", "sample_ms"))
        device_time = boot_ms / 1000 if has_device_clock else host_time
        row = {
            "sensor_uid": sensor_uid,
            "mount_site": site,
            "body_part": body_part,
            "side": raw.get("side", ""),
            "subject_id": raw.get("subject_id", ""),
            "session_id": raw.get("session_id") or raw.get("recording_id") or path.stem,
            "trial_id": raw.get("trial_id") or raw.get("recording_id") or path.stem,
            "stream_epoch": f"{path.stem}:{raw.get('stream_epoch', '')}:{epoch}",
            "calibration_id": raw.get("calibration_id") or "",
            "calibration_method_version": calibration_method_version,
            "calibration_time_boot_ms": calibration_time_boot_ms,
            "calibration_boot_id": raw.get("calibration_boot_id") or "",
            "calibrated": str(raw.get("calibrated", "false")).lower() in ("true", "1", "yes"),
            "relative_angle_valid": str(raw.get("relative_angle_valid", "false")).lower() in ("true", "1", "yes"),
            "relative_orientation_valid": str(raw.get("relative_orientation_valid", raw.get("calibrated", "false"))).lower() in ("true", "1", "yes"),
            "action_label": action_label,
            "phase_label": str(phase_label).strip(),
            "phase_annotation_source": raw.get("phase_annotation_source") or "legacy_explicit",
            "phase_annotation_reviewed": phase_annotation_reviewed,
            "boot_ms": boot_ms,
            "seq": seq,
            "device_time_s": device_time,
            "host_time_s": host_time,
            "valid": str(raw.get("valid", "true")).lower() in ("true", "1", "yes"),
            "gap_before": str(raw.get("gap_before", "false")).lower() in ("true", "1", "yes"),
        }
        for axis in "xyz":
            # CSV channels use SI units: m/s^2 and rad/s, matching MQTT v2.
            row[f"accel_{axis}"] = raw.get(f"accel_{axis}", "")
            row[f"gyro_{axis}"] = raw.get(f"gyro_{axis}", "")
        for key in ("relative_roll_deg", "relative_pitch_deg"):
            row[key] = raw.get(key, "")
        if row["relative_angle_valid"] and not row["relative_pitch_deg"]:
            row["relative_pitch_deg"] = raw.get("relative_tilt_deg", "")
        if row["relative_angle_valid"] and not row["relative_roll_deg"]:
            row["relative_roll_deg"] = "0"
        rows.append(row)
    return rows


def load_windows(inputs: list[Path], config: dict, seconds: float):
    result = []
    ignored_files = []
    for path in inputs:
        rows = read_recording(path, config)
        allowed = set(label_index(config)) | case_label_index(config)
        known = [row for row in rows if row["action_label"] in allowed]
        if not known:
            ignored_files.append(f"{path}: no action label or configured legacy filename mapping")
            continue
        use_angles = any(row["calibrated"] and row["relative_orientation_valid"]
                         and row["calibration_method_version"] >= 1
                         and row["calibration_id"] for row in known)
        # Preserve every sample as a boundary marker. Filtering uncalibrated or
        # unlabeled rows out first could stitch samples on either side together.
        rows = [{**row, "action_label": row["action_label"] if row["action_label"] in allowed else ""}
                for row in rows]
        if use_angles:
            rows = [{**row, "valid": row["valid"] and row["calibrated"]
                     and row["relative_orientation_valid"] and bool(row["calibration_id"])
                     and row["calibration_method_version"] >= 1}
                    for row in rows]
        if not use_angles:
            # Explicitly raw-only: never use uncalibrated relative angle zeroes.
            pass
        result.extend(samples_to_windows(rows, seconds, float(config["sampling_hz"]),
                                         float(config["prediction"]["gap_reset_seconds"]), use_angles))
    return result, ignored_files


def make_estimator(name: str, seed: int):
    if name == "random_forest":
        return RandomForestClassifier(n_estimators=300, max_depth=10, min_samples_leaf=2,
                                      class_weight="balanced_subsample", random_state=seed,
                                      n_jobs=1)
    return make_pipeline(StandardScaler(), SVC(C=2.0, kernel="rbf", class_weight="balanced",
                                               probability=True, random_state=seed))


def split_groups(windows: list[dict], target: str):
    labels = np.asarray([row[target] for row in windows])
    subjects = {row["subject_id"] for row in windows if row["subject_id"]}
    if len(subjects) >= 2 and all(any(row["subject_id"] == subject for row in windows) for subject in subjects):
        groups = np.asarray([row["subject_id"] or row["trial_id"] for row in windows])
        group_level = "subject_id"
    else:
        if any(row["session_id"] for row in windows):
            groups = np.asarray([row["session_id"] or row["trial_id"] for row in windows])
            group_level = "session_id"
        else:
            groups = np.asarray([row["trial_id"] for row in windows])
            group_level = "trial_id"
    unique_groups = len(set(groups))
    if unique_groups < 2 or len(set(labels)) < 2:
        return [], group_level, f"Need at least two groups and two classes; found {unique_groups} groups and {len(set(labels))} classes"
    folds = min(5, unique_groups)
    splitter = StratifiedGroupKFold(n_splits=folds, shuffle=True, random_state=42)
    valid = []
    for train, test in splitter.split(np.zeros(len(labels)), labels, groups):
        if len(set(labels[train])) < 2 or not len(test):
            continue
        valid.append((train, test))
    if not valid:
        return [], group_level, "No group-separated folds have enough classes to evaluate"
    return valid, group_level, ""


def evaluate_model(windows: list[dict], target: str, feature_cols: list[str],
                   name: str, seed: int = 42):
    folds, group_level, reason = split_groups(windows, target)
    labels = sorted({row[target] for row in windows})
    if not folds:
        return {"available": False, "reason": reason, "grouped_by": group_level,
                "labels": labels, "window_count": len(windows)}
    all_true, all_pred = [], []
    missing_test_labels = set()
    for train, test in folds:
        y = np.asarray([windows[index][target] for index in train])
        test_y = np.asarray([windows[index][target] for index in test])
        absent = set(labels) - set(y)
        if absent:
            missing_test_labels.update(absent)
            continue
        estimator = make_estimator(name, seed)
        x_train = np.asarray([[windows[index]["features"].get(col, 0.0) for col in feature_cols]
                              for index in train], dtype=float)
        x_test = np.asarray([[windows[index]["features"].get(col, 0.0) for col in feature_cols]
                             for index in test], dtype=float)
        estimator.fit(x_train, y)
        all_true.extend(test_y.tolist())
        all_pred.extend(estimator.predict(x_test).tolist())
    if not all_true:
        return {"available": False,
                "reason": "No group-separated fold retained every target class in training; collect more independent trials per class",
                "grouped_by": group_level, "labels": labels, "window_count": len(windows),
                "classes_missing_from_train_folds": sorted(missing_test_labels)}
    precision, recall, f1, support = precision_recall_fscore_support(
        all_true, all_pred, labels=labels, zero_division=0)
    return {
        "available": True,
        "grouped_by": group_level,
        "fold_count": len(folds),
        "window_count": len(windows),
        "accuracy": float(np.mean(np.asarray(all_true) == np.asarray(all_pred))),
        "macro_f1": float(f1_score(all_true, all_pred, labels=labels, average="macro", zero_division=0)),
        "per_class": {label: {"precision": float(precision[i]), "recall": float(recall[i]),
                              "f1": float(f1[i]), "support": int(support[i])}
                      for i, label in enumerate(labels)},
        "confusion_matrix": confusion_matrix(all_true, all_pred, labels=labels).tolist(),
        "labels": labels,
    }


def feature_units(columns: list[str]) -> dict[str, str]:
    units = {}
    for name in columns:
        base = name.split("__at_", 1)[0]
        if base.startswith("accel_"):
            units[name] = "m/s^3" if base.endswith("_slope_per_s") else "m/s^2"
        elif base.startswith("gyro_"):
            units[name] = "rad/s^2" if base.endswith("_slope_per_s") else "rad/s"
        elif base.startswith("relative_roll_deg_") or base.startswith("relative_pitch_deg_"):
            units[name] = "deg/s" if base.endswith("_slope_per_s") else "deg"
        else:
            units[name] = "unitless"
    return units


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="*", type=Path,
                        help=f"Recording CSV files; defaults to {DEFAULT_DATASET_DIR}")
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    parser.add_argument("--output", type=Path, default=ROOT / "models" / "pose_model")
    parser.add_argument("--window", type=float, default=1.0)
    parser.add_argument("--seed", type=int, default=42)
    args = parser.parse_args()
    config = load_config(args.config)
    inputs = args.inputs or sorted(DEFAULT_DATASET_DIR.glob("*.csv"))
    inputs = [path.resolve() for path in inputs if path.exists()]
    if not inputs:
        parser.error(f"No input CSV files found; default dataset directory is {DEFAULT_DATASET_DIR}")
    windows, ignored = load_windows(inputs, config, args.window)
    action_windows = [row for row in windows if row["action_label"]]
    if not action_windows:
        print("No trainable labeled windows. Add action_label rows or configured legacy file mappings.", file=sys.stderr)
        return 2

    # Raw-only old captures cannot supply calibrated orientation features.
    use_angles = any(float(row["features"].get("relative_orientation_valid", 0)) == 1 for row in action_windows)
    sites = {row["mount_site"] for row in action_windows}
    bodies = {row["body_part"] for row in action_windows}
    sides = {row["side"] for row in action_windows}
    columns = feature_names(sites, use_angles, bodies, sides)
    stage_windows = [row for row in action_windows if row["phase_label"] in
                     {"neutral", "approaching", "holding", "returning"}]

    if use_angles:
        # Do not mix calibrated and raw-only windows under an angle-enabled schema.
        action_windows = [row for row in action_windows
                          if float(row["features"].get("relative_orientation_valid", 0.0)) == 1.0]
        stage_windows = [row for row in stage_windows
                         if float(row["features"].get("relative_orientation_valid", 0.0)) == 1.0]
    action_labels = sorted({row["action_label"] for row in action_windows})
    stage_labels = sorted({row["phase_label"] for row in stage_windows})
    if not action_windows:
        print("No eligible action windows after calibration and boundary filtering.", file=sys.stderr)
        return 2

    if len(action_labels) < 2:
        print(f"❌ ไม่สามารถเทรนโมเดลจำแนกท่าได้: พบท่าทาง (action_label) เพียง {len(action_labels)} ท่า คือ {action_labels}", file=sys.stderr)
        print("💡 การเทรนโมเดล AI (Classification) จำเป็นต้องมีอย่างน้อย 2 ท่าขึ้นไป เพื่อให้ AI เปรียบเทียบและแยกแยะได้", file=sys.stderr)
        print("   เช่น ต้องมี case_neutral (ท่าปกติ) และ case_flexion (ก้ม/งอ) จาก Node ที่ใช้ร่วมกัน", file=sys.stderr)
        print(f"   (จำนวนหน้าต่างเวลาที่สกัดได้จากไฟล์นี้: {len(action_windows)} windows, จังหวะ phase: {stage_labels})", file=sys.stderr)
        return 2

    evaluations = {}
    for target, eligible in (("action_label", action_windows), ("phase_label", stage_windows)):
        if target == "phase_label" and len(stage_labels) < 2:
            evaluations[target] = {"available": False,
                                    "reason": "Fewer than two explicitly annotated phase classes; phase output will remain unknown",
                                    "labels": stage_labels, "window_count": len(stage_windows)}
            continue
        evaluations[target] = {
            model_name: evaluate_model(eligible, target, columns, model_name, args.seed)
            for model_name in ("random_forest", "svm_rbf")
        }

    def select_model(target):
        comparisons = evaluations.get(target, {})
        available = [(name, metrics) for name, metrics in comparisons.items()
                     if isinstance(metrics, dict) and metrics.get("available")]
        if not available:
            return "random_forest", "grouped evaluation unavailable; RF retained as prototype"
        best_score = max(metrics["macro_f1"] for _, metrics in available)
        # RF wins exact ties so the initial prototype remains the simple default.
        best = next((name for name, metrics in available
                     if metrics["macro_f1"] == best_score and name == "random_forest"), None)
        if best is None:
            best = next(name for name, metrics in available if metrics["macro_f1"] == best_score)
        return best, "selected by group-separated macro-F1; RF preferred on ties"

    selected_action_model, action_selection_reason = select_model("action_label")
    selected_phase_model, phase_selection_reason = select_model("phase_label")
    phase_evaluation_available = any(
        metrics.get("available") for metrics in evaluations.get("phase_label", {}).values()
        if isinstance(metrics, dict)
    )

    # Fit selected estimators on all eligible data after grouped comparison.
    action_model = make_estimator(selected_action_model, args.seed)
    action_x = np.asarray([[row["features"].get(col, 0.0) for col in columns]
                           for row in action_windows], dtype=float)
    action_y = [row["action_label"] for row in action_windows]
    action_model.fit(action_x, action_y)
    phase_model = None
    phase_estimator_name = None
    if len(stage_labels) >= 2 and phase_evaluation_available:
        phase_estimator_name = selected_phase_model
        phase_model = make_estimator(phase_estimator_name, args.seed)
        phase_x = np.asarray([[row["features"].get(col, 0.0) for col in columns]
                              for row in stage_windows], dtype=float)
        phase_model.fit(phase_x, [row["phase_label"] for row in stage_windows])

    calibrated_angles = use_angles
    calibration_versions = sorted({row["calibration_method_version"] for row in action_windows
                                    if row["calibrated"] and row["calibration_method_version"] > 0})
    bundle = {
        "bundle_version": 1,
        "feature_version": FEATURE_VERSION,
        "action_model": action_model,
        "phase_model": phase_model,
        "selected_action_estimator": selected_action_model,
        "selected_phase_estimator": phase_estimator_name,
        "model_selection": {"action": action_selection_reason,
                            "phase": phase_selection_reason},
        "feature_columns": columns,
        "feature_units": feature_units(columns),
        "action_labels": action_labels,
        "phase_labels": stage_labels if phase_model else [],
        "phase_model_available": phase_model is not None,
        "phase_annotation_policy": (
            "New recorder CSV phases require phase_annotation_reviewed=true; "
            "legacy CSVs without that column keep explicit phase labels"
        ),
        "mount_sites": sorted(sites),
        "body_parts": sorted(bodies),
        "sides": sorted(sides),
        "supported_pairs": sorted({(row["body_part"], row["mount_site"]) for row in action_windows}),
        "supported_contexts": sorted({(row["body_part"], row["mount_site"], row["side"])
                                       for row in action_windows}),
        "window_seconds": args.window,
        "sampling_hz": float(config["sampling_hz"]),
        "requires_calibration": calibrated_angles,
        "calibration_requirements": {
            "required": calibrated_angles,
            "orientation_field": "relative_roll_deg, relative_pitch_deg",
            "requires_calibration_id": calibrated_angles,
            "method_versions": calibration_versions,
        },
        "units": {"accel": "m/s^2", "gyro": "rad/s", "orientation": "degrees"},
        "score_kind": "uncalibrated estimator score; not a guaranteed probability",
        "model_version": f"pose-{selected_action_model.replace('_', '-')}-v1",
        "config": config,
        "evaluation": evaluations,
        "phase_unavailable_reason": None if phase_model else (
            evaluations.get("phase_label", {}).get("reason") or
            "No independent group-separated phase evaluation; stage will remain unknown"),
    }
    args.output.mkdir(parents=True, exist_ok=True)
    joblib.dump(bundle, args.output / "model_bundle.joblib")
    report = {
        "model_version": bundle["model_version"],
        "window_seconds": args.window,
        "sampling_hz": config["sampling_hz"],
        "action_windows": len(action_windows),
        "phase_windows": len(stage_windows),
        "action_labels": action_labels,
        "phase_labels": stage_labels,
        "phase_model_available": phase_model is not None,
        "phase_annotation_policy": bundle["phase_annotation_policy"],
        "requires_calibration": calibrated_angles,
        "selected_action_estimator": selected_action_model,
        "selected_phase_estimator": phase_estimator_name,
        "model_selection": bundle["model_selection"],
        "evaluation": evaluations,
        "ignored_files": ignored,
        "limitation": "Scores are not calibrated probabilities. Independent evaluation is unavailable wherever group-separated folds cannot cover all classes.",
    }
    (args.output / "evaluation.json").write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

### 6.40 ไฟล์ `tools/serve_model.py` — เซิร์ฟเวอร์ HTTP/MQTT สำหรับรันโมเดล AI ทำนายท่าทางแบบเรียลไทม์ (Port 8765) (605 บรรทัด)
```python
"""Local web/API server for CSV replay and ESP32 live model inspection."""

from __future__ import annotations

import argparse
import ipaddress
import json
import sys
import time
import urllib.error
import urllib.request
from urllib.parse import urlencode, urlsplit
from collections import deque
from pathlib import Path
from threading import Lock

import joblib
import numpy as np
from flask import Flask, jsonify, request, send_from_directory

from imu_features import FEATURE_VERSION, StreamBuffer, feature_names
from plank_phase import (PlankInputError, advance_phase, capabilities as plank_capabilities,
                         create_phase_state, load_config as load_plank_config,
                         make_calibration)

ROOT = Path(__file__).resolve().parents[1]
app = Flask(__name__)
bundle = None
states = {}
buffers = {}
live_cursors = {}
lock = Lock()
plank_lock = Lock()
plank_states = {}
plank_config = load_plank_config()


@app.after_request
def allow_local_monitor(response):
    # Support the existing file:// or localhost monitor without opening the API
    # to arbitrary remote website origins. Same-origin /monitor needs no CORS.
    origin = request.headers.get("Origin", "")
    if request.path.startswith("/api/") and (origin == "null" or
            urlsplit(origin).hostname in {"localhost", "127.0.0.1", "::1"}):
        response.headers["Access-Control-Allow-Origin"] = origin
        response.headers["Access-Control-Allow-Headers"] = "Content-Type"
        response.headers["Access-Control-Allow-Methods"] = "GET, POST, OPTIONS"
        response.headers["Vary"] = "Origin"
    return response


def reset_stream(key):
    state = states.setdefault(key, {})
    state.clear()
    if key in buffers:
        buffers[key].reset()


def reset_matching(site: str, body_part: str, side: str):
    suffix = f"|{site}|{body_part}|{side}"
    with lock:
        for key in list(buffers):
            parts = key.split("|")
            same_stream_context = (len(parts) >= 4 and parts[-3] == site and parts[-2] == body_part
                                   and (side == "unknown" or parts[-1] == side))
            if key.endswith(suffix) or same_stream_context:
                buffers[key].reset()
                states.pop(key, None)


def unknown(bundle_data, body_part="", site="", calibrated=False, reason="warming_up"):
    return {
        "body_part": body_part or "unknown", "action": "unknown", "stage": "unknown",
        "score": None, "score_kind": bundle_data.get("score_kind", "unavailable"),
        "held_ms": 0, "calibrated": bool(calibrated),
        "mount_site": site or "unknown", "model_version": bundle_data.get("model_version", "unavailable"),
        "reason": reason,
    }


def _class_probabilities(model, x):
    if model is None:
        return {}
    values = model.predict_proba(x)[0]
    return {str(label): float(probability) for label, probability in zip(model.classes_, values)}


def predict_from_features(features: dict, site: str, body_part: str,
                          calibrated: bool, stream_key: str | None = None,
                          device_time_s: float | None = None, side: str = "unknown"):
    item = bundle
    if item is None:
        return unknown({}, body_part, site, calibrated, "model_not_loaded")
    if site not in item["mount_sites"] or body_part not in item["body_parts"]:
        return unknown(item, body_part, site, calibrated, "unsupported_site_or_body_part")
    if (body_part, site) not in {tuple(pair) for pair in item.get("supported_pairs", [])}:
        return unknown(item, body_part, site, calibrated, "site_body_part_pair_not_seen_in_training")
    contexts = {tuple(context) for context in item.get("supported_contexts", [])}
    if contexts and (body_part, site, side or "unknown") not in contexts:
        return unknown(item, body_part, site, calibrated, "site_body_part_side_not_seen_in_training")
    if item.get("requires_calibration") and not calibrated:
        return unknown(item, body_part, site, calibrated, "calibration_required")

    x = np.asarray([[float(features.get(name, 0.0)) for name in item["feature_columns"]]], dtype=float)
    action_probs = _class_probabilities(item.get("action_model"), x)
    allowed = {entry["id"] for entry in item["config"]["body_parts"].get(body_part, {}).get("actions", [])}
    allowed.update(entry["id"] for entry in item["config"].get("case_labels", []))
    action_probs = {label: score for label, score in action_probs.items() if label in allowed}
    if not action_probs:
        return unknown(item, body_part, site, calibrated, "no_action_classes_for_body_part")
    action, action_score = max(action_probs.items(), key=lambda pair: pair[1])
    action_minimum = float(item["config"]["prediction"]["minimum_action_score"])
    if action_score < action_minimum:
        if stream_key is not None:
            states.pop(stream_key, None)
        return unknown(item, body_part, site, calibrated, "low_action_score")

    phase_model = item.get("phase_model")
    phase_probs = _class_probabilities(phase_model, x)
    if not phase_probs:
        stage, reason, phase_score = "unknown", item.get("phase_unavailable_reason") or "phase_model_unavailable", None
    else:
        stage, phase_score = max(phase_probs.items(), key=lambda pair: pair[1])
        if phase_score < float(item["config"]["prediction"]["minimum_phase_score"]):
            stage, reason = "unknown", "low_phase_score"
        else:
            reason = ""

    held_ms = 0
    if stream_key is not None:
        state = states.setdefault(stream_key, {})
        history = state.setdefault("action_history", deque(maxlen=int(item["config"]["prediction"]["smoothing_windows"])))
        history.append(action)
        action_counts = {candidate: history.count(candidate) for candidate in set(history)}
        majority = max(action_counts, key=action_counts.get)
        if action_counts[majority] >= 2 or "stable_action" not in state:
            state["stable_action"] = majority
        action = state["stable_action"]
        if stage in {"neutral", "approaching", "holding", "returning"}:
            phase_history = state.setdefault("phase_history", deque(maxlen=int(item["config"]["prediction"]["smoothing_windows"])))
            phase_history.append(stage)
            phase_counts = {candidate: phase_history.count(candidate) for candidate in set(phase_history)}
            phase_majority = max(phase_counts, key=phase_counts.get)
            if phase_counts[phase_majority] >= 2 or "stable_phase" not in state:
                state["stable_phase"] = phase_majority
            stage = state["stable_phase"]
        else:
            if "phase_history" in state:
                state["phase_history"].clear()
            state.pop("stable_phase", None)
        now = device_time_s if device_time_s is not None else 0.0
        if stage == "holding":
            if state.get("hold_action") != action:
                state["hold_action"] = action
                state["hold_started"] = now
            held_ms = max(0, int((now - float(state.get("hold_started", now))) * 1000))
            if held_ms < float(item["config"]["prediction"]["holding_stable_seconds"]) * 1000:
                stage, reason = "unknown", "confirming_holding"
        else:
            state.pop("hold_action", None)
            state.pop("hold_started", None)

    return {
        "body_part": body_part,
        "action": action,
        "stage": stage if stage in {"neutral", "approaching", "holding", "unknown", "returning"} else "unknown",
        "score": round(action_score, 4),
        "score_kind": item["score_kind"],
        "phase_score": None if phase_score is None else round(phase_score, 4),
        "held_ms": held_ms,
        "calibrated": bool(calibrated),
        "mount_site": site,
        "model_version": item["model_version"],
        "reason": reason,
    }


def normalize_live(payload: dict, site: str, body_part: str, side: str):
    boot_ms = int(payload.get("sample_ms", payload.get("boot_ms", 0)))
    accel_unit = payload.get("accel_unit", "g")  # Legacy standalone firmware.
    if accel_unit not in {"g", "m/s^2"} or payload.get("gyro_unit", "rad/s") != "rad/s":
        raise ValueError("Unsupported sensor units")
    accel_scale = 9.80665 if accel_unit == "g" else 1.0
    row = {
        "sensor_uid": payload.get("sensor_uid") or payload.get("sensor_id") or "esp32",
        "mount_site": payload.get("mount_site") or site,
        "body_part": body_part,
        "side": (side if side != "unknown" else str(payload.get("side") or "unknown")),
        "seq": int(payload.get("seq", 0)),
        "device_time_s": boot_ms / 1000.0,
        "boot_ms": boot_ms,
        "stream_epoch": int(payload.get("boot_id", 0)),
        "calibration_id": payload.get("calibration_id") or "",
        "calibrated": bool(payload.get("calibrated", False)),
        "relative_angle_valid": bool(payload.get("relative_angle_valid", payload.get("calibrated", False))),
        "relative_orientation_valid": bool(payload.get("relative_orientation_valid", payload.get("calibrated", False))),
        "calibration_method_version": int(payload.get("calibration_method_version", 0)),
        "calibration_time_boot_ms": int(payload.get("calibration_time_boot_ms", 0)),
        "calibration_boot_id": int(payload.get("calibration_boot_id", 0)),
        "relative_roll_deg": payload.get("relative_roll_deg", 0.0),
        "relative_pitch_deg": payload.get("relative_pitch_deg", payload.get("wrist_flex_deg", 0.0)),
        "valid": bool(payload.get("valid", True)),
        "accel_x": float(payload["ax"]) * accel_scale,
        "accel_y": float(payload["ay"]) * accel_scale,
        "accel_z": float(payload["az"]) * accel_scale,
        "gyro_x": float(payload["gx"]),
        "gyro_y": float(payload["gy"]),
        "gyro_z": float(payload["gz"]),
    }
    return row


@app.get("/")
@app.get("/model_test.html")
def index():
    return send_from_directory(ROOT, "model_test.html")


@app.get("/monitor")
def mqtt_monitor():
    return send_from_directory(ROOT / "WEB", "index.html")


@app.get("/imu_protocol.js")
def monitor_protocol():
    return send_from_directory(ROOT / "WEB", "imu_protocol.js")


@app.get("/api/health")
def health():
    if bundle is None:
        return jsonify({"ready": False, "reason": "Train a model first with tools/train_model.py"}), 503
    return jsonify({"ready": True, "model_version": bundle["model_version"],
                    "feature_version": bundle["feature_version"],
                    "feature_columns": bundle["feature_columns"],
                    "feature_units": bundle.get("feature_units", {}),
                    "units": bundle.get("units", {}),
                    "score_kind": bundle["score_kind"],
                    "window_seconds": bundle["window_seconds"],
                    "sampling_hz": bundle["sampling_hz"],
                    "gap_reset_seconds": bundle["config"]["prediction"]["gap_reset_seconds"],
                    "action_labels": bundle["action_labels"], "phase_labels": bundle["phase_labels"],
                    "phase_available": bundle.get("phase_model") is not None,
                    "phase_unavailable_reason": bundle.get("phase_unavailable_reason"),
                    "requires_calibration": bundle["requires_calibration"],
                    "calibration_requirements": bundle.get("calibration_requirements", {}),
                    "mount_sites": bundle["mount_sites"], "body_parts": bundle["body_parts"],
                    "sides": bundle.get("sides", []),
                    "supported_pairs": bundle.get("supported_pairs", []),
                    "supported_contexts": bundle.get("supported_contexts", []),
                    "body_part_config": bundle["config"]["body_parts"],
                    "legacy_file_actions": {filename: action["id"]
                        for body in bundle["config"]["body_parts"].values()
                        for action in body.get("actions", [])
                        for filename in action.get("legacy_files", [])}})


@app.get("/api/plank/capabilities")
def plank_health():
    return jsonify(plank_capabilities(plank_config))


@app.post("/api/plank/calibrate")
def plank_calibrate():
    payload = request.get_json(force=True)
    if not isinstance(payload, dict):
        return jsonify({"ready": False, "reason": "invalid_request_body"}), 400
    stream_key = str(payload.get("stream_key") or "")
    if not stream_key or len(stream_key) > 240:
        return jsonify({"ready": False, "reason": "invalid_stream_key"}), 400
    try:
        calibration = make_calibration(payload.get("frames"), plank_config)
    except PlankInputError as error:
        return jsonify({"ready": False, "reason": str(error)}), 400
    state = create_phase_state(calibration)
    state["stream_key"] = stream_key
    with plank_lock:
        plank_states[stream_key] = state
    return jsonify({
        "ready": True,
        "model_version": plank_config["model_version"],
        "trained_classifier": False,
        "thresholds_validated": bool(plank_config["score_thresholds"].get("validated", False)),
        "baseline_pitch_deg": round(calibration["baseline_pitch_deg"], 3),
        "baseline_roll_deg": round(calibration["baseline_roll_deg"], 3),
        "pitch_std_deg": calibration["pitch_std_deg"],
        "roll_std_deg": calibration["roll_std_deg"],
        "node_baselines": calibration["node_baselines"],
        "sample_count": calibration["sample_count"],
        "duration_seconds": calibration["duration_seconds"],
    })


@app.post("/api/plank/frame")
def plank_frame():
    payload = request.get_json(force=True)
    if not isinstance(payload, dict):
        return jsonify({"ready": False, "phase": "unknown", "reason": "invalid_request_body"}), 400
    stream_key = str(payload.get("stream_key") or "")
    if not stream_key or len(stream_key) > 240:
        return jsonify({"ready": False, "phase": "unknown", "reason": "invalid_stream_key"}), 400
    with plank_lock:
        state = plank_states.get(stream_key)
        if state is None:
            return jsonify({"ready": False, "phase": "unknown", "reason": "calibration_required"}), 409
        try:
            result = advance_phase(state, payload.get("frame"), plank_config, time.monotonic())
        except PlankInputError as error:
            return jsonify({"ready": False, "phase": "unknown", "reason": str(error)}), 409
    result.update({"ready": True, "model_version": plank_config["model_version"]})
    return jsonify(result)


@app.post("/api/predict-window")
def predict_window():
    if bundle is None:
        return health()
    payload = request.get_json(force=True)
    rows = payload.get("rows", [])
    site = str(payload.get("mount_site", "other"))
    body_part = str(payload.get("body_part", "other"))
    if not isinstance(rows, list) or any(not isinstance(row, dict) for row in rows):
        return jsonify(unknown(bundle, body_part, site, False, "invalid_window_rows"))
    if len(rows) < 3:
        return jsonify(unknown(bundle, body_part, site, False, "window_too_short"))
    supported_calibration_versions = {int(value) for value in
                                      bundle.get("calibration_requirements", {}).get("method_versions", [])}
    try:
        required_seconds = float(bundle["window_seconds"])
        if float(rows[-1]["device_time_s"]) - float(rows[0]["device_time_s"]) + 1e-9 < required_seconds:
            return jsonify(unknown(bundle, body_part, site, False, "window_not_full"))
        rows = list(rows)
        while (len(rows) > 3 and
               float(rows[-1]["device_time_s"]) - float(rows[0]["device_time_s"])
               > required_seconds + 1e-9):
            rows.pop(0)
        for index, row in enumerate(rows):
            if row.get("valid") is not True:
                return jsonify(unknown(bundle, body_part, site, False, "invalid_sensor_sample"))
            if row.get("mount_site", site) != site or row.get("body_part", body_part) != body_part:
                return jsonify(unknown(bundle, body_part, site, False, "window_crosses_site_or_body_part"))
            if bundle.get("requires_calibration") and (
                    not bool(row.get("calibrated"))
                    or not bool(row.get("relative_orientation_valid"))
                    or not row.get("calibration_id")
                    or int(row.get("calibration_method_version", 0)) not in supported_calibration_versions):
                return jsonify(unknown(bundle, body_part, site, False, "calibration_required_or_outdated"))
            if index == 0:
                continue
            previous = rows[index - 1]
            gap = float(row["device_time_s"]) - float(previous["device_time_s"])
            seq_gap = int(row["seq"]) != int(previous["seq"]) + 1
            boundary = any(row.get(key) != previous.get(key) for key in
                           ("calibration_id", "calibration_method_version", "calibrated",
                            "relative_orientation_valid", "stream_epoch",
                            "sensor_uid", "mount_site", "body_part", "side"))
            if gap <= 0 or gap > float(bundle["config"]["prediction"]["gap_reset_seconds"]) or seq_gap or boundary:
                return jsonify(unknown(bundle, body_part, site, False, "window_crosses_gap_or_stream_boundary"))
    except (KeyError, TypeError, ValueError):
        return jsonify(unknown(bundle, body_part, site, False, "invalid_window_metadata"))
    use_angles = bool(bundle.get("requires_calibration"))
    from imu_features import window_features
    try:
        features = window_features(rows, site, bundle["sampling_hz"], use_angles, body_part,
                                   str(rows[-1].get("side") or "unknown"))
    except (ValueError, KeyError, TypeError) as error:
        return jsonify(unknown(bundle, body_part, site, False, f"invalid_window: {error}"))
    calibrated = all(bool(row.get("calibrated", False)) for row in rows)
    stream_key = str(payload.get("stream_key") or "")
    try:
        requested_time = payload.get("device_time_s")
        device_time_s = float(rows[-1]["device_time_s"] if requested_time is None else requested_time)
    except (KeyError, TypeError, ValueError):
        return jsonify(unknown(bundle, body_part, site, calibrated, "invalid_window_timestamp"))
    if stream_key:
        previous = states.get(stream_key, {})
        stream_context = payload.get("stream_context") or {
            key: rows[-1].get(key, "") for key in
            ("sensor_uid", "mount_site", "body_part", "side", "subject_id", "session_id",
             "trial_id", "stream_epoch", "calibration_id", "calibration_method_version")
        }
        if (previous and (stream_context != previous.get("stream_context")
                          or int(payload.get("seq", rows[-1]["seq"])) <= int(previous.get("last_input_seq", -1))
                          or device_time_s <= float(previous.get("last_input_time", 0.0))
                          or device_time_s - float(previous.get("last_input_time", 0.0)) >
                          required_seconds + float(bundle["config"]["prediction"]["gap_reset_seconds"]))):
            states.pop(stream_key, None)
        result = predict_from_features(features, site, body_part, calibrated, stream_key,
                                       device_time_s, str(rows[-1].get("side") or "unknown"))
        result.update({"calibration_id": rows[-1].get("calibration_id", ""),
                       "calibration_method_version": rows[-1].get("calibration_method_version", 0),
                       "calibration_time_boot_ms": rows[-1].get("calibration_time_boot_ms", 0),
                       "calibration_boot_id": rows[-1].get("calibration_boot_id", 0)})
        state = states.setdefault(stream_key, {})
        state["last_input_seq"] = int(payload.get("seq", rows[-1].get("seq", -1)))
        state["last_input_time"] = device_time_s
        state["stream_context"] = stream_context
        return jsonify(result)
    result = predict_from_features(features, site, body_part, calibrated,
                                   side=str(rows[-1].get("side") or "unknown"))
    result.update({"calibration_id": rows[-1].get("calibration_id", ""),
                   "calibration_method_version": rows[-1].get("calibration_method_version", 0),
                   "calibration_time_boot_ms": rows[-1].get("calibration_time_boot_ms", 0),
                   "calibration_boot_id": rows[-1].get("calibration_boot_id", 0)})
    return jsonify(result)


@app.get("/api/live")
def live():
    if bundle is None:
        return health()
    raw_ip = request.args.get("ip", "192.168.4.1").strip()
    site = request.args.get("mount_site", "")
    body_part = request.args.get("body_part", "")
    side = request.args.get("side", "unknown")
    try:
        address = ipaddress.ip_address(raw_ip)
        if not address.is_private:
            raise ValueError("Use a private ESP32 IP address")
        if not site or not body_part:
            raise ValueError("Select a mount site and body part")
        cursor_key = f"{address}|{site}|{body_part}|{side}"
        cursor = live_cursors.get(cursor_key, {})
        cursor_boot_id = int(cursor.get("boot_id", 0))
        cursor_seq = int(cursor.get("seq", 0))
        # Gateway /samples uses per-node cursors, unlike the legacy server.
        query = urlencode({"after_seq": cursor_seq, "boot_id": cursor_boot_id, **{
            f"n{i}_{key}": value for i in range(1, 5)
            for key, value in (("after_seq", cursor_seq), ("boot_id", cursor_boot_id))}})
        with urllib.request.urlopen(f"http://{address}/samples?{query}", timeout=2.0) as response:
            payload = json.loads(response.read().decode("utf-8"))
        if isinstance(payload, dict) and "nodes" in payload and isinstance(payload["nodes"], list):
            target_node = None
            for nd in payload["nodes"]:
                if nd.get("mount_site") == site and nd.get("body_part") == body_part:
                    target_node = nd
                    break
            if target_node:
                metadata = {key: target_node[key] for key in
                            ("sensor_uid", "mount_site", "body_part", "side", "boot_id") if key in target_node}
                units = payload.get("units", {})
                metadata.update({"accel_unit": units.get("accel", "m/s^2"),
                                 "gyro_unit": units.get("gyro", "rad/s")})
                payload["samples"] = [{**metadata, **row} for row in target_node.get("samples", [])]
                payload["boot_id"] = target_node.get("boot_id", 0)
                payload["latest_seq"] = target_node.get("latest_seq", 0)
                payload["dropped"] = target_node.get("dropped", False)
        if not isinstance(payload, dict) or not isinstance(payload.get("samples"), list):
            raise ValueError("ESP /samples response is invalid; upload firmware with buffered sample support")
    except (KeyError, ValueError, OSError, urllib.error.URLError, json.JSONDecodeError) as error:
        old_cursor = live_cursors.pop(f"{raw_ip}|{site}|{body_part}|{side}", {})
        old_key = old_cursor.get("stream_key")
        if old_key:
            with lock:
                buffers.pop(old_key, None)
                states.pop(old_key, None)
        return jsonify(unknown(bundle, body_part, site, False, f"live_read_error: {error}")), 502

    current_boot_id = int(payload.get("boot_id", 0))
    latest_seq = int(payload.get("latest_seq", cursor_seq))
    incoming = payload["samples"]
    if incoming:
        newest_payload = incoming[-1]
        newest_row = normalize_live(newest_payload, site, body_part, side)
        if newest_row["mount_site"] != site:
            return jsonify(unknown(bundle, body_part, newest_row["mount_site"], newest_row["calibrated"],
                                   "selected_mount_site_does_not_match_esp")), 409
        newest_side = newest_payload.get("side")
        if side != "unknown" and newest_side not in (None, "unknown", side):
            return jsonify(unknown(bundle, body_part, site, newest_row["calibrated"],
                                   "selected_side_does_not_match_esp")), 409
        incoming = [sample for sample in incoming
                    if sample.get("mount_site") == site
                    and (side == "unknown" or sample.get("side") in (None, "unknown", side))]
    old_stream_key = cursor.get("stream_key")
    if ((cursor_boot_id and cursor_boot_id != current_boot_id) or bool(payload.get("dropped"))):
        if old_stream_key:
            with lock:
                buffers.pop(old_stream_key, None)
                states.pop(old_stream_key, None)

    if not incoming:
        if old_stream_key:
            with lock:
                cached = states.get(old_stream_key, {}).get("last_prediction")
                if cached is not None:
                    result = dict(cached)
                    result["duplicate_sample"] = True
                    return jsonify(result)
        return jsonify(unknown(bundle, body_part, site, False, "warming_up_or_waiting_for_sample"))

    latest_result = None
    latest_stream_key = old_stream_key
    with lock:
        supported_calibration_versions = {int(value) for value in
                                           bundle.get("calibration_requirements", {}).get("method_versions", [])}
        for sample_payload in incoming:
            row = normalize_live(sample_payload, site, body_part, side)
            if row["mount_site"] != site:
                result = unknown(bundle, body_part, row["mount_site"], row["calibrated"],
                                 "selected_mount_site_does_not_match_esp")
                return jsonify(result), 409
            device_side = sample_payload.get("side")
            if side != "unknown" and device_side not in (None, "unknown", side):
                result = unknown(bundle, body_part, site, row["calibrated"],
                                 "selected_side_does_not_match_esp")
                return jsonify(result), 409

            key = f"{row['sensor_uid']}|{row['mount_site']}|{body_part}|{row['side']}"
            latest_stream_key = key
            buffer = buffers.get(key)
            if buffer is None:
                buffer = StreamBuffer(bundle["window_seconds"], bundle["sampling_hz"],
                                      bundle["config"]["prediction"]["gap_reset_seconds"])
                buffers[key] = buffer
            was_calibrated = bool(row["calibrated"])
            if bundle.get("requires_calibration") and (
                    not was_calibrated or not row.get("relative_orientation_valid")
                    or not row.get("calibration_id")):
                states.pop(key, None)
                buffer.reset()
                latest_result = unknown(bundle, body_part, site, was_calibrated, "calibration_required")
                continue
            if (bundle.get("requires_calibration")
                    and int(row.get("calibration_method_version", 0)) not in supported_calibration_versions):
                states.pop(key, None)
                buffer.reset()
                latest_result = unknown(bundle, body_part, site, was_calibrated,
                                        "calibration_method_version_unavailable")
                continue
            if buffer.rows:
                previous = buffer.rows[-1]
                if (row["seq"] <= int(previous["seq"])
                        or row["seq"] != int(previous["seq"]) + 1
                        or row["device_time_s"] <= float(previous["device_time_s"])
                        or row["device_time_s"] - float(previous["device_time_s"]) > buffer.gap_reset_seconds
                        or row["calibration_id"] != previous.get("calibration_id")
                        or row["stream_epoch"] != previous.get("stream_epoch")
                        or row["side"] != previous.get("side")):
                    states.pop(key, None)
                    buffer.reset()
            if not buffer.add(row):
                continue
            state = states.setdefault(key, {})
            state["last_fresh_monotonic"] = time.monotonic()
            try:
                features = buffer.features(site, bundle.get("requires_calibration", False))
            except ValueError as error:
                latest_result = unknown(bundle, body_part, site, was_calibrated,
                                        f"invalid_window: {error}")
                continue
            if features is None:
                result = unknown(bundle, body_part, site, was_calibrated, "warming_up_or_stream_reset")
            else:
                result = predict_from_features(features, site, body_part, was_calibrated, key,
                                               row["device_time_s"], row["side"])
            result.update({"seq": row["seq"], "device_time_s": row["device_time_s"],
                           "calibration_id": row["calibration_id"],
                           "calibration_method_version": row["calibration_method_version"],
                           "calibration_time_boot_ms": row["calibration_time_boot_ms"],
                           "calibration_boot_id": row["calibration_boot_id"],
                           "firmware_status": sample_payload.get("status", "unknown"),
                           "firmware_status_is_not_ml_output": True})
            state["last_prediction"] = dict(result)
            latest_result = result

        live_cursors[cursor_key] = {
            "boot_id": current_boot_id,
            "seq": latest_seq,
            "stream_key": latest_stream_key,
        }
    return jsonify(latest_result or unknown(bundle, body_part, site, False, "no_usable_samples"))


def create_app(model_path: Path):
    global bundle
    if model_path.exists():
        candidate = joblib.load(model_path)
        expected = feature_names(candidate["mount_sites"], candidate.get("requires_calibration", False),
                                 candidate["body_parts"], candidate.get("sides", []))
        if candidate.get("feature_version") != FEATURE_VERSION or candidate["feature_columns"] != expected:
            raise ValueError("Model feature schema does not match imu_features.py; select or retrain a compatible bundle")
        for key in ("action_model", "phase_model"):
            estimator = candidate.get(key)
            if estimator is not None and getattr(estimator, "n_features_in_", len(expected)) != len(expected):
                raise ValueError(f"{key} input dimensions do not match the feature schema")
        bundle = candidate
    return app


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", type=Path, default=ROOT / "models" / "pose_model" / "model_bundle.joblib")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    if not args.model.exists():
        parser.error(f"Model bundle not found: {args.model}. Run tools/train_model.py first.")
    create_app(args.model)
    app.run(host=args.host, port=args.port, debug=False, threaded=True)


if __name__ == "__main__":
    main()
```

### 6.41 ไฟล์ `test/test_imu_protocol.cjs` — ชุดทดสอบโปรโตคอล IMU และการแปลงหน่วย (35 บรรทัด)
```javascript
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const {sampleFromMessage, SampleWindow} = require('../WEB/imu_protocol.js');
const context = {mount_site: 'wrist', body_part: 'wrist', side: 'unknown'};
const message = {units: {accel: 'm/s^2', gyro: 'rad/s'}, gateway: {clientId: 'gw', bootId: 10}};
const rows = Array.from({length: 21}, (_, i) => sampleFromMessage(message, {
  nodeId: 1, sensor_uid: 'sensor-1', mount_site: 'wrist', body_part: 'wrist', side: 'unknown',
  boot_id: 20, seq: i, ts: 5000+i*50, accel: [0.1+i/100, 0.2, 9.80665],
  gyro: [0.01, 0.02+i/1000, 0.03], roll: 1, pitch: 2, yaw: 3,
  valid: true, calibrated: true, relative_orientation_valid: false
}, context));
if (process.argv.includes('--emit')) {
  process.stdout.write(JSON.stringify(rows));
} else {
  const window = new SampleWindow();
  rows.forEach(row => window.add(row));
  assert.equal(window.ready(), true);
  assert.equal(window.rows.length, 21);
  assert.equal(window.add(rows.at(-1)), false);
  window.add({...rows.at(-1), seq: 22, device_time_s: 6.1});
  assert.equal(window.ready(), false);
  window.add({...rows[0], stream_epoch: 'new-boot'});
  assert.equal(window.rows.length, 1);
  const node = {seq: 0, ts: 0, valid: true, accel: [0, 0, 9.8]};
  assert.throws(() => sampleFromMessage(message, node, context), /gyro/);
  assert.throws(() => sampleFromMessage({...message, units: {}}, {...node, gyro: [0,0,0]}, context), /units/);
  assert.throws(() => sampleFromMessage(message, {...node, gyro:[0,0,0], mount_site:'neck'}, context), /context/);
  const html = fs.readFileSync(require('node:path').join(__dirname, '../WEB/index.html'), 'utf8');
  for (const m of html.matchAll(/<script\b[^>]*>([\s\S]*?)<\/script>/g)) if (m[1].trim()) new vm.Script(m[1]);
  assert.ok(!html.includes('/api/live?'), 'Monitor must use MQTT samples, not sensor HTTP');
  assert.ok(html.includes('/api/predict-window'));
  console.log('PASS: MQTT SI units, six axes, zero sequence, windows, duplicate/gap/boot reset, inline JS and MQTT-only data source');
}
```

### 6.42 ไฟล์ `test/test_multinode_recorder.cjs` — ชุดทดสอบการบันทึกข้อมูลหลาย Node พร้อมกัน (88 บรรทัด)
```javascript
const assert = require('node:assert/strict');
const {MultiNodeRecorder, nodeKey, csv} = require('../WEB/imu_protocol.js');

const base = 1800000000000;
function frame(i, gatewayId = 'gw-a') {
  return {type: 'imu_stream', schema: 'planko-imu-v2',
    units: {accel: 'm/s^2', gyro: 'rad/s'},
    gateway: {clientId: gatewayId, bootId: 100, mqttSeq: i},
    nodes: [
      {nodeId: 1, sensor_uid: 'neck-uid', mount_site: 'neck', body_part: 'neck'},
      {nodeId: 2, sensor_uid: 'lumbar-uid', mount_site: 'lumbar', body_part: 'back'}
    ].map((node, n) => ({...node, side: 'unknown', boot_id: 200 + n, seq: i,
      // Independent device clocks: both streams have 20 samples per second.
      ts: 10000 + n * 7000 + i * 50,
      accel: [n + i / 100, 0.2, 9.80665], gyro: [0.01, n / 10, 0.03],
      pitch: n * 20, roll: 1, yaw: 0, valid: true, isMock: false,
      calibrated: true, relative_orientation_valid: false}))};
}
const metadata = {subject_id: 'P01', session_id: 'S01', trial_id: 'trial-01'};
const annotation = {phase: 'neutral', source: 'guided_timer_cue', reviewed: false};
const label = (key, node) => node.nodeId === 1 ? 'neck_neutral' : 'lumbar_neutral';
const recorder = new MultiNodeRecorder();
recorder.start('gw-a', metadata, base);
for (let i = 0; i <= 40; i++) recorder.ingest(frame(i), label, annotation, base + i * 50);
const streams = [...recorder.streams.values()];
assert.equal(streams.length, 2);
for (const stream of streams) {
  assert.equal(stream.rows.length, 41);
  assert.equal(stream.rows.at(-1).device_time_s - stream.rows[0].device_time_s, 2);
  assert.equal(stream.rows.every(r => r.sensor_uid === stream.sensorUid), true);
  assert.equal(stream.rows.every(r => r.phase_annotation_reviewed === false), true);
  assert.equal(stream.missing, 0);
}
assert.equal(streams[1].rows[0].body_part, 'lumbar');
assert.equal(streams[1].rows[0].firmware_body_part, 'back');

if (process.argv.includes('--emit')) {
  recorder.stop(base + 2050);
  process.stdout.write(JSON.stringify(streams.map(s => ({name: `node${s.nodeId}.csv`, csv: csv(s.rows)}))));
} else {
  // Repeated latest-sample frames and other gateways must not contaminate capture.
  recorder.ingest(frame(40), label, annotation, base + 2050);
  recorder.ingest(frame(41, 'gw-b'), label, annotation, base + 2050);
  assert.equal(streams[0].rows.length, 41);
  assert.equal(streams[0].duplicates, 1);
  // One node loses a sample, while the other remains contiguous.
  const gap = frame(42);
  gap.nodes[1] = frame(41).nodes[1];
  recorder.ingest(gap, label, annotation, base + 2100);
  assert.equal(streams[0].rows.at(-1).gap_before, true);
  assert.equal(streams[0].missing, 1);
  assert.equal(streams[1].rows.at(-1).gap_before, false);
  recorder.ingest(frame(39), label, annotation, base + 2150);
  assert.equal(streams[0].outOfOrder, 1);
  // Missing gyro is excluded, never filled with zero; recovery is a boundary.
  const bad = frame(43);
  delete bad.nodes[0].gyro;
  recorder.ingest(bad, label, annotation, base + 2150);
  assert.equal(streams[0].invalid, 1);
  recorder.ingest(frame(44), label, annotation, base + 2200);
  assert.equal(streams[0].rows.at(-1).gap_before, true);
  assert.equal(streams[0].rows.at(-1).seq, 44);
  // A node reboot stays in its own file and starts a new epoch.
  const reboot = frame(0);
  reboot.nodes = [reboot.nodes[0]];
  reboot.nodes[0].boot_id++;
  recorder.ingest(reboot, label, annotation, base + 2250);
  assert.equal(streams[0].rows.at(-1).gap_before, true);
  assert.equal(streams[0].rows.at(-1).seq, 0);
  // A changed mounting context creates a different dataset rather than relabeling old rows.
  const moved = frame(1);
  moved.nodes = [{...moved.nodes[0], mount_site: 'wrist', body_part: 'wrist'}];
  recorder.ingest(moved, () => 'wrist_neutral', annotation, base + 2300);
  assert.equal(recorder.streams.size, 3);
  assert.equal(recorder.streams.get(nodeKey(moved, moved.nodes[0])).rows[0].body_part, 'wrist');
  const encoded = csv([{subject_id: 'P,"one"\nsecond line'}]);
  assert.ok(encoded.includes('"P,""one""\nsecond line"'));
  recorder.stop(base + 2350);
  const count = streams[0].rows.length;
  recorder.ingest(frame(45), label, annotation, base + 2400);
  assert.equal(streams[0].rows.length, count);
  const manifest = recorder.manifest();
  assert.equal(manifest.target_sampling_hz, 20);
  assert.equal(manifest.streams[0].observed_missing_samples, 2);
  assert.equal(manifest.streams[0].invalid_excluded, 1);
  console.log('PASS: simultaneous 20 Hz nodes, identity isolation, per-node labels, SI CSV, duplicate/out-of-order exclusion, gaps, reboot and context boundaries');
}
```

### 6.43 ไฟล์ `test/test_multinode_page.cjs` — ชุดทดสอบการทำงานของหน้าเว็บ WEB/index.html (111 บรรทัด)
```javascript
// Run the actual page handlers with a minimal DOM and a local MQTT client double.
// No messages leave this process; rendering and a real broker are separate checks.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const protocol = require('../WEB/imu_protocol.js');
const html = fs.readFileSync(path.join(__dirname, '../WEB/index.html'), 'utf8');
class Element {
  constructor(tag) {
    this.tag = tag; this.children = []; this.listeners = new Map(); this.style = {};
    this.classList = {add() {}, remove() {}, toggle() {}};
    this.dataset = {}; this.disabled = false; this.checked = false; this._value = null;
  }
  get options() { return this.children.filter(e => e.tag === 'option'); }
  get value() { return this._value ?? this.options[0]?.value ?? ''; }
  set value(value) { this._value = value; }
  set innerHTML(value) { this.children = []; }
  appendChild(child) { this.children.push(child); return child; }
  append(...children) { this.children.push(...children); }
  prepend(...children) { this.children.unshift(...children); }
  replaceChildren(...children) { this.children = children; }
  setAttribute() {}
  addEventListener(name, handler) {
    if (!this.listeners.has(name)) this.listeners.set(name, []);
    this.listeners.get(name).push(handler);
  }
  fire(name) { for (const handler of this.listeners.get(name) || []) handler({target: this}); }
  click() { if (!this.disabled) this.fire('click'); }
  remove() {}
  getBoundingClientRect() { return {width: 800, height: 600}; }
  getContext() { return {}; }
}
const elements = new Map();
for (const match of html.matchAll(/<(\w+)\b([^>]*\bid="([^"]+)"[^>]*)>/g)) {
  const e = new Element(match[1]);
  const value = match[2].match(/\bvalue="([^"]*)"/);
  if (value) e.value = value[1];
  e.checked = /\bchecked\b/.test(match[2]);
  e.disabled = /\bdisabled\b/.test(match[2]);
  elements.set(match[3], e);
}
for (const match of html.matchAll(/<select\b[^>]*id="([^"]+)"[^>]*>([\s\S]*?)<\/select>/g)) {
  const select = elements.get(match[1]);
  for (const option of match[2].matchAll(/<option\b([^>]*)>([\s\S]*?)<\/option>/g)) {
    const e = new Element('option');
    e.value = option[1].match(/value="([^"]*)"/)?.[1] || '';
    select.appendChild(e);
    if (/selected/.test(option[1])) select.value = e.value;
  }
}
let client, wall = 1800000000000;
const blobs = new Map();
const context = {
  PlanKOImu: protocol, console,
  URLSearchParams,
  location: {search: '', hostname: '127.0.0.1', protocol: 'http:', origin: 'http://127.0.0.1'},
  navigator: {onLine: true}, TextEncoder,
  document: {getElementById: id => {
    assert.ok(elements.has(id), `Unknown DOM id: ${id}`); return elements.get(id);
  }, createElement: tag => new Element(tag), querySelectorAll: () => [], body: new Element('body')},
  Option: function(text, value) { const e = new Element('option'); e.value = value; e.textContent = text; return e; },
  Date: class extends Date { static now() { return wall; } },
  performance: {now: () => wall}, setInterval() {}, setTimeout() {}, clearTimeout() {},
  requestAnimationFrame() {}, addEventListener() {}, devicePixelRatio: 1,
  Blob, URL: {createObjectURL(blob) { const url = `blob:test-${blobs.size}`; blobs.set(url, blob); return url; }, revokeObjectURL() {}},
  alert: message => { throw new Error(`Unexpected alert: ${message}`); },
  Paho: {MQTT: {Client: class {
    constructor(host, port, route) { assert.equal(host, '127.0.0.1'); assert.equal(port, 9001); assert.equal(route, '/mqtt'); client = this; }
    isConnected() { return true; }
    connect(options) { assert.equal(options.mqttVersion, 4); options.onSuccess(); }
    subscribe(topic, options) { assert.equal(topic, 'planko/imu'); options.onSuccess(); }
    disconnect() {}
  }}}
};
context.window = context;
vm.createContext(context);
for (const script of html.matchAll(/<script\b[^>]*>([\s\S]*?)<\/script>/g)) {
  if (script[1].trim()) vm.runInContext(script[1], context);
}
function send(seq, gateway = 'gw-test') {
  const data = {type: 'imu_stream', units: {accel: 'm/s^2', gyro: 'rad/s'},
    gateway: {clientId: gateway, bootId: 1, mqttSeq: seq},
    nodes: [1, 2].map(id => ({nodeId: id, sensor_uid: `uid-${id}`, mount_site: 'wrist',
      body_part: 'wrist', side: 'unknown', seq, ts: id * 10000 + seq * 50, boot_id: id,
      accel: [0, 0, 9.80665], gyro: [0.01, 0.02, 0.03], valid: true, pitch: 1, roll: 2, yaw: 3}))};
  client.onMessageArrived({destinationName: 'planko/imu', retained: false, payloadString: JSON.stringify(data)});
}
send(0);
assert.equal(elements.get('gatewayFilter').value, 'gw-test');
assert.equal(elements.get('recNodeList').children.length, 2);
elements.get('sharedActionLabel').value = 'case_flexion';
elements.get('guidedTimerCheck').checked = false;
elements.get('recStartBtn').click();
assert.equal(elements.get('gatewayFilter').disabled, true);
// Rendering is paused and a different (offline) node is selected; capture must continue for BOTH live nodes.
elements.get('pauseBtn').click();
elements.get('nodeSelect').value = '4';
elements.get('nodeSelect').fire('change');
for (let i = 1; i <= 40; i++) { wall += 50; send(i); }
send(41, 'another-gateway');
assert.equal(vm.runInContext('[...datasetRecorder.streams.values()].every(s => s.rows.length === 40)', context), true);
elements.get('recStopBtn').click();
assert.equal(elements.get('recDownloadList').children.length, 3);
assert.equal(elements.get('gatewayFilter').disabled, false);
assert.equal(elements.get('recDownloadAllBtn').disabled, false);
assert.equal(vm.runInContext('datasetRecorder.manifest().streams.length', context), 2);
assert.equal(vm.runInContext('[...datasetRecorder.streams.values()].every(s => s.rows.every(r => r.action_label === "case_flexion"))', context), true);
assert.equal(vm.runInContext('[...captureNodes.values()].every(e => e.status.textContent.includes("20.0 Hz"))', context), true);
console.log('PASS: actual index.html MQTT handlers, gateway pinning, one shared case label, simultaneous capture while display paused/offline, and two CSVs plus manifest');
```

### 6.44 ไฟล์ `test/test_multinode_training.py` — ชุดทดสอบการอ่านไฟล์ CSV หลาย Node เข้าสู่ระบบเทรน (54 บรรทัด)
```python
"""Exercise browser multi-node CSV through the real training reader and extractor."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from train_model import load_config, load_windows, read_recording


class MultiNodeTrainingTest(unittest.TestCase):
    def test_separate_20hz_csv_files_produce_separate_training_windows(self):
        files = json.loads(subprocess.check_output([
            'node', '--preserve-symlinks', '--preserve-symlinks-main',
            str(ROOT / 'test/test_multinode_recorder.cjs'), '--emit'], encoding='utf-8'))
        config = load_config(ROOT / 'config/pose_labels.json')
        with tempfile.TemporaryDirectory() as folder:
            paths = []
            for item in files:
                path = Path(folder) / item['name']
                path.write_text(item['csv'], encoding='utf-8')
                paths.append(path)
                rows = read_recording(path, config)
                self.assertEqual(len(rows), 41)
                self.assertEqual(len({r['sensor_uid'] for r in rows}), 1)
                self.assertEqual(rows[-1]['device_time_s'] - rows[0]['device_time_s'], 2)
                self.assertTrue(all(r['phase_label'] == '' for r in rows), 'unreviewed timer cues are not phase ground truth')
                self.assertTrue(all(r['valid'] for r in rows))
            windows, ignored = load_windows(paths, config, 1.0)
            self.assertEqual(ignored, [])
            self.assertTrue(windows)
            self.assertEqual({r['sensor_uid'] for r in windows}, {'neck-uid', 'lumbar-uid'})
            self.assertEqual({r['body_part'] for r in windows}, {'neck', 'lumbar'})
            self.assertEqual({r['action_label'] for r in windows}, {'neck_neutral', 'lumbar_neutral'})
            # Explicit discontinuities survive CSV ingestion, even if clocks and counters look contiguous.
            text = files[0]['csv']
            import csv
            import io
            data = list(csv.DictReader(io.StringIO(text.lstrip('\ufeff'))))
            data[10]['gap_before'] = 'true'
            path = paths[0]
            with path.open('w', encoding='utf-8', newline='') as output:
                writer = csv.DictWriter(output, fieldnames=data[0].keys())
                writer.writeheader()
                writer.writerows(data)
            self.assertTrue(read_recording(path, config)[10]['gap_before'])


if __name__ == '__main__':
    unittest.main()
```

### 6.45 ไฟล์ `test/test_model_contract.py` — ชุดทดสอบความถูกต้องของอินพุต/เอาต์พุตโมเดล AI (97 บรรทัด)
```python
"""Regression checks for MQTT -> browser CSV -> train/inference parity."""
import csv
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import imu_features
import serve_model
import train_model


class ModelContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rows = json.loads(subprocess.check_output(['node', '--preserve-symlinks', '--preserve-symlinks-main',
                                                     str(ROOT/'test/test_imu_protocol.cjs'), '--emit']))
        serve_model.create_app(ROOT/'models/pose_model/model_bundle.joblib')
        cls.client = serve_model.app.test_client()

    def test_browser_csv_training_and_prediction_use_identical_features(self):
        rows = [{**r, 'action_label':'wrist_neutral', 'phase_label':'',
                 'subject_id':'test', 'session_id':'s1', 'trial_id':'t1'} for r in self.rows]
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/'capture.csv'
            with path.open('w', newline='', encoding='utf-8') as stream:
                writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
                writer.writeheader()
                writer.writerows(rows)
            loaded = train_model.read_recording(path, train_model.load_config(train_model.DEFAULT_CONFIG))
        self.assertEqual(loaded[0]['seq'], 0)
        train_windows = imu_features.samples_to_windows(loaded, 1, 20)
        self.assertEqual(len(train_windows), 1)
        expected = train_windows[0]['features']
        captured = {}
        def capture(features, *args, **kwargs):
            captured.update(features)
            return {'action':'test', 'stage':'unknown'}
        with patch.object(serve_model, 'predict_from_features', side_effect=capture):
            response = self.client.post('/api/predict-window', json={
                'rows': self.rows, 'mount_site':'wrist', 'body_part':'wrist'})
        self.assertEqual(response.status_code, 200)
        self.assertEqual(set(expected), set(captured))
        np.testing.assert_allclose([expected[k] for k in sorted(expected)],
                                   [captured[k] for k in sorted(expected)], rtol=1e-12, atol=1e-12)
        self.assertEqual(len(serve_model.bundle['feature_columns']), 40)

    def test_missing_gyro_and_invalid_samples_are_rejected(self):
        for change, reason in [({'gyro_x':None}, 'invalid_window'), ({'valid':False}, 'invalid_sensor_sample')]:
            rows = [dict(r) for r in self.rows]
            rows[10].update(change)
            result = self.client.post('/api/predict-window', json={
                'rows':rows, 'mount_site':'wrist', 'body_part':'wrist'}).get_json()
            self.assertTrue(result['reason'].startswith(reason), result)

    def test_actual_bundle_prediction_and_unsupported_context(self):
        result = self.client.post('/api/predict-window', json={
            'rows':self.rows, 'mount_site':'wrist', 'body_part':'wrist'}).get_json()
        self.assertIn(result['action'], set(serve_model.bundle['action_labels']) | {'unknown'})
        unsupported = [{**r, 'mount_site':'neck', 'body_part':'neck'} for r in self.rows]
        result = self.client.post('/api/predict-window', json={
            'rows':unsupported, 'mount_site':'neck', 'body_part':'neck'}).get_json()
        self.assertEqual(result['reason'], 'unsupported_site_or_body_part')

    def test_gateway_si_values_and_legacy_g_values(self):
        payload = {'ax':0, 'ay':0, 'az':9.80665, 'gx':0.1, 'gy':0.2, 'gz':0.3, 'accel_unit':'m/s^2'}
        self.assertEqual(serve_model.normalize_live(payload, 'wrist','wrist','unknown')['accel_z'], 9.80665)
        payload.update(az=1, accel_unit='g')
        self.assertEqual(serve_model.normalize_live(payload, 'wrist','wrist','unknown')['accel_z'], 9.80665)

    def test_gaps_rejected_by_training_and_inference(self):
        rows = [{**r, 'action_label':'wrist_neutral', 'phase_label':''} for r in self.rows]
        rows[10]['seq'] += 1
        self.assertEqual(imu_features.samples_to_windows(rows, 1, 20), [])
        result = self.client.post('/api/predict-window', json={
            'rows':rows, 'mount_site':'wrist', 'body_part':'wrist'}).get_json()
        self.assertEqual(result['reason'], 'window_crosses_gap_or_stream_boundary')

    def test_health_monitor_and_local_cors(self):
        health = self.client.get('/api/health', headers={'Origin':'null'})
        self.assertEqual(health.headers['Access-Control-Allow-Origin'], 'null')
        self.assertEqual(len(health.get_json()['feature_columns']), 40)
        for url in ('/monitor', '/imu_protocol.js'):
            with self.client.get(url) as response:
                self.assertEqual(response.status_code, 200)


if __name__ == '__main__':
    unittest.main()
```

### 6.46 ไฟล์ `models/pose_model/evaluation.json` — ผลลัพธ์การประเมินโมเดลล่าสุด (evaluation.json) (246 บรรทัด)
```json
{
  "model_version": "pose-svm-rbf-v1",
  "window_seconds": 1.0,
  "sampling_hz": 20,
  "action_windows": 4384,
  "phase_windows": 0,
  "action_labels": [
    "case_extension",
    "case_flexion",
    "case_neutral",
    "elbow_extension",
    "elbow_neutral",
    "hand_neutral"
  ],
  "phase_labels": [],
  "phase_model_available": false,
  "phase_annotation_policy": "New recorder CSV phases require phase_annotation_reviewed=true; legacy CSVs without that column keep explicit phase labels",
  "requires_calibration": false,
  "selected_action_estimator": "svm_rbf",
  "selected_phase_estimator": null,
  "model_selection": {
    "action": "selected by group-separated macro-F1; RF preferred on ties",
    "phase": "grouped evaluation unavailable; RF retained as prototype"
  },
  "evaluation": {
    "action_label": {
      "random_forest": {
        "available": true,
        "grouped_by": "subject_id",
        "fold_count": 4,
        "window_count": 4384,
        "accuracy": 0.527536231884058,
        "macro_f1": 0.11511701454775458,
        "per_class": {
          "case_extension": {
            "precision": 0.0,
            "recall": 0.0,
            "f1": 0.0,
            "support": 0
          },
          "case_flexion": {
            "precision": 0.0,
            "recall": 0.0,
            "f1": 0.0,
            "support": 0
          },
          "case_neutral": {
            "precision": 0.0,
            "recall": 0.0,
            "f1": 0.0,
            "support": 0
          },
          "elbow_extension": {
            "precision": 0.0,
            "recall": 0.0,
            "f1": 0.0,
            "support": 0
          },
          "elbow_neutral": {
            "precision": 0.0,
            "recall": 0.0,
            "f1": 0.0,
            "support": 0
          },
          "hand_neutral": {
            "precision": 1.0,
            "recall": 0.527536231884058,
            "f1": 0.6907020872865275,
            "support": 345
          }
        },
        "confusion_matrix": [
          [
            0,
            0,
            0,
            0,
            0,
            0
          ],
          [
            0,
            0,
            0,
            0,
            0,
            0
          ],
          [
            0,
            0,
            0,
            0,
            0,
            0
          ],
          [
            0,
            0,
            0,
            0,
            0,
            0
          ],
          [
            0,
            0,
            0,
            0,
            0,
            0
          ],
          [
            0,
            0,
            142,
            0,
            21,
            182
          ]
        ],
        "labels": [
          "case_extension",
          "case_flexion",
          "case_neutral",
          "elbow_extension",
          "elbow_neutral",
          "hand_neutral"
        ]
      },
      "svm_rbf": {
        "available": true,
        "grouped_by": "subject_id",
        "fold_count": 4,
        "window_count": 4384,
        "accuracy": 0.5507246376811594,
        "macro_f1": 0.11838006230529595,
        "per_class": {
          "case_extension": {
            "precision": 0.0,
            "recall": 0.0,
            "f1": 0.0,
            "support": 0
          },
          "case_flexion": {
            "precision": 0.0,
            "recall": 0.0,
            "f1": 0.0,
            "support": 0
          },
          "case_neutral": {
            "precision": 0.0,
            "recall": 0.0,
            "f1": 0.0,
            "support": 0
          },
          "elbow_extension": {
            "precision": 0.0,
            "recall": 0.0,
            "f1": 0.0,
            "support": 0
          },
          "elbow_neutral": {
            "precision": 0.0,
            "recall": 0.0,
            "f1": 0.0,
            "support": 0
          },
          "hand_neutral": {
            "precision": 1.0,
            "recall": 0.5507246376811594,
            "f1": 0.7102803738317757,
            "support": 345
          }
        },
        "confusion_matrix": [
          [
            0,
            0,
            0,
            0,
            0,
            0
          ],
          [
            0,
            0,
            0,
            0,
            0,
            0
          ],
          [
            0,
            0,
            0,
            0,
            0,
            0
          ],
          [
            0,
            0,
            0,
            0,
            0,
            0
          ],
          [
            0,
            0,
            0,
            0,
            0,
            0
          ],
          [
            0,
            0,
            137,
            0,
            18,
            190
          ]
        ],
        "labels": [
          "case_extension",
          "case_flexion",
          "case_neutral",
          "elbow_extension",
          "elbow_neutral",
          "hand_neutral"
        ]
      }
    },
    "phase_label": {
      "available": false,
      "reason": "Fewer than two explicitly annotated phase classes; phase output will remain unknown",
      "labels": [],
      "window_count": 0
    }
  },
  "ignored_files": [],
  "limitation": "Scores are not calibrated probabilities. Independent evaluation is unavailable wherever group-separated folds cannot cover all classes."
}
```
