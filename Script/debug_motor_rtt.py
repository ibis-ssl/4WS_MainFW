"""Mainのモーター試験をRTTで実行し、両CANバスと軸診断を同じ時系列に保存する。"""
import argparse
from collections import Counter
import csv
from datetime import datetime
import json
from pathlib import Path
import secrets
import socket
import struct
import sys
import threading
import time


def capture_can(port, label, offset, start, folder, finish, ready, summary, errors):
    from can import CanError, CanOperationError, Message
    from dual_debug.canrush import CanRushBus

    class CaptureBus(CanRushBus):
        bel = 0

        def _read(self, timeout):
            try:
                return super()._read(timeout)
            except CanOperationError as error:
                if "BEL" not in str(error.__cause__):
                    raise
                self.bel += 1
                return None

    counts = Counter()
    fault_frames = []
    current_counts = Counter()
    rpm = {}
    try:
        with (
            CaptureBus(port, timeout=.006) as bus,
            (folder / f"{label}.csv").open("w", newline="", encoding="utf-8") as frames,
            (folder / f"{label}-axis.jsonl").open("w", encoding="utf-8", buffering=1) as axes,
        ):
            records = csv.writer(frames)
            records.writerow(["elapsed_s", "id", "dlc", "data", "direction"])
            sequence = secrets.randbelow(256)
            fields = [(1, 0), (1, 1), (1, 5), (1, 12), (1, 2), (1, 9),
                      (0, 0), (0, 1), (0, 5), (0, 12)]
            index = 0
            pending = None
            next_read = start
            ready.set()
            while not finish.is_set():
                now = time.monotonic()
                # 保守CANはID 0のAXIS読取りだけ。駆動指令はすべてMainから送る。
                if offset is not None and now >= next_read and (pending is None or now - pending[3] >= .080):
                    axis, field = fields[index % len(fields)]
                    index += 1
                    sequence = (sequence + 1) % 256
                    data = struct.pack("<BBBBI", 0xD4, 10, 0xFA | axis, sequence, field)
                    bus.send(Message(arbitration_id=0x5D0 + offset, is_extended_id=False, data=data), timeout=.02)
                    pending = (axis, field, sequence, now)
                    next_read = now + .025
                    records.writerow([f"{now-start:.6f}", f"{0x5D0 + offset:03X}", 8, data.hex(), "tx"])

                frame = bus.recv(.010)
                if frame is None:
                    continue
                elapsed = time.monotonic() - start
                ident, data = frame.arbitration_id, bytes(frame.data)
                counts[f"{ident:03X}"] += 1
                records.writerow([f"{elapsed:.6f}", f"{ident:03X}", frame.dlc, data.hex(), "rx"])
                if frame.dlc != 8:
                    continue
                if ident == 0x000:
                    source, code = struct.unpack_from("<HH", data)
                    fault_frames.append({"t": elapsed, "source": source, "code": code, "data": data.hex()})
                if ident == 0x200:
                    current_counts["nonzero" if any(data) else "zero"] += 1
                if 0x201 <= ident <= 0x204:
                    speed, = struct.unpack_from(">h", data, 2)
                    low, high = rpm.setdefault(f"{ident:03X}", [speed, speed])
                    rpm[f"{ident:03X}"] = [min(low, speed), max(high, speed)]
                if pending and ident == 0x5D1 + offset:
                    axis, field, seq, _ = pending
                    if data[:4] == bytes((0xD4, 0x8A, 0xFA | axis, seq)):
                        value, = struct.unpack_from("<i", data, 4)
                        axes.write(json.dumps(dict(t=elapsed, axis=axis, field=field, value=value)) + "\n")
                        pending = None
            summary[label] = dict(port=port, frames=counts, fault=fault_frames, drive=current_counts,
                                  rpm_range=rpm, adapter_bel=bus.bel)
    except (OSError, CanError, ValueError) as error:
        errors[label] = str(error)
        finish.set()


class Rtt:
    def __init__(self, link, log, start):
        self.link = link
        self.reader = link.makefile("rb")
        self.log = log
        self.start = start

    def _record(self, direction, text):
        self.log.write(json.dumps({"t": time.monotonic() - self.start, direction: text}) + "\n")

    def send(self, command, response=True):
        self._record("tx", command)
        self.link.sendall((command + "\n").encode("ascii"))
        if response:
            line = self.reader.readline().decode("ascii").strip()
            if not line:
                raise ConnectionError("RTTの応答が途絶しました")
            self._record("rx", line)
            return line
        return None

    def status(self):
        line = self.send("status")
        if not line.startswith("MAIN "):
            raise ValueError(f"状態応答が不正です: {line}")
        return dict(part.split("=", 1) for part in line.split()[1:])


def ready_for(status, action):
    if status["mode"] != "1" or status["state"] != "2":
        return False
    stopped = all(abs(int(rpm)) <= 40 for rpm in status["rpm"].split(","))
    return stopped and (action == "init" or (status["cal"] == "F" and status["esc"] == "F"))


def run_trial(rtt, action, finish):
    ready_deadline = time.monotonic() + 5
    status = rtt.status()
    while not ready_for(status, action) and status["state"] != "7" and time.monotonic() < ready_deadline:
        if finish.wait(.10):
            return {"action": action, "result": "capture_lost", "last": status}
        status = rtt.status()
    if finish.is_set():
        return {"action": action, "result": "capture_lost", "last": status}
    if not ready_for(status, action):
        return {"action": action, "result": "precondition", "last": status}
    response = rtt.send(action)
    active = "5" if action == "init" else "4"
    if response != ("OK init" if action == "init" else "OK drive"):
        return {"action": action, "result": "rejected", "response": response, "last": status}

    result = "timeout"
    tracking_rpm = []
    deadline = time.monotonic() + (31 if action == "init" else 4)
    while time.monotonic() < deadline and not finish.is_set():
        if action == "init":
            rtt.send("keep", response=False)
        status = rtt.status()
        if status["state"] != active:
            if status["state"] == "7":
                result = "fault"
            elif action == "init":
                result = "calibrated" if status["cal"] == "F" else "stopped"
            else:
                sign = 1 if action == "drive+" else -1
                result = "drive_complete" if all(
                    any(sign * sample[wheel] > 40 for sample in tracking_rpm)
                    for wheel in range(4)
                ) else "speed_unverified"
            break
        tracking_rpm.append([int(rpm) for rpm in status["rpm"].split(",")])
        finish.wait(.08)
    if finish.is_set():
        result = "capture_lost"
    return {"action": action, "result": result, "last": status, "rpm_samples": len(tracking_rpm)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("init", "drive+", "drive-", "sequence"))
    parser.add_argument("--can1", required=True)
    parser.add_argument("--can2", required=True)
    parser.add_argument("--steer-repo", required=True, type=Path, help="CANRushアダプター実装を含むステアFWの作業コピー")
    parser.add_argument("--id0-offset", type=int, choices=(0, 16), default=16)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    tools = args.steer_repo.resolve() / "Tool"
    if not (tools / "dual_debug" / "canrush.py").is_file():
        parser.error("ステアFWの Tool/dual_debug/canrush.py を確認してください")
    if args.can1 == args.can2:
        parser.error("CAN1とCAN2には別のポートを指定してください")
    sys.path.insert(0, str(tools))
    from dual_debug.canrush import CanRushBus  # noqa: F401

    folder = args.output or Path("build/hardware") / ("motor-rtt-" + datetime.now().strftime("%Y%m%d-%H%M%S"))
    folder.mkdir(parents=True)
    start = time.monotonic()
    finish = threading.Event()
    errors, can_summary = {}, {}
    trials = []
    sensors = []
    for port, label, offset in ((args.can1, "CAN1", args.id0_offset), (args.can2, "CAN2", None)):
        ready = threading.Event()
        worker = threading.Thread(target=capture_can, args=(port, label, offset, start, folder, finish, ready, can_summary, errors))
        worker.start()
        sensors.append((worker, ready))
    try:
        if not all(ready.wait(2) for _, ready in sensors) or finish.wait(.4):
            raise RuntimeError("両CANバスの記録を開始できませんでした")
        with socket.create_connection(("127.0.0.1", 19030), timeout=.4) as link, (
            folder / "main-rtt.jsonl"
        ).open("w", encoding="utf-8", buffering=1) as log:
            rtt = Rtt(link, log, start)
            try:
                actions = ("init", "drive+", "drive-") if args.action == "sequence" else (args.action,)
                for action in actions:
                    result = run_trial(rtt, action, finish)
                    trials.append(result)
                    if result["result"] not in ("calibrated", "drive_complete"):
                        break
            finally:
                try:
                    rtt.send("stop")
                    time.sleep(.3)
                    rtt.status()
                finally:
                    rtt.reader.close()
    except (OSError, RuntimeError, ValueError) as error:
        errors["trial"] = str(error)
    finally:
        finish.set()
        for worker, _ in sensors:
            worker.join()
        summary = dict(trials=trials, can=can_summary, errors=errors)
        (folder / "summary.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        print(folder.resolve())
        print(json.dumps(summary, indent=2, ensure_ascii=False))
    return int(bool(errors) or not trials or any(
        trial["result"] not in ("calibrated", "drive_complete") for trial in trials
    ))


if __name__ == "__main__":
    raise SystemExit(main())
