# Plank phase dataset (single lumbar node)

This dataset was derived from the eight lumbar CSV examples supplied for this task. The sensor measurements are copied from the captures; no synthetic IMU values were added.

## Phase labels

`phase_stage_label` preserves the five chronological stages:

1. `normal_start`
2. `approaching_error`
3. `incorrect_posture_hold`
4. `recovery_to_normal`
5. `normal_end`

`model_phase_label` maps those five stages to the current phase model's four states. Both normal stages map to `normal`; the temporal order distinguishes the start from the end. The original four-class cue is preserved in `phase_label` (`neutral`, `approaching`, `holding`, `returning`).

## Dataset status

- 8 unique lumbar recordings, 2,539 sample rows, 3 session IDs, and one subject.
- The source recorder labels these phases from a guided timer and marks them unreviewed. `phase_annotation_reviewed` therefore remains `false`; the generated labels are provisional until checked against video.
- This is one sensor node. It cannot evaluate the current upper/lower paired Plank Phase Pilot.
- The current ML trainer compares Random Forest and RBF-SVM by grouped macro-F1, but this dataset is not an independent accuracy test. Review the phase annotations first and collect more people and sessions before claiming model accuracy.

## Rebuild

```powershell
python Code\tools\prepare_plank_5stage_dataset.py `
  "C:\Users\GF\Downloads\trial-01_lumbar_lumbar_extension.csv" `
  "C:\Users\GF\Downloads\trial-01_lumbar_lumbar_extension 2.csv" `
  "C:\Users\GF\Downloads\trial-01_lumbar_lumbar_extension 3.csv" `
  "C:\Users\GF\Downloads\trial-01_lumbar_lumbar_extension 5.csv" `
  "C:\Users\GF\Downloads\trial-01_lumbar_lumbar_flexion 2.csv" `
  "C:\Users\GF\Downloads\trial-01_lumbar_lumbar_flexion 3.csv" `
  "C:\Users\GF\Downloads\trial-01_lumbar_lumbar_flexion 4.csv" `
  "C:\Users\GF\Downloads\trial-01_lumbar_lumbar_flexion 5.csv"
```
