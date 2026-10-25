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
