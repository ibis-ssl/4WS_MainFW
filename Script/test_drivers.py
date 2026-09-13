"""実ドライバをHALモックと組み合わせてホスト上で検証する。実機には接続しない。"""
import argparse
from pathlib import Path
import shutil
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cc', help='ホスト用clang/gccの実行ファイル。省略時はclangを検索')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
compiler = args.cc or shutil.which('clang')
if not compiler:
    candidate = Path('C:/Program Files/LLVM/bin/clang.exe')
    if candidate.exists():
        compiler = str(candidate)
if not compiler:
    parser.error('ホスト用コンパイラーが見つかりません。--ccで指定してください。')
out = root / 'build' / 'host-tests'
out.mkdir(parents=True, exist_ok=True)
sources = [
    'Tests/test_drivers.c', 'Application/Board/board_can.c',
    'Application/Board/board_debug_uart.c', 'Application/Board/board_user_adc.c',
    'Application/Board/board_pwm_math.c', 'Application/Board/board_buzzer.c',
    'Application/Board/board_time.c', 'Application/Device/buzzer.c',
    'Application/Device/user_switch.c', 'Application/Device/user_switch_board.c',
    'Application/Protocol/power_packet.c', 'Application/Device/power_board.c',
    'Application/Board/board_control_timer.c', 'Application/App/app_control.c', 'Application/App/app.c',
]
exe = out / 'test_drivers.exe'
subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
    '-I', str(root / 'Tests/mocks'), '-I', str(root / 'Application'),
    *[str(root / source) for source in sources], '-o', str(exe)], check=True, cwd=out)
subprocess.run([str(exe)], check=True, cwd=out)
subprocess.run([str(exe), 'app'], check=True, cwd=out)
