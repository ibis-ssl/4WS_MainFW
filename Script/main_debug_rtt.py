"""PCからRTTでMainの温度診断とCM4相当のステアFW更新を実行する。"""
import argparse
import hashlib
import json
from pathlib import Path
import socket
import struct
import time
import zlib


def validate_image(image):
    app_address = 0x08004000
    if not 0x208 <= len(image) <= 0x18000:
        raise ValueError("ステアV4のアプリ長が範囲外です")
    sp, vector = struct.unpack_from("<II", image)
    if not 0x20000000 < sp <= 0x20020000 or sp % 8:
        raise ValueError("初期SPが範囲外です")
    if not vector & 1 or not app_address <= vector & ~1 < app_address + len(image):
        raise ValueError("Reset_HandlerがステアV4のアプリ範囲外です")
    if struct.unpack_from("<II", image, 0x200) != (0x34444344, 3):
        raise ValueError("ステアV4 / 更新形式3の識別子がありません")


class Rtt:
    def __init__(self, link, reader, log):
        self.link = link
        self.reader = reader
        self.log = log
        self.started = time.monotonic()

    def call(self, command):
        self.link.sendall((command + "\n").encode("ascii"))
        response = self.reader.readline().decode("ascii").strip()
        if not response:
            raise ConnectionError("MainのRTT応答が途絶しました")
        if self.log:
            label = command.rsplit(" ", 1)[0] if command.startswith("fw_data ") else command
            self.log.write(json.dumps(dict(t=round(time.monotonic()-self.started, 3), tx=label, rx=response)) + "\n")
        return response

    def require(self, command):
        response = self.call(command)
        if response != "OK fw":
            raise RuntimeError(f"{command.split()[0]}: {response}")


def fields(response, prefix):
    if not response.startswith(prefix + " "):
        raise RuntimeError(f"想定外の応答: {response}")
    return dict(part.split("=", 1) for part in response.split()[1:])


def read_temperature(rtt):
    values = fields(rtt.call("temp"), "TEMP")
    seen, valid = (int(values[key], 16) for key in ("seen", "valid"))
    raw = [int(value) for value in values["decic"].split(",")]
    return {"seen": seen, "valid": valid, "decic": raw,
            "celsius": [value / 10 if valid & (1 << i) else None for i, value in enumerate(raw)]}


def update(rtt, board, image):
    validate_image(image)
    main = fields(rtt.call("status"), "MAIN")
    if main["boot"] == "0":
        raise RuntimeError("Mainは周辺基板の起動確認中です")
    crc = zlib.crc32(image)
    rtt.require(f"fw_begin {board} {len(image)} {crc:08x}")
    committed = False
    try:
        for at in range(0, len(image), 256):
            rtt.require(f"fw_data {at} {image[at:at+256].hex()}")
        rtt.require("fw_commit")
        committed = True
        deadline = time.monotonic() + 180
        while time.monotonic() < deadline:
            status = fields(rtt.call("fw_status"), "FW")
            if status["state"] == "4":
                raise RuntimeError(f"Mainが更新失敗を報告しました: {status}")
            if status["state"] == "3":
                if int(status["written"]) != len(image) or int(status["crc"], 16) != crc:
                    raise RuntimeError("Mainの完了応答で書込み長またはCRCが一致しません")
                return {"board": board, "bytes": len(image), "sha256": hashlib.sha256(image).hexdigest(),
                        "crc32": f"{crc:08x}", "previous_crc32": status["old"].lower(),
                        "verified_after_reboot": True}
            time.sleep(.15)
        raise TimeoutError("Mainの完了応答が時間内に届きませんでした")
    finally:
        if not committed:
            # 本転送前に失敗した場合はステージングを解除する。
            try:
                rtt.call("fw_abort")
            except (ConnectionError, OSError):
                pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", type=Path, help="RTT送受信のJSONL保存先")
    jobs = parser.add_subparsers(dest="job", required=True)
    jobs.add_parser("temperature")
    writer = jobs.add_parser("steer-update")
    writer.add_argument("--board", type=int, choices=(0, 1), required=True)
    writer.add_argument("image", type=Path)
    args = parser.parse_args()
    log = None
    try:
        if args.log:
            args.log.parent.mkdir(parents=True, exist_ok=True)
            log = args.log.open("x", encoding="utf-8", buffering=1)
        with socket.create_connection(("127.0.0.1", 19030), timeout=1.5) as link, link.makefile("rb") as reader:
            rtt = Rtt(link, reader, log)
            result = read_temperature(rtt) if args.job == "temperature" else update(rtt, args.board, args.image.read_bytes())
        print(json.dumps(result, ensure_ascii=False, indent=2))
    except (OSError, RuntimeError, ValueError, KeyError) as error:
        parser.exit(1, f"{error}\n")
    finally:
        if log:
            log.close()


if __name__ == "__main__":
    main()
