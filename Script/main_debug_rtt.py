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
            label = command.rsplit(" ", 1)[0] if command.startswith(("fw_data ", "pwr_data ")) else command
            self.log.write(json.dumps(dict(t=round(time.monotonic()-self.started, 3), tx=label, rx=response)) + "\n")
        return response

    def require(self, command, response_type="fw"):
        response = self.call(command)
        if response != f"OK {response_type}":
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


def read_power_version(rtt):
    rtt.require("pwr_version", "pwr")
    deadline = time.monotonic() + 1
    while time.monotonic() < deadline:
        version = fields(rtt.call("pwr_version_status"), "PVER")
        if version["valid"] == "1":
            return {"build": version["build"].lower(), "crc32c": version["crc"].lower()}
        time.sleep(.04)
    raise TimeoutError("電源基板のFW照会応答がありません")


def crc32c(image):
    crc = 0xffffffff
    for byte in image:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0x82f63b78 if crc & 1 else 0)
    return crc ^ 0xffffffff


def update_power(rtt, image):
    if not 0x408 <= len(image) <= 0x1b800 or image[0x400:0x404] != b"FWVR":
        raise ValueError("F303電源基板のスタンプ済みアプリBINを指定してください")
    sp, vector = struct.unpack_from("<II", image)
    build = struct.unpack_from("<I", image, 0x404)[0]
    if not 0x20000000 < sp <= 0x20008000 or sp % 8 or not vector & 1 or not build or not (
            0x08004000 <= vector & ~1 < 0x08004000 + len(image)):
        raise ValueError("電源アプリのベクターまたはビルドIDが無効です")
    crc = crc32c(image)
    rtt.require(f"pwr_begin {len(image)} {crc:08x} {build:08x}", "pwr")
    for at in range(0, len(image), 896):
        block = image[at:at + 896]
        for offset in range(0, len(block), 256):
            rtt.require(f"pwr_data {at + offset} {block[offset:offset + 256].hex()}", "pwr")
        rtt.require("pwr_commit", "pwr")
        deadline = time.monotonic() + 40
        while time.monotonic() < deadline:
            status = fields(rtt.call("pwr_status"), "PWR")
            if status["state"] == "4":
                raise RuntimeError(f"電源FW更新が停止しました {status}")
            if status["state"] == "3" and int(status["written"]) == len(image):
                return {"bytes": len(image), "build": f"{build:08x}", "crc32c": f"{crc:08x}",
                        "verified_after_reboot": True}
            if status["state"] == "1" and int(status["written"]) == at + len(block):
                break
            time.sleep(.04)
        else:
            raise TimeoutError("電源FWのブロック確定が時間内に届きませんでした")
    raise RuntimeError("電源FW更新の完了状態を確認できませんでした")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", type=Path, help="RTT送受信のJSONL保存先")
    jobs = parser.add_subparsers(dest="job", required=True)
    jobs.add_parser("temperature")
    writer = jobs.add_parser("steer-update")
    writer.add_argument("--board", type=int, choices=(0, 1), required=True)
    writer.add_argument("image", type=Path)
    jobs.add_parser("power-version")
    jobs.add_parser("power-update").add_argument("image", type=Path)
    args = parser.parse_args()
    log = None
    try:
        if args.log:
            args.log.parent.mkdir(parents=True, exist_ok=True)
            log = args.log.open("x", encoding="utf-8", buffering=1)
        with socket.create_connection(("127.0.0.1", 19030), timeout=1.5) as link, link.makefile("rb") as reader:
            rtt = Rtt(link, reader, log)
            if args.job == "temperature":
                result = read_temperature(rtt)
            elif args.job == "steer-update":
                result = update(rtt, args.board, args.image.read_bytes())
            elif args.job == "power-version":
                result = read_power_version(rtt)
            else:
                result = update_power(rtt, args.image.read_bytes())
        print(json.dumps(result, ensure_ascii=False, indent=2))
    except (OSError, RuntimeError, ValueError, KeyError) as error:
        parser.exit(1, f"{error}\n")
    finally:
        if log:
            log.close()


if __name__ == "__main__":
    main()
