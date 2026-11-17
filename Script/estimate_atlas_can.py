"""Atlasの2バスCAN負荷を再現計算する。実機通信を行わず、Classic上限とFD比較用概算を出力する。"""
import argparse
import csv
import math
import sys


def classic_us(payload):
    # 標準11bitデータフレーム、IFS込み、SOF〜CRC列の最大スタッフ数を加算する。
    assert 0 <= payload <= 8
    return 47 + 8 * payload + (33 + 8 * payload) // 4


def fd_us(payload, data_mbps):
    # ISO FDの厳密長ではなく、比較用の余裕を含む時間枠。調停/終端40bit、
    # データ・CRC等50bit、データ側全体に25%のスタッフ余裕を見込む。
    assert payload in (12, 20, 32, 64) and data_mbps in (1, 2, 4)
    return 40 + math.ceil((8 * payload + 50) * 1.25) / data_mbps


def estimate(profile, rate, command_hz, feedback_hz, groups, power_fps,
             mouse_hz, calibration_bytes, configuration_fps):
    fd_rate = {'fd_1m': 1, 'fd_2m': 2, 'fd_4m': 4, 'fd_2m_velocity': 2}.get(profile)
    c610 = (2 * feedback_hz + groups * command_hz) * classic_us(8)
    if fd_rate:
        feedback_size = 32 if profile == 'fd_2m_velocity' else 20
        steering = rate * (fd_us(12, fd_rate) + fd_us(feedback_size, fd_rate))
        periodic = 10 * (classic_us(8) + fd_us(20, fd_rate)) + fd_us(12, fd_rate)
        calibration = 2 * math.ceil(calibration_bytes / 56) * fd_us(64, fd_rate)
    else:
        # 既存案は指令/電流が各4byte、角度+速度が8byte。packedは角度+電流8byte。
        steering = rate * (2 * classic_us(4) +
                           (2 * classic_us(8) if profile == 'classic_packed'
                            else 2 * classic_us(4) + 2 * classic_us(8)))
        periodic = 10 * (classic_us(8) + 2 * classic_us(8)) + 2 * classic_us(4)
        calibration = 2 * math.ceil(calibration_bytes / 4) * classic_us(8)
    configuration = configuration_fps * classic_us(8)
    total = c610 + steering + periodic + calibration + configuration
    power = power_fps * classic_us(8)
    mouse = mouse_hz * classic_us(8)
    return dict(profile=profile, steering_hz=rate, c610_command_hz=command_hz,
                c610_feedback_hz=feedback_hz, c610_groups=groups,
                c610_percent=round(c610 / 10000, 4),
                steering_percent=round(steering / 10000, 4),
                periodic_percent=round(periodic / 10000, 4),
                calibration_percent=round(calibration / 10000, 4),
                configuration_percent=round(configuration / 10000, 4),
                bus_a_percent=round(total / 10000, 4),
                bus_b_power_percent=round((total + power) / 10000, 4),
                bus_b_power_mouse_percent=round((total + power + mouse) / 10000, 4),
                power_assumed_fps=power_fps, mouse_assumed_hz=mouse_hz,
                calibration_bytes_per_axis=calibration_bytes,
                configuration_assumed_fps_per_board=configuration_fps)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--c610-command-hz', type=int, default=500)
    parser.add_argument('--c610-feedback-hz', type=int, default=1000)
    parser.add_argument('--c610-command-groups', type=int, choices=(1, 2), default=1)
    parser.add_argument('--power-fps', type=int, default=90)
    parser.add_argument('--mouse-hz', type=int, default=1000)
    parser.add_argument('--calibration-bytes-per-axis', type=int, default=0)
    parser.add_argument('--configuration-fps', type=int, default=0)
    parser.add_argument('--csv', action='store_true')
    args = parser.parse_args()
    if any(value < 0 for key, value in vars(args).items() if key != 'csv'):
        parser.error('rates and byte counts must be nonnegative')
    rows = [estimate(profile, rate, args.c610_command_hz, args.c610_feedback_hz,
                     args.c610_command_groups, args.power_fps, args.mouse_hz,
                     args.calibration_bytes_per_axis, args.configuration_fps)
            for profile in ('classic_separate', 'classic_packed', 'fd_1m', 'fd_2m',
                            'fd_4m', 'fd_2m_velocity') for rate in (100, 250, 500)]
    if args.csv:
        writer = csv.DictWriter(sys.stdout, fieldnames=rows[0].keys(), lineterminator='\n')
        writer.writeheader()
        writer.writerows(rows)
    else:
        print('profile                Hz     bus A    bus B+power    bus B+power+mouse')
        for row in rows:
            print(f"{row['profile']:22s} {row['steering_hz']:3d} {row['bus_a_percent']:9.2f}%"
                  f" {row['bus_b_power_percent']:12.2f}% {row['bus_b_power_mouse_percent']:18.2f}%")
        print('Unknown calibration/configuration excluded when byte/frame arguments are zero.')
        print('FD coexistence assumed; FD times are engineering estimates, not ISO exact lengths.')


if __name__ == '__main__':
    main()
