"""CANRushのフレームCSVからAtlas/C610の割当、DLC、周期、帰還を確認する。"""
import argparse
from collections import Counter, defaultdict
import csv
from decimal import Decimal, InvalidOperation
import json
from math import ceil
from pathlib import Path
import struct


# DLC、所属基板ID（両バス共通はNone）、名称。
FRAMES = {0x000: (8, None, '共通エラー'), 0x200: (8, None, 'C610電流指令')}
for board in (0, 1):
    for base, length, label in (
        (0x120, 8, '角度指令'), (0x130, 8, 'Duty指令'),
        (0x230, 8, '角度・電流帰還'), (0x290, 4, '温度'),
        (0x350, 2, '基板制御'), (0x510, 8, '校正・軸状態'),
    ):
        FRAMES[base + 2 * board] = (length, board, label)
    for esc_id in (2 * board + 1, 2 * board + 2):
        FRAMES[0x200 + esc_id] = (8, board, f'C610 ID {esc_id}帰還')


def decode_last(identifier, payload):
    if 0x201 <= identifier <= 0x204:
        angle, rpm, current = struct.unpack_from('>Hhh', payload)
        return dict(rotor_angle_raw=angle, motor_rpm=rpm, current_raw=current, valid_angle=angle <= 8191)
    if identifier == 0x200:
        return dict(command_a=[value / 1000 for value in struct.unpack('>4h', payload)])
    if identifier in (0x230, 0x232):
        return [dict(angle_rad=None if angle == -32768 else angle / 1000,
                     current_a=None if current == 65535 else current / 1000)
                for angle, current in struct.iter_unpack('<hH', payload)]
    if identifier in (0x510, 0x512):
        return [dict(initial_angle_rad=None if angle == -32768 else angle / 1000,
                     flags=flags, state=state)
                for angle, flags, state in struct.iter_unpack('<hBB', payload)]
    return None


def timing(timestamps):
    timestamps.sort()
    gaps = sorted(b - a for a, b in zip(timestamps, timestamps[1:]))
    span = timestamps[-1] - timestamps[0]
    return dict(
        count=len(timestamps), rate_hz=round(float(len(gaps) * 1000 / span), 2) if span else None,
        p99_interval_ms=float(gaps[ceil(.99 * len(gaps)) - 1]) if gaps else None,
        max_interval_ms=float(gaps[-1]) if gaps else None,
        gaps_ge_50ms=sum(gap >= 50 for gap in gaps),
    )


def analyze(stream):
    reader = csv.DictReader(stream)
    required = {'timestamp_host', 'bus', 'id', 'id_format', 'frame_format', 'frame_type',
                'dlc', 'data_length', 'data_hex'}
    if not required <= set(reader.fieldnames or ()):
        raise ValueError('CANRushのフレームCSVを指定してください')
    timestamps = defaultdict(list)
    last = {}
    rejected = Counter()
    for row in reader:
        if (row['id_format'], row['frame_format'], row['frame_type']) != ('standard', 'classic', 'data'):
            rejected['other_frame_format'] += 1
            continue
        try:
            identifier = int(row['id'], 16)
            timestamp = Decimal(row['timestamp_host']) * 1000
            dlc = int(row['dlc'])
            length = int(row['data_length'])
            payload = bytes.fromhex(row['data_hex'])
            bus = (row['bus'] or '').strip()
            if not bus or not timestamp.is_finite() or timestamp < 0 or not 0 <= identifier <= 0x7FF:
                raise ValueError('invalid frame')
            if not 0 <= length <= 8 or dlc != length or len(payload) != length:
                raise ValueError('invalid length')
        except (TypeError, ValueError, InvalidOperation):
            rejected['invalid_row'] += 1
            continue
        key = (bus, identifier, dlc)
        timestamps[key].append(timestamp)
        if key not in last or timestamp >= last[key][0]:
            last[key] = (timestamp, payload)

    board_buses = {
        board: {bus for bus, identifier, length in timestamps
                if identifier == 0x230 + 2 * board and length == 8}
        for board in (0, 1)
    }
    buses = {}
    for (bus, identifier, length), times in sorted(timestamps.items()):
        expected = FRAMES.get(identifier)
        item = dict(id=f'0x{identifier:03X}', dlc=length, **timing(times))
        if expected:
            expected_dlc, board, label = expected
            item.update(label=label, dlc_matches=length == expected_dlc)
            if board is not None and len(board_buses[board]) == 1:
                item['bus_matches'] = bus in board_buses[board]
            if item['dlc_matches']:
                decoded = decode_last(identifier, last[bus, identifier, length][1])
                if decoded is not None:
                    item['last'] = decoded
        buses.setdefault(bus, []).append(item)

    missing = {}
    for board, candidates in board_buses.items():
        if len(candidates) == 1:
            bus, = candidates
            expected_ids = (0x230 + 2 * board, 0x290 + 2 * board, 0x510 + 2 * board,
                            0x201 + 2 * board, 0x202 + 2 * board)
            missing[str(board)] = [f'0x{identifier:03X}' for identifier in expected_ids
                                  if (bus, identifier, FRAMES[identifier][0]) not in timestamps]
    return dict(board_buses={str(board): sorted(ports) for board, ports in board_buses.items()},
                shared_board_bus=sorted(board_buses[0] & board_buses[1]),
                missing_periodic_ids=missing, rejected_rows=dict(rejected), buses=buses)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path, help='CANRushのフレームCSV')
    parser.add_argument('--output', type=Path, help='結果JSONの保存先。省略時は標準出力')
    args = parser.parse_args()
    try:
        with args.capture.open(encoding='utf-8-sig', newline='') as stream:
            rendered = json.dumps(analyze(stream), ensure_ascii=False, indent=2) + '\n'
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(rendered, encoding='utf-8')
        else:
            print(rendered, end='')
    except (OSError, ValueError, csv.Error) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
