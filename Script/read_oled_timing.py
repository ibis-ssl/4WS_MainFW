"""OLEDの成功画面のDWT計測値をSWDで読み取り、時間集計をJSONへ保存する。"""
import argparse
import json
from pathlib import Path
import shutil
import statistics
import struct
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--serial', required=True, help='対象ST-Linkのシリアル番号')
parser.add_argument('--elf', default='build/Debug/4WS_MainFW.elf', help='実機へ書き込んだELF')
parser.add_argument('--programmer', help='STM32_Programmer_CLI.exeのパス')
parser.add_argument('--nm', help='arm-none-eabi-nm.exeのパス')
parser.add_argument('--output', default='build/hardware/oled-timing.json', help='JSONの保存先')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
elf = (root / args.elf).resolve()
nm = args.nm or shutil.which('arm-none-eabi-nm')
programmer = args.programmer or shutil.which('STM32_Programmer_CLI')
if not programmer:
    candidates = sorted(Path('C:/ST').glob('STM32CubeCLT_*/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe'))
    programmer = str(candidates[-1]) if candidates else None
if not nm or not programmer or not elf.is_file():
    parser.error('ELF、Arm nm、CubeProgrammerを確認してください。')
symbols = {}
for line in subprocess.check_output([nm, '-S', str(elf)], text=True).splitlines():
    columns = line.split()
    if len(columns) == 4:
        symbols[columns[3]] = (int(columns[0], 16), int(columns[1], 16))
names = ['oled_timing_count', 'oled_timing_samples', 'SystemCoreClock']
if any(name not in symbols for name in names):
    parser.error('ELFにOLED計測シンボルがありません。計測対応FWをビルド・書込みしてください。')
fields = ['render_cycles', 'transfer_cycles', 'process_cycles', 'frame_cycles',
          'transactions', 'max_transfer_cycles']
stride = len(fields) * 4
capacity = symbols['oled_timing_samples'][1] // stride
if symbols['oled_timing_samples'][1] % stride or capacity != 64:
    parser.error('OLED計測構造体のサイズが変わっています。フィールド定義を更新してください。')
output = (root / args.output).resolve()
output.parent.mkdir(parents=True, exist_ok=True)
raw_files = {name: output.with_name(output.stem + '-' + name + '.bin') for name in names}
connection = [programmer, '-c', 'port=SWD', f'sn={args.serial}', 'mode=HOTPLUG', 'freq=1000']
command = [*connection, '-halt']
for name in names:
    address, size = symbols[name]
    command.extend(['-u', hex(address), str(size), str(raw_files[name])])
try:
    subprocess.run(command, check=True)
finally:
    # 計測は実機内で完了済み。読取り失敗時もCPU停止を残さないよう再開する。
    subprocess.run([*connection, '-run'], check=True)
count, = struct.unpack('<I', raw_files['oled_timing_count'].read_bytes())
clock, = struct.unpack('<I', raw_files['SystemCoreClock'].read_bytes())
if count != capacity or clock == 0:
    parser.error('64画面の保存が未完了です。正常接続で起動後約7秒以上待って再実行してください。')
raw = raw_files['oled_timing_samples'].read_bytes()
samples = [dict(zip(fields, struct.unpack_from('<6I', raw, i * stride))) for i in range(count)]
if any(s['transactions'] != 137 or s['frame_cycles'] < s['process_cycles'] or
       s['process_cycles'] < s['transfer_cycles'] for s in samples):
    parser.error('計測値が不整合です。実機FWと指定ELFの一致を確認してください。')
metrics = {
    'render_ms': lambda s: s['render_cycles'],
    'hal_transfer_sum_ms': lambda s: s['transfer_cycles'],
    'oled_process_sum_ms': lambda s: s['process_cycles'],
    'commit_to_complete_ms': lambda s: s['frame_cycles'],
    'render_to_complete_ms': lambda s: s['render_cycles'] + s['frame_cycles'],
    'non_hal_processing_ms': lambda s: s['render_cycles'] + s['process_cycles'] - s['transfer_cycles'],
    'active_elapsed_sum_ms': lambda s: s['render_cycles'] + s['process_cycles'],
    'interleaved_other_work_ms': lambda s: s['frame_cycles'] - s['process_cycles'],
    'max_single_hal_transfer_ms': lambda s: s['max_transfer_cycles'],
}
summary = {}
for name, metric in metrics.items():
    values = [metric(s) * 1000 / clock for s in samples]
    summary[name] = {'min': min(values), 'mean': statistics.mean(values), 'max': max(values)}
result = {'elf': str(elf), 'stlink_serial': args.serial, 'hclk_hz': clock,
          'sample_count': count, 'transactions_per_frame': 137,
          'note': '経過時間は割込みを含む。HAL送信は完了待機とHAL処理を含み、バス波形の測定ではない。',
          'summary': summary, 'samples_cycles': samples}
output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps({'sample_count': count, 'hclk_hz': clock, 'summary': summary}, ensure_ascii=False, indent=2))
