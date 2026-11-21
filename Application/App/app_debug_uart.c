/* Orion形式の診断ページをUART4に出す。表示用の読取りは機器の制御状態を変更しない。 */
#include "App/app_debug_uart.h"
#include "App/app_board_reset.h"
#include "App/app_monitor.h"
#include "App/app_motor_debug.h"
#include "Board/board_control_timer.h"
#include "Board/board_debug_uart.h"
#include "Board/board_gpio.h"
#include "Board/board_user_adc.h"
#include "Device/imu.h"
#include "Device/power_board.h"
#include "Device/user_switch.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>

typedef enum {
    CMD, MOTOR, DRIBBLER, KICKER, MOUSE, ODOM, MOTION, VEL, LATENCY, SYSTEM, UART_RAW, PAGE_COUNT
} page_t;
static const char *const names[] = {
    "CMD", "MOTOR", "DRIBBLER", "KICKER", "MOUSE", "ODOM", "MOTION", "VEL", "LATENCY", "SYSTEM", "UART RAW",
};
enum { REPORT_MS = 100, POWER_FRESH_MS = 2500 };
static page_t page;
static bool banner_pending, menu_pending, previous_cr;
static uint32_t report_ms;
static char text[1024];
static size_t used;

static void append(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void append(const char *format, ...)
{
    if (used >= sizeof(text)) { return; }
    va_list args;
    va_start(args, format);
    int length = vsnprintf(text + used, sizeof(text) - used, format, args);
    va_end(args);
    used = length < 0 || (size_t)length >= sizeof(text) - used ? sizeof(text) : used + (size_t)length;
}

static void read_keys(void)
{
    uint8_t key;
    for (unsigned int n = 0; n < 32U && board_debug_uart_read(&key); ++n) {
        page_t next = page;
        if (key >= '0' && key <= '9') { next = (page_t)(key - '0'); }
        else if (key == '\r' || (key == '\n' && !previous_cr)) { next = (page + 1U) % PAGE_COUNT; }
        else if (key == '\b') { next = (page + PAGE_COUNT - 1U) % PAGE_COUNT; }
        else if (key == 0x7FU) { next = CMD; }
        if (next != page || key == '?') { menu_pending = true; }
        page = next;
        previous_cr = key == '\r';
    }
}

static void voltage(float volts, bool received, uint32_t age)
{
    if (!received || age >= POWER_FRESH_MS) { append("%s", received ? "stale" : "--"); return; }
    if (volts >= 10000.0f) { append("range"); return; }
    unsigned int decivolts = (unsigned int)lroundf(volts * 10.0f);
    append("%u.%uV", decivolts / 10U, decivolts % 10U);
}

static void age_ms(uint32_t age)
{
    if (age == UINT32_MAX) { append("--"); }
    else { append("%lu", (unsigned long)age); }
}

static void measurement(int value, bool valid, uint32_t age)
{
    if (valid) { append("%+d", value); }
    else { append("%s", age == UINT32_MAX ? "--" : "n/a"); }
}

static const char *motor_state(motor_debug_state_t state)
{
    static const char *const states[] = {"OFF", "WAIT", "READY", "STEER", "DRIVE", "INIT", "STOP", "FAULT"};
    return states[state];
}

static void motor(uint32_t now)
{
    motor_debug_status_t data = app_motor_debug_snapshot(now);
    append("Mode=%u State=%s Tar_rpm=%+d", (unsigned)data.mode_active, motor_state(data.state), data.target_rpm);
    for (unsigned int g = 0; g < 4U; ++g) {
        const motor_debug_axis_status_t *a = &data.axes[g];
        append(" / g%u C610=%u rpm=", g, g + 1U);
        measurement(a->drive_rpm, a->drive_fresh, a->drive_age_ms);
        append(" Iraw="); measurement(a->drive_raw_current, a->drive_fresh, a->drive_age_ms);
        append(" Cmd_mA=%+d Age_ms=", a->drive_command_ma); age_ms(a->drive_age_ms);
    }
}

static void motion(uint32_t now)
{
    motor_debug_status_t data = app_motor_debug_snapshot(now);
    steer_temperature_t temp = app_motor_debug_temperature(now);
    append("STEER Mode=%u State=%s", (unsigned)data.mode_active, motor_state(data.state));
    for (unsigned int g = 0; g < 4U; ++g) {
        const motor_debug_axis_status_t *a = &data.axes[g];
        append(" / g%u Tar_mrad=", g);
        measurement(a->steer_target_mrad, a->steer_target_valid, a->steer_age_ms);
        append(" Pos_mrad=");
        measurement(a->steer_angle_mrad, a->steer_fresh && a->steer_angle_mrad != INT16_MIN, a->steer_age_ms);
        append(" Iabs_mA=");
        measurement(a->steer_current_ma, a->steer_fresh && a->steer_current_ma != UINT16_MAX, a->steer_age_ms);
        append(" Cal="); measurement(a->steer_initialized, a->cal_fresh, a->cal_age_ms);
        append(" St="); measurement(a->steer_state, a->cal_fresh, a->cal_age_ms);
        append(" Temp_dC=");
        measurement(temp.decic[g], (temp.valid_mask & (1U << g)) != 0U,
                    (temp.seen_mask & (1U << g)) != 0U ? 0U : UINT32_MAX);
        append(" Age_ms="); age_ms(a->steer_age_ms);
    }
}

static void odom(void)
{
    imu_status_t imu;
    (void)imu_get_status(&imu);
    append("IMU who=%02X ready=%u valid=%u samples=%lu err=%lu", (unsigned)imu.who_am_i,
           (unsigned)imu.ready, (unsigned)imu.valid, (unsigned long)imu.samples, (unsigned long)imu.errors);
    if (imu.valid) {
        append(" Acc_raw=%+d,%+d,%+d Gyro_raw=%+d,%+d,%+d Temp_dC=%+ld", imu.raw_accel[0], imu.raw_accel[1],
               imu.raw_accel[2], imu.raw_gyro[0], imu.raw_gyro[1], imu.raw_gyro[2], lroundf(imu.temperature_c * 10.0f));
    }
    append(" Yaw=n/a Odom=n/a");
}

static void system_page(void)
{
    board_control_timer_stats_t control;
    board_control_timer_get_stats(&control);
    user_switch_t key = user_switch_decode((uint16_t)app_monitor_status.sw_raw, app_monitor_status.sw_valid != 0U);
    append("SW valid=%lu raw=%lu key_mask=%02X ADC errors=%lu DIP=", (unsigned long)app_monitor_status.sw_valid,
           (unsigned long)app_monitor_status.sw_raw, (unsigned)key, (unsigned long)board_user_adc_error_count());
    for (unsigned int i = 0; i < 4U; ++i) {
        bool high;
        append("%c", board_gpio_read_input((board_gpio_input_t)(BOARD_GPIO_INPUT_DIP_0 + i), &high) ? (high ? '1' : '0') : '?');
    }
    for (unsigned int bus = 0; bus < BOARD_CAN_COUNT; ++bus) {
        board_can_stats_t s;
        (void)board_can_get_stats((board_can_bus_t)bus, &s);
        append(" / CAN%u rx=%lu drop=%lu lost=%lu busoff=%lu tx=%lu err=%lu buffered=%lu tx_drop=%lu", bus + 1U,
               (unsigned long)s.rx_received, (unsigned long)s.rx_dropped, (unsigned long)s.rx_lost,
               (unsigned long)s.bus_off_events, (unsigned long)s.tx_submitted, (unsigned long)s.tx_errors,
               (unsigned long)s.tx_buffered, (unsigned long)s.tx_dropped);
    }
    append(" / CTRL calls=%lu overrun=%lu max_cycles=%lu delay_us=%lu / OLED addr=%02lX frames=%lu err=%lu",
           (unsigned long)control.calls, (unsigned long)control.overruns, (unsigned long)control.max_execution_cycles,
           (unsigned long)control.max_entry_delay_us, (unsigned long)app_monitor_status.oled_address,
           (unsigned long)app_monitor_status.oled_frames, (unsigned long)app_monitor_status.oled_errors);
}

static void kicker(const power_board_status_t *power, uint32_t now)
{
    append("Batt(Pw)="); voltage(power->battery_voltage, power->battery_valid, now - power->battery_ms);
    append(" Cap="); voltage(power->capacitor_voltage, power->capacitor_valid, now - power->capacitor_ms);
    if (power->temperature_valid && now - power->temperature_ms < POWER_FRESH_MS) {
        append(" FET=%uC L1=%uC L2=%uC", (unsigned)power->fet_temp, (unsigned)power->coil_temp[0], (unsigned)power->coil_temp[1]);
    } else { append(" FET/L1/L2=%s", power->temperature_valid ? "stale" : "--"); }
    append(" Kick=n/a");
}

static void uart_raw(void)
{
    board_debug_uart_stats_t s;
    board_debug_uart_get_stats(&s);
    append("CM4_SPI=n/a / UART4 tx=%lu rejected=%lu tx_err=%lu rx_drop=%lu rx_err=%lu",
           (unsigned long)s.tx_completed, (unsigned long)s.tx_rejected, (unsigned long)s.tx_errors,
           (unsigned long)s.rx_dropped, (unsigned long)s.rx_errors);
}

void app_debug_uart_init(uint32_t now)
{
    page = SYSTEM;
    banner_pending = menu_pending = true;
    previous_cr = false;
    report_ms = now;
}

void app_debug_uart_process(uint32_t now)
{
    read_keys();
    uint32_t elapsed = now - report_ms;
    if (elapsed < REPORT_MS) { return; }
    /* 位相を維持し、遅れた分を連続送信しない。 */
    report_ms += (elapsed / REPORT_MS) * REPORT_MS;
    if (!board_debug_uart_ready()) { return; }
    used = 0;
    if (banner_pending) {
        append("\x1b[0m4ws main start [%s] [%s] UART4 2000000 8N1 CAN1/2 started\r\n", __DATE__, __TIME__);
        append("IMU who=%02lX ready=%lu OLED ready=%lu\r\n", (unsigned long)app_monitor_status.imu_who,
               (unsigned long)app_monitor_status.imu_ready, (unsigned long)app_monitor_status.oled_ready);
    }
    if (menu_pending) {
        for (unsigned int i = 0; i < PAGE_COUNT; ++i) {
            append(i == (unsigned int)page ? "\x1b[1;32m %u:%s \x1b[0m" : " %u:%s ", i, names[i]);
        }
        append("\r\n");
    }
    power_board_status_t power;
    (void)power_board_get_status(BOARD_CAN_1, &power);
    static const char *const reset[] = {"PENDING", "READY", "FAILED"};
    motor_debug_state_t state = app_motor_debug_status();
    append("Batt="); voltage(power.battery_voltage, power.battery_valid, now - power.battery_ms);
    append(" Boot=%s", reset[app_board_reset_status()]);
    if (state == MOTOR_DEBUG_FAULT) { append(" \x1b[31mErr=FAULT\x1b[0m"); }
    append(" %s ", names[page]);
    switch (page) {
    case MOTOR: motor(now); break;
    case KICKER: kicker(&power, now); break;
    case ODOM: odom(); break;
    case MOTION: motion(now); break;
    case SYSTEM: system_page(); break;
    case UART_RAW: uart_raw(); break;
    case CMD: append("CM4_SPI=n/a"); break;
    case LATENCY: append("CM4_SPI=n/a CAM=n/a"); break;
    case VEL: append("Chassis=n/a C610=page1"); break;
    default: append("n/a"); break;
    }
    append("\r\n");
    if (used < sizeof(text) && board_debug_uart_write(text, used)) { banner_pending = menu_pending = false; }
}
