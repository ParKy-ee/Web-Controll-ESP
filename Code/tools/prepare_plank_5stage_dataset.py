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
