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
