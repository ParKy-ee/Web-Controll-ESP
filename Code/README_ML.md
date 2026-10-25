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
