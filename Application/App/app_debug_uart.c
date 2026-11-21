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
enum { REPORT_MS = 100, POWER_FRESH_MS = 2500, LINE_MAX = 198 };
static page_t page;
static bool banner_pending, menu_pending, previous_cr;
static uint32_t report_ms;
static char text[1024];
static size_t used, line_start;

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

static void end_line(void)
{
    /* CRLFを含め200 byte。列が欠けたログを送信しない。 */
    if (used - line_start > LINE_MAX) { used = sizeof(text); return; }
    append("\r\n");
    line_start = used;
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

static const char *power_marker(bool received, uint32_t age)
{
    if (!received) { return "--"; }
    return age >= POWER_FRESH_MS ? "stale" : NULL;
}

static void decimal(float value, bool signed_value, int width, const char *marker)
{
    if (!marker && fabsf(value) >= 10000.0f) { marker = "range"; }
    if (marker) { append("%*s", width, marker); return; }
    long tenths = lroundf(value * 10.0f);
    const char *sign = tenths < 0 ? "-" : signed_value ? "+" : "";
    unsigned long magnitude = (unsigned long)(tenths < 0L ? -tenths : tenths);
    char number[16];
    int length = snprintf(number, sizeof(number), "%s%lu.%lu", sign, magnitude / 10U, magnitude % 10U);
    append("%*s", width, length > 0 && length <= width ? number : "range");
}

static void age_ms(uint32_t age)
{
    if (age == UINT32_MAX) { append("%3s", "--"); }
    else if (age > 999U) { append("1k+"); }
    else { append("%3lu", (unsigned long)age); }
}

static void measurement(int value, bool valid, uint32_t age, int width)
{
    if (valid) { append("%+*d", width, value); }
    else { append("%*s", width, age == UINT32_MAX ? "--" : "n/a"); }
}

static const char *motor_state(motor_debug_state_t state)
{
    static const char *const states[] = {"OFF", "WAIT", "READY", "STEER", "DRIVE", "INIT", "STOP", "FAULT"};
    return states[state];
}

static void motor(uint32_t now)
{
    motor_debug_status_t data = app_motor_debug_snapshot(now);
    append("M=%u %-5s TG=%+4d R[rpm]=", (unsigned)data.mode_active, motor_state(data.state), data.target_rpm);
    for (unsigned int g = 0; g < 4U; ++g) {
        const motor_debug_axis_status_t *a = &data.axes[g];
        append(g ? "," : "");
        measurement(a->drive_rpm, a->drive_fresh, a->drive_age_ms, 6);
    }
    append(" Ir=");
    for (unsigned int g = 0; g < 4U; ++g) {
        const motor_debug_axis_status_t *a = &data.axes[g];
        append(g ? "," : "");
        measurement(a->drive_raw_current, a->drive_fresh, a->drive_age_ms, 6);
    }
    append(" C[mA]=");
    for (unsigned int g = 0; g < 4U; ++g) { append(g ? ",%+4d" : "%+4d", data.axes[g].drive_command_ma); }
    append(" A=");
    for (unsigned int g = 0; g < 4U; ++g) { if (g) { append(","); } age_ms(data.axes[g].drive_age_ms); }
}

static void motion_angles(const motor_debug_status_t *data)
{
    append("T[mrad]=");
    for (unsigned int g = 0; g < 4U; ++g) {
        const motor_debug_axis_status_t *a = &data->axes[g];
        if (g) { append(","); }
        measurement(a->steer_target_mrad, a->steer_target_valid, a->steer_age_ms, 6);
    }
    append(" P[mrad]=");
    for (unsigned int g = 0; g < 4U; ++g) {
        const motor_debug_axis_status_t *a = &data->axes[g];
        if (g) { append(","); }
        measurement(a->steer_angle_mrad, a->steer_fresh && a->steer_angle_mrad != INT16_MIN, a->steer_age_ms, 6);
    }
}

static void motion(uint32_t now)
{
    motor_debug_status_t data = app_motor_debug_snapshot(now);
    steer_temperature_t temp = app_motor_debug_temperature(now);
    append("M=%u %-5s ", (unsigned)data.mode_active, motor_state(data.state));
    motion_angles(&data);
    append(" I=");
    for (unsigned int g = 0; g < 4U; ++g) {
        const motor_debug_axis_status_t *a = &data.axes[g];
        if (g) { append(","); }
        if (a->steer_fresh && a->steer_current_ma < 10000U) { append("%4u", (unsigned)a->steer_current_ma); }
        else { append("%4s", a->steer_age_ms == UINT32_MAX ? "--" : "n/a"); }
    }
    append(" C=");
    for (unsigned int g = 0; g < 4U; ++g) {
        const motor_debug_axis_status_t *a = &data.axes[g];
        append("%c", a->cal_fresh ? (a->steer_initialized ? '1' : '0') : '-');
    }
    append(" S=");
    for (unsigned int g = 0; g < 4U; ++g) {
        const motor_debug_axis_status_t *a = &data.axes[g];
        append("%c", a->cal_fresh && a->steer_state <= 9U ? '0' + a->steer_state : '-');
    }
    append(" H=");
    for (unsigned int g = 0; g < 4U; ++g) {
        if (g) { append(","); }
        bool valid = (temp.valid_mask & (1U << g)) != 0U && temp.decic[g] >= -9999 && temp.decic[g] <= 9999;
        measurement(temp.decic[g], valid,
                    (temp.seen_mask & (1U << g)) != 0U ? 0U : UINT32_MAX, 5);
    }
    append(" A=");
    for (unsigned int g = 0; g < 4U; ++g) { if (g) { append(","); } age_ms(data.axes[g].steer_age_ms); }
}

static void odom(void)
{
    imu_status_t imu;
    (void)imu_get_status(&imu);
    append("IMU ID=%2X R=%u V=%u N=%10lu E=%10lu", (unsigned)imu.who_am_i,
           (unsigned)imu.ready, (unsigned)imu.valid, (unsigned long)imu.samples, (unsigned long)imu.errors);
    append(" Acc=");
    for (unsigned int i = 0; i < 3U; ++i) {
        if (i != 0U) { append(","); }
        measurement(imu.raw_accel[i], imu.valid, UINT32_MAX, 6);
    }
    append(" Gyr=");
    for (unsigned int i = 0; i < 3U; ++i) {
        if (i != 0U) { append(","); }
        measurement(imu.raw_gyro[i], imu.valid, UINT32_MAX, 6);
    }
    append(" H="); measurement((int)lroundf(imu.temperature_c * 10.0f), imu.valid, UINT32_MAX, 5);
    append(" Yaw=n/a Odom=n/a");
}

static void counter(uint32_t value)
{
    /* 常用範囲は整数で表示し、桁あふれ時も累積カウンターの列幅を保つ。 */
    if (value < 10000U) { append("%4lu", (unsigned long)value); }
    else if (value < 1000000U) { append("%3luk", (unsigned long)(value / 1000U)); }
    else if (value < 1000000000U) { append("%3luM", (unsigned long)(value / 1000000U)); }
    else { append("%3luG", (unsigned long)(value / 1000000000U)); }
}

static void counters(const uint32_t *values, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        if (i) { append(","); }
        counter(values[i]);
    }
}

static void system_page(void)
{
    board_control_timer_stats_t control;
    board_control_timer_get_stats(&control);
    user_switch_t key = user_switch_decode((uint16_t)app_monitor_status.sw_raw, app_monitor_status.sw_valid != 0U);
    append("Ad=%4lu/%lu K=%2X E=", (unsigned long)app_monitor_status.sw_raw,
           (unsigned long)app_monitor_status.sw_valid, (unsigned)key);
    counter(board_user_adc_error_count());
    append(" D=");
    for (unsigned int i = 0; i < 4U; ++i) {
        bool high;
        append("%c", board_gpio_read_input((board_gpio_input_t)(BOARD_GPIO_INPUT_DIP_0 + i), &high) ? (high ? '1' : '0') : '?');
    }
    for (unsigned int bus = 0; bus < BOARD_CAN_COUNT; ++bus) {
        board_can_stats_t s;
        (void)board_can_get_stats((board_can_bus_t)bus, &s);
        const uint32_t values[] = {s.rx_received, s.rx_dropped, s.rx_lost, s.bus_off_events,
                                   s.tx_submitted, s.tx_errors, s.tx_buffered, s.tx_dropped};
        append(" C%u=", bus + 1U);
        counters(values, sizeof(values) / sizeof(values[0]));
    }
    const uint32_t timing[] = {control.calls, control.overruns, control.max_execution_cycles, control.max_entry_delay_us};
    append(" T="); counters(timing, sizeof(timing) / sizeof(timing[0]));
    append(" O=%2lX,", (unsigned long)app_monitor_status.oled_address);
    const uint32_t oled[] = {app_monitor_status.oled_frames, app_monitor_status.oled_errors};
    counters(oled, sizeof(oled) / sizeof(oled[0]));
}

static void kicker(const power_board_status_t *power, uint32_t now)
{
    append("PW=%-3s VC=", power_board_supply_status());
    decimal(power->capacitor_voltage, false, 5, power_marker(power->capacitor_valid, now - power->capacitor_ms));
    append(" IB="); decimal(power->battery_current, true, 6, power_marker(power->current_valid, now - power->current_ms));
    const char *temp = power_marker(power->temperature_valid, now - power->temperature_ms);
    if (temp) { append(" FET=%5s L1=%5s L2=%5s", temp, temp, temp); }
    else { append(" FET=%5u L1=%5u L2=%5u", (unsigned)power->fet_temp, (unsigned)power->coil_temp[0], (unsigned)power->coil_temp[1]); }
    const char *flags = power_marker(power->flags_valid, now - power->flags_ms);
    char bits[5];
    for (unsigned int i = 0; i < sizeof(bits); ++i) {
        bits[i] = flags ? '-' : ((power->flags >> i) & 1U) ? '1' : '0';
    }
    append(" ");
    if (flags) { append("Fl=%5s", flags); }
    else { append("Fl=%5X", (unsigned)power->flags); }
    append(" Bst=%c Kick=%c Chg=%c En=%c Out=%c", bits[0], bits[1], bits[2], bits[3], bits[4]);
    if (power->fault_seen) { append(" Err=%4X", (unsigned)power->fault_code); }
    else { append(" Err=%4s", "--"); }
    append(" Val="); decimal(power->fault_value, true, 7, power->fault_seen ? NULL : "--");
}

static void uart_raw(void)
{
    board_debug_uart_stats_t s;
    board_debug_uart_get_stats(&s);
    const uint32_t values[] = {s.tx_completed, s.tx_rejected, s.tx_errors, s.rx_dropped, s.rx_errors};
    append("SPI=n/a U4="); counters(values, sizeof(values) / sizeof(values[0]));
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
    used = line_start = 0;
    if (banner_pending) {
        append("\x1b[0m4ws main start [%s] [%s] UART4 2000000 8N1 CAN1/2 started", __DATE__, __TIME__);
        end_line();
        append("IMU ID=%2lX R=%lu OLED R=%lu", (unsigned long)app_monitor_status.imu_who,
               (unsigned long)app_monitor_status.imu_ready, (unsigned long)app_monitor_status.oled_ready);
        end_line();
    }
    if (menu_pending) {
        for (unsigned int i = 0; i < PAGE_COUNT; ++i) {
            append(i == (unsigned int)page ? "\x1b[1;32m %u:%s \x1b[0m" : " %u:%s ", i, names[i]);
        }
        end_line();
    }
    power_board_status_t power;
    (void)power_board_get_status(POWER_BOARD_CAN_BUS, &power);
    static const char *const reset[] = {"WAIT", "RDY", "FAIL"};
    motor_debug_state_t state = app_motor_debug_status();
    append("%-8s VB=", names[page]);
    decimal(power.battery_voltage, false, 5, power_marker(power.battery_valid, now - power.battery_ms));
    append(" BT=%-4s F=%u ", reset[app_board_reset_status()], (unsigned)(state == MOTOR_DEBUG_FAULT));
    switch (page) {
    case MOTOR: motor(now); break;
    case KICKER: kicker(&power, now); break;
    case ODOM: odom(); break;
    case MOTION: motion(now); break;
    case SYSTEM: system_page(); break;
    case UART_RAW: uart_raw(); break;
    case CMD: append("SPI=n/a"); break;
    case LATENCY: append("SPI=n/a CAM=n/a"); break;
    case VEL: append("Body=n/a C610=1:MOTOR"); break;
    default: append("n/a"); break;
    }
    end_line();
    if (used < sizeof(text) && board_debug_uart_write(text, used)) { banner_pending = menu_pending = false; }
}
