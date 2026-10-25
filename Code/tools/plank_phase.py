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
