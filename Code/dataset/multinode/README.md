# Multi-node IMU training archive

This folder is the canonical input for `tools/train_model.py` when no CSV paths
are supplied. Keep each capture's per-node CSV files and manifest together.

## Current archive

- Captures from 2026-11-01 for participants P01, P02, and P03.
- 18 per-node CSV files and 9 capture manifests.
- Labels: `case_neutral`, `case_flexion`, and `case_extension`.
- P01 S02 and S03 use `trial_id=trial-01` in this archive, per the dataset
  correction. The original files in Downloads were left unchanged.

Add future paired-node recordings here with unique participant and session IDs.
Review action and phase annotations before treating them as ground truth.
