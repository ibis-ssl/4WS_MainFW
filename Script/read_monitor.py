"""書き込み済みELFの診断構造体をSWDで読み、JSONへ保存する。短時間CPUを停止する。"""
import argparse
import json
from pathlib import Path
import shutil
import struct
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--serial', required=True, help='対象ST-Linkのシリアル番号')
parser.add_argument('--elf', default='build/Debug/4WS_MainFW.elf', help='実機へ書き込んだELF')
parser.add_argument('--programmer', help='STM32_Programmer_CLI.exeのパス')
parser.add_argument('--nm', help='arm-none-eabi-nm.exeのパス')
parser.add_argument('--output', default='build/hardware/monitor.json', help='JSONの保存先')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
elf = (root / args.elf).resolve()
nm = args.nm or shutil.which('arm-none-eabi-nm')
programmer = args.programmer or shutil.which('STM32_Programmer_CLI')
if not programmer:
    candidates = sorted(Path('C:/ST').glob('STM32CubeCLT_*/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe'))
    programmer = str(candidates[-1]) if candidates else None
if not nm or not programmer or not elf.is_file():
    parser.error('ELF、Arm nm、CubeProgrammerを確認してください。--nm / --programmerで場所を指定できます。')
symbols = subprocess.check_output([nm, '-S', str(elf)], text=True)
symbol = next((line.split() for line in symbols.splitlines() if line.endswith(' app_monitor_status')), None)
if not symbol:
    parser.error('ELFにapp_monitor_statusがありません。')
address, size = int(symbol[0], 16), int(symbol[1], 16)
fields = ['magic', 'sequence', 'uptime_ms']
for field in ('can_rx', 'can_drop', 'can_lost', 'can_busoff', 'orion_rx', 'last_id'):
    fields.extend(f'{field}_{bus}' for bus in (1, 2))
fields.extend(['imu_who', 'imu_ready', 'imu_valid', 'imu_samples', 'imu_errors'])
fields.extend(f'{field}_{axis}' for field in ('accel_raw', 'gyro_raw') for axis in 'xyz')
fields.extend(['temperature_raw', 'oled_address', 'oled_ready', 'oled_frames', 'oled_errors',
               'sw_valid', 'sw_raw', 'control_calls', 'control_overruns', 'control_max_cycles'])
if size != len(fields) * 4:
    parser.error('診断構造体のサイズが変わっています。スクリプトのフィールド定義を更新してください。')
output = (root / args.output).resolve()
output.parent.mkdir(parents=True, exist_ok=True)
raw = output.with_suffix('.bin')
connection = [programmer, '-c', 'port=SWD', f'sn={args.serial}', 'mode=HOTPLUG', 'freq=1000']
try:
    subprocess.run([*connection, '-halt', '-u', hex(address), str(size), str(raw)], check=True)
finally:
    # 読取りに失敗しても、この検証によるCPU停止を残さないよう再開を試みる。
    subprocess.run([*connection, '-run'], check=True)
values = struct.unpack('<' + 'I' * len(fields), raw.read_bytes())
result = dict(zip(fields, values))
if result['magic'] != 0x34575331 or result['sequence'] % 2:
    parser.error('FWとELFが一致しないか、診断値更新途中です。FWを確認して読取りを再実行してください。')
for field in fields:
    if field.startswith(('accel_raw_', 'gyro_raw_')) or field == 'temperature_raw':
        if result[field] >= 0x80000000:
            result[field] -= 0x100000000
result['elf'] = str(elf)
result['stlink_serial'] = args.serial
result['temperature_c'] = result['temperature_raw'] / 326.8 + 25
result['accel_mps2'] = [result[f'accel_raw_{axis}'] * 9.80665 / 16384 for axis in 'xyz']
result['gyro_dps'] = [result[f'gyro_raw_{axis}'] * 1000 / 32768 for axis in 'xyz']
output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps(result, ensure_ascii=False, indent=2))
