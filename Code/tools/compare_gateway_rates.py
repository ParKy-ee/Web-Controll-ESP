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
