/*
 * 実ソースにHALモックを接続し、転送順序・境界値・バッファ寿命・異常系を検証する。
 * IRQの実時間競合や電気的動作は実機検証の対象であり、この検証には含めない。
 */
#include "stm32g4xx_hal.h"
#include "Board/board_can.h"
#include "Board/board_debug_uart.h"
#include "Board/board_debug_rtt.h"
#include "Board/board_user_adc.h"
#include "Board/board_buzzer.h"
#include "Board/board_pwm_math.h"
#include "Device/buzzer.h"
#include "Device/user_switch.h"
#include "Device/power_board.h"
#include "App/app.h"
#include "App/app_board_reset.h"
#include "App/app_steer_update.h"
#include "App/app_control.h"
#include "App/app_motor_debug.h"
#include "Board/board_gpio.h"
#include "Device/orion_can.h"
#include "Board/board_control_timer.h"
#include "Board/board_critical.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

uint32_t mock_primask, mock_tick;
uint32_t mock_basepri, mock_ipsr;
void (*mock_dmb_hook)(void);
TIM_TypeDef mock_tim6;
mock_core_debug_t mock_core_debug;
mock_dwt_t mock_dwt;
static uint32_t irq_priorities[7], uart_transfers;
void TIM6_DAC_IRQHandler(void);
FDCAN_HandleTypeDef hfdcan1 = {0}, hfdcan2 = {1};
UART_HandleTypeDef huart4;
ADC_HandleTypeDef hadc1;
static TIM_TypeDef timer;
TIM_HandleTypeDef htim1 = {.Instance=&timer};
static unsigned int can_free[2], can_sent_count[2], rx_count[2], rx_position[2];
static uint32_t latest_buffer[2], pending_buffers[2], cancelled_buffers[2];
static bool fail_can_tx, fail_uart_tx, fail_uart_rx, fail_start;
static board_can_frame_t sent[2][2048], incoming[2][64];
static FDCAN_RxHeaderTypeDef rx_header_override;
static bool override_header;
static uint8_t *uart_rx, *uart_tx;
static uint16_t uart_tx_length;
static bool uart_contains(const char *expected)
{
    size_t length = strlen(expected);
    for (size_t offset = 0; offset + length <= uart_tx_length; ++offset) {
        if (memcmp(uart_tx + offset, expected, length) == 0) { return true; }
    }
    return false;
}
static uint16_t *adc_values;
static uint32_t apb_divider = 1, pclk2 = 170000000;
static const char *rtt_input = "";
static char rtt_output[192];

void board_debug_rtt_init(void) {}
bool board_debug_rtt_read(uint8_t *value)
{
    if (*rtt_input == '\0') { return false; }
    *value = (uint8_t)*rtt_input++;
    return true;
}
void board_debug_rtt_write(const char *line)
{
    (void)snprintf(rtt_output, sizeof(rtt_output), "%s", line);
}
uint32_t HAL_GetTick(void) { return mock_tick; }
void HAL_NVIC_SetPriority(IRQn_Type irq, uint32_t a, uint32_t b) { irq_priorities[irq]=a; (void)b; }
void HAL_NVIC_EnableIRQ(IRQn_Type irq) { (void)irq; }
void HAL_NVIC_DisableIRQ(IRQn_Type irq) { (void)irq; }
void HAL_NVIC_ClearPendingIRQ(IRQn_Type irq) { (void)irq; }
HAL_StatusTypeDef HAL_FDCAN_ConfigFilter(FDCAN_HandleTypeDef *h, FDCAN_FilterTypeDef *f)
{ (void)h; assert(f->FilterID1 == 0 && f->FilterID2 == 0); return HAL_OK; }
HAL_StatusTypeDef HAL_FDCAN_ConfigGlobalFilter(FDCAN_HandleTypeDef *h, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{ (void)h; assert(a == FDCAN_REJECT && b == FDCAN_REJECT && c == FDCAN_REJECT_REMOTE && d == FDCAN_REJECT_REMOTE); return HAL_OK; }
HAL_StatusTypeDef HAL_FDCAN_ConfigInterruptLines(FDCAN_HandleTypeDef *h, uint32_t a, uint32_t b)
{ (void)h; (void)a; (void)b; return HAL_OK; }
HAL_StatusTypeDef HAL_FDCAN_Start(FDCAN_HandleTypeDef *h) { return fail_start && h->index == 1 ? HAL_ERROR : HAL_OK; }
HAL_StatusTypeDef HAL_FDCAN_Stop(FDCAN_HandleTypeDef *h) { (void)h; return HAL_OK; }
HAL_StatusTypeDef HAL_FDCAN_ActivateNotification(FDCAN_HandleTypeDef *h, uint32_t a, uint32_t b)
{ (void)h; (void)a; (void)b; return HAL_OK; }
uint32_t HAL_FDCAN_GetTxFifoFreeLevel(FDCAN_HandleTypeDef *h) { return can_free[h->index]; }
HAL_StatusTypeDef HAL_FDCAN_AddMessageToTxFifoQ(FDCAN_HandleTypeDef *h, const FDCAN_TxHeaderTypeDef *header, const uint8_t *data)
{
    if (fail_can_tx) { return HAL_ERROR; }
    assert(can_free[h->index] > 0);
    assert(header->DataLength <= 8 && header->FDFormat == FDCAN_CLASSIC_CAN && header->IdType == FDCAN_STANDARD_ID);
    board_can_frame_t *f = &sent[h->index][can_sent_count[h->index]++];
    f->id = (uint16_t)header->Identifier;
    f->length = (uint8_t)header->DataLength;
    memcpy(f->data, data, f->length);
    latest_buffer[h->index] = 1U << (can_sent_count[h->index] % 3U);
    can_free[h->index]--;
    return HAL_OK;
}
uint32_t HAL_FDCAN_GetLatestTxFifoQRequestBuffer(const FDCAN_HandleTypeDef *h)
{ return latest_buffer[h->index]; }
uint32_t HAL_FDCAN_IsTxBufferMessagePending(const FDCAN_HandleTypeDef *h, uint32_t mask)
{ return pending_buffers[h->index] & mask; }
HAL_StatusTypeDef HAL_FDCAN_AbortTxRequest(FDCAN_HandleTypeDef *h, uint32_t mask)
{
    cancelled_buffers[h->index] |= pending_buffers[h->index] & mask;
    pending_buffers[h->index] &= ~mask;
    return HAL_OK;
}
uint32_t HAL_FDCAN_GetRxFifoFillLevel(FDCAN_HandleTypeDef *h, uint32_t fifo) { (void)fifo; return rx_count[h->index]; }
HAL_StatusTypeDef HAL_FDCAN_GetRxMessage(FDCAN_HandleTypeDef *h, uint32_t fifo, FDCAN_RxHeaderTypeDef *header, uint8_t *data)
{
    (void)fifo;
    board_can_frame_t *f = &incoming[h->index][rx_position[h->index]++];
    *header = (FDCAN_RxHeaderTypeDef){.Identifier=f->id, .DataLength=f->length};
    if (override_header) { *header = rx_header_override; }
    memcpy(data, f->data, 8); rx_count[h->index]--; return HAL_OK;
}
void HAL_FDCAN_IRQHandler(FDCAN_HandleTypeDef *h) { (void)h; }
HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *h) { (void)h; return HAL_OK; }
void HAL_DMA_IRQHandler(DMA_HandleTypeDef *h) { (void)h; }
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *h, uint8_t *data, uint16_t length)
{ assert(h == &huart4 && length == 1); uart_rx=data; h->ErrorCode=0; return fail_uart_rx ? HAL_ERROR : HAL_OK; }
HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *h, uint8_t *data, uint16_t length)
{ assert(h == &huart4); uart_tx=data; uart_tx_length=length; if (!fail_uart_tx) { uart_transfers++; } return fail_uart_tx ? HAL_ERROR : HAL_OK; }
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *h) { (void)h; return HAL_OK; }
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *h) { h->ErrorCode=0; return HAL_OK; }
void HAL_UART_IRQHandler(UART_HandleTypeDef *h) { (void)h; }
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *h, uint32_t mode) { (void)h; (void)mode; return HAL_OK; }
HAL_StatusTypeDef HAL_ADC_Start_DMA(ADC_HandleTypeDef *h, uint32_t *data, uint32_t size)
{ (void)h; assert(size == 32); adc_values=(uint16_t *)data; return HAL_OK; }
void HAL_ADC_IRQHandler(ADC_HandleTypeDef *h) { (void)h; }
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *h, uint32_t channel) { (void)h; (void)channel; return HAL_OK; }
HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef *h, uint32_t channel) { (void)h; (void)channel; return HAL_OK; }
void HAL_RCC_GetClockConfig(RCC_ClkInitTypeDef *c, uint32_t *latency) { c->APB2CLKDivider=apb_divider; c->APB1CLKDivider=1; *latency=4; }
uint32_t HAL_RCC_GetPCLK2Freq(void) { return pclk2; }
uint32_t HAL_RCC_GetPCLK1Freq(void) { return 170000000; }
uint32_t HAL_RCC_GetHCLKFreq(void) { return 170000000; }
HAL_StatusTypeDef HAL_TIM_Base_Init(TIM_HandleTypeDef *h)
{ h->Instance->PSC=h->Init.Prescaler; h->Instance->ARR=h->Init.Period; h->Instance->SR=TIM_FLAG_UPDATE; return HAL_OK; }
HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *h) { h->Instance->DIER=TIM_IT_UPDATE; return HAL_OK; }

static void test_can(void)
{
    uint8_t data[8] = {0}; board_can_stats_t stats;
    assert(!board_can_send(BOARD_CAN_1, 1, data));
    fail_start=true; assert(!board_can_init()); assert(!board_can_send(BOARD_CAN_1, 1, data));
    fail_start=false; assert(board_can_init()); assert(!board_can_init());
    assert(!board_can_send((board_can_bus_t)-1, 1, data));
    assert(!board_can_send(BOARD_CAN_1, 0x800, data));
    assert(!board_can_send(BOARD_CAN_1, 1, NULL));
    for (unsigned int i=0; i<20; ++i) { data[0]=(uint8_t)i; assert(board_can_send(BOARD_CAN_1, (uint16_t)i, data)); }
    assert(!board_can_send(BOARD_CAN_1, 99, data));
    can_free[1]=3; assert(board_can_send(BOARD_CAN_2, 123, data)); assert(can_sent_count[1]==1);
    mock_primask=1; can_free[0]=3; HAL_FDCAN_TxFifoEmptyCallback(&hfdcan1); assert(mock_primask==1); mock_primask=0;
    assert(can_sent_count[0]==3);
    data[0]=20; assert(board_can_send(BOARD_CAN_1, 20, data));
    for (int i=0; i<6; ++i) { can_free[0]=3; board_can_process(); }
    assert(can_sent_count[0]==21);
    for (unsigned int i=0; i<21; ++i) { assert(sent[0][i].id==i && sent[0][i].data[0]==i); }
    can_free[0]=3; fail_can_tx=true; assert(board_can_send(BOARD_CAN_1, 100, data));
    fail_can_tx=false; board_can_process(); assert(sent[0][21].id==100);
    for (unsigned int i=0; i<22; ++i) { incoming[0][i]=(board_can_frame_t){.id=(uint16_t)i,.length=8,.data={0xA5}}; }
    rx_position[0]=0; rx_count[0]=22; mock_tick=123;
    for (int i=0; i<8; ++i) { HAL_FDCAN_RxFifo0Callback(&hfdcan1, 0); }
    board_can_frame_t f;
    for (unsigned int i=0; i<20; ++i) { assert(board_can_receive(BOARD_CAN_1, &f)); assert(f.id==i && f.length==8 && f.data[0]==0xA5 && f.received_ms==123); }
    assert(!board_can_receive(BOARD_CAN_1, &f));
    override_header=true;
    FDCAN_RxHeaderTypeDef invalid[]={{.IdType=1,.DataLength=8},{.RxFrameType=1,.DataLength=8},{.FDFormat=1,.DataLength=8},{.DataLength=9}};
    for (unsigned int i=0; i<4; ++i) { rx_position[0]=0; rx_count[0]=1; rx_header_override=invalid[i]; HAL_FDCAN_RxFifo0Callback(&hfdcan1,0); assert(!board_can_receive(BOARD_CAN_1,&f)); }
    override_header=false;
    HAL_FDCAN_RxFifo0Callback(&hfdcan1,FDCAN_IT_RX_FIFO0_MESSAGE_LOST);
    HAL_FDCAN_ErrorStatusCallback(&hfdcan1,FDCAN_IT_BUS_OFF);
    board_can_get_stats(BOARD_CAN_1,&stats);
    assert(stats.tx_dropped==1 && stats.rx_dropped==2 && stats.rx_invalid==4 && stats.rx_lost==1 && stats.bus_off_events==1);
    assert(stats.tx_errors>0);
}
static void test_uart(void)
{
    assert(board_debug_uart_init());
    uint8_t text[]={1,2,3};
    assert(board_debug_uart_write(text,3)); text[0]=9;
    assert(uart_tx[0]==1 && uart_tx_length==3);
    assert(!p("overwrite")); assert(uart_tx[0]==1);
    HAL_UART_TxCpltCallback(&huart4);
    char huge[2002]; memset(huge,'x',sizeof(huge)); huge[2001]=0;
    assert(!p("%s",huge));
    assert(!board_debug_uart_write(huge,2001)); assert(!board_debug_uart_write(NULL,1));
    assert(p("%s:%u","ok",42)); assert(uart_tx_length==5 && memcmp(uart_tx,"ok:42",5)==0);
    huart4.ErrorCode=HAL_UART_ERROR_DMA; HAL_UART_ErrorCallback(&huart4);
    assert(!board_debug_uart_write(text,3)); board_debug_uart_process();
    fail_uart_tx=true; assert(!board_debug_uart_write(text,3)); fail_uart_tx=false;
    assert(board_debug_uart_write(text,3)); HAL_UART_TxCpltCallback(&huart4);
    huart4.ErrorCode=0;
    for (unsigned int i=0; i<257; ++i) { *uart_rx=(uint8_t)i; HAL_UART_RxCpltCallback(&huart4); }
    uint8_t value;
    for (unsigned int i=0; i<256; ++i) { assert(board_debug_uart_read(&value) && value==(uint8_t)i); }
    assert(!board_debug_uart_read(&value));
    huart4.ErrorCode=1; *uart_rx='q'; HAL_UART_RxCpltCallback(&huart4);
    assert(huart4.ErrorCode==1); HAL_UART_ErrorCallback(&huart4);
    assert(!board_debug_uart_read(&value)); board_debug_uart_process();
    board_debug_uart_stats_t stats; board_debug_uart_get_stats(&stats);
    assert(stats.rx_dropped==1 && stats.rx_errors==1 && stats.tx_errors==2);
}
static void test_adc_switch(void)
{
    assert(user_switch_read()==USER_SWITCH_INVALID); assert(board_user_adc_init());
    assert(user_switch_read()==USER_SWITCH_INVALID);
    const uint16_t raw[]={0,100,101,500,501,2000,2001,3000,3001,3900,3901,4095};
    const user_switch_t expect[]={USER_SWITCH_CENTER,USER_SWITCH_CENTER,USER_SWITCH_BACK,USER_SWITCH_BACK,
        USER_SWITCH_RIGHT,USER_SWITCH_RIGHT,USER_SWITCH_FORWARD,USER_SWITCH_FORWARD,USER_SWITCH_LEFT,
        USER_SWITCH_LEFT,USER_SWITCH_NONE,USER_SWITCH_NONE};
    for (unsigned int i=0; i<12; ++i) { assert(user_switch_decode(raw[i],true)==expect[i]); }
    assert(user_switch_decode(4096,true)==USER_SWITCH_INVALID);
    assert(user_switch_decode(0,false)==USER_SWITCH_INVALID);
    assert(user_switch_decode(50, true) == 0x01);
    assert(user_switch_decode(110, true) == 0x02);
    assert(user_switch_decode(501, true) == 0x04);
    assert(user_switch_decode(2400, true) == 0x08);
    assert(user_switch_decode(3001, true) == 0x10);
    assert(user_switch_decode(4095, true) == 0x00);
    mock_tick=UINT32_MAX-20; adc_values[15]=501; HAL_ADC_ConvHalfCpltCallback(&hadc1);
    mock_tick=20; assert(user_switch_read()==USER_SWITCH_RIGHT);
    mock_tick=200; assert(user_switch_read()==USER_SWITCH_INVALID);
    adc_values[31]=3001; HAL_ADC_ConvCpltCallback(&hadc1); assert(user_switch_read()==USER_SWITCH_LEFT);
    HAL_ADC_ErrorCallback(&hadc1); HAL_ADC_ConvCpltCallback(&hadc1);
    assert(user_switch_read()==USER_SWITCH_INVALID && board_user_adc_error_count()==1);
}
static void test_buzzer(void)
{
    board_pwm_plan_t plan;
    assert(!board_pwm_calculate(170000000,0,&plan)); assert(!board_pwm_calculate(170000000,20001,&plan));
    assert(!board_pwm_calculate(0,2000,&plan)); assert(!board_pwm_calculate(170000000,2000,NULL));
    for (uint32_t hz=1; hz<=20000; ++hz) {
        assert(board_pwm_calculate(170000000,hz,&plan));
        double actual=170000000.0/((plan.prescaler+1.0)*(plan.period+1.0));
        assert(fabs(actual-hz)/hz < 0.00006);
        assert(plan.pulse>0 && plan.pulse<=plan.period);
    }
    assert(board_buzzer_init() && timer.CCR1==0);
    uint32_t actual;
    assert(board_buzzer_set_hz(2000,&actual) && actual==2000000);
    assert((timer.PSC+1U)*(timer.ARR+1U)==85000U);
    apb_divider=2; pclk2=85000000; assert(board_buzzer_set_hz(2000,&actual) && actual==2000000);
    mock_tick=UINT32_MAX-10; assert(buzzer_start(1046,20));
    mock_tick=8; buzzer_process(); assert(timer.CCR1!=0);
    mock_tick=9; buzzer_process(); assert(timer.CCR1==0);
    assert(buzzer_start(2000,0)); mock_tick+=100000; buzzer_process(); assert(timer.CCR1!=0); buzzer_stop(); assert(timer.CCR1==0);
}
static void test_power(void)
{
    uint8_t data[8];
    assert(power_packet_on(true,data)); const uint8_t on[8]={0,1}; assert(memcmp(on,data,8)==0);
    assert(power_packet_param(POWER_PARAM_MAX_VOLTAGE,24.0f,data));
    const uint8_t param[8]={2,0,0,0xC0,0x41,0,0,0}; assert(memcmp(param,data,8)==0);
    assert(!power_packet_param((power_param_t)0,24,data));
    assert(!power_packet_param(POWER_PARAM_MAX_VOLTAGE,NAN,data));
    assert(!power_packet_param(POWER_PARAM_MAX_VOLTAGE,-1,data));
    assert(power_packet_reset(data) && data[0]==0x5A && data[1]==0xA5);
    board_can_frame_t f={.id=0x215,.length=8,.data={0,0,0xC0,0x41},.received_ms=100};
    assert(power_board_accept(BOARD_CAN_1,&f)); power_board_status_t s;
    assert(power_board_get_status(BOARD_CAN_1,&s) && s.battery_valid && s.battery_voltage==24.0f && s.battery_ms==100);
    assert(power_board_get_status(BOARD_CAN_2,&s) && !s.battery_valid);
    f.length=4; assert(power_board_accept(BOARD_CAN_1,&f));
    assert(!power_board_accept(BOARD_CAN_2,&f));
    f.length=3; assert(!power_board_accept(BOARD_CAN_1,&f)); f.length=8;
    f.id=0x200; assert(!power_board_accept(BOARD_CAN_1,&f));
    f.id=0x224; f.data[0]=40; f.data[1]=41; f.data[2]=42; assert(power_board_accept(BOARD_CAN_1,&f));
    assert(power_board_get_status(BOARD_CAN_1,&s) && s.fet_temp==40 && s.coil_temp[1]==42);
    can_free[0]=0; can_free[1]=3;
    for (unsigned int i=0; i<20; ++i) { assert(board_can_send(BOARD_CAN_1,1,data)); }
    assert(power_board_set_on(true)==0);
    for (int i=0; i<7; ++i) { can_free[0]=3; board_can_process(); }
    unsigned int other_count=can_sent_count[1];
    assert(power_board_set_on(true)==1);
    unsigned int count=can_sent_count[0];
    assert(sent[0][count-1].id==0x010 && memcmp(sent[0][count-1].data,on,8)==0);
    assert(can_sent_count[1]==other_count);
}
static void test_control_callback(void) { mock_dwt.CYCCNT += 340001U; mock_tim6.SR |= TIM_FLAG_UPDATE; }
static void test_control_timer(void)
{
    assert(!board_control_timer_start(0,test_control_callback));
    assert(!board_control_timer_start(333,test_control_callback));
    assert(board_control_timer_start(500,test_control_callback));
    assert(mock_tim6.PSC==169 && mock_tim6.ARR==1999 && mock_tim6.SR==0);
    assert(irq_priorities[TIM6_DAC_IRQn] < irq_priorities[UART4_IRQn]);
    uint32_t lock=board_critical_enter();
    assert(mock_primask==0 && mock_basepri==(5U << 4));
    uint32_t nested=board_critical_enter(); board_critical_exit(nested); assert(mock_basepri==(5U << 4));
    /* 制御優先度2はBASEPRIの禁止対象に入らない。 */
    assert((irq_priorities[TIM6_DAC_IRQn] << 4) < mock_basepri);
    mock_tim6.SR=TIM_FLAG_UPDATE; mock_tim6.CNT=3;
    TIM6_DAC_IRQHandler(); board_critical_exit(lock);
    board_control_timer_stats_t s; board_control_timer_get_stats(&s);
    assert(s.calls==1 && s.overruns==1 && s.max_execution_cycles==340001 && s.max_entry_delay_us==3);
    assert(mock_basepri==0 && mock_tim6.SR==0);
}
static void interrupt_adc_publish(void)
{
    app_control_step();
    assert(app_control_user_switch()==USER_SWITCH_RIGHT);
}
static void test_app_periodic(void)
{
    assert(app_init());
    assert(timer.CCR1 != 0U && (timer.PSC + 1U) * (timer.ARR + 1U) == 85000U);
    assert(uart_transfers==0);
    adc_values[15]=501; HAL_ADC_ConvHalfCpltCallback(&hadc1);
    mock_dmb_hook=interrupt_adc_publish;
    adc_values[15]=3001; HAL_ADC_ConvHalfCpltCallback(&hadc1);
    app_control_step(); assert(app_control_user_switch()==USER_SWITCH_LEFT);
    for (uint32_t t=1;t<=1000;++t) {
        mock_tick=t;
        if (t%2U==0) { mock_ipsr=1; mock_tim6.SR=TIM_FLAG_UPDATE; TIM6_DAC_IRQHandler(); mock_ipsr=0; }
        app_process();
        if (t < 100U) { assert(timer.CCR1 != 0U); }
        else { assert(timer.CCR1 == 0U); }
        if (t%100U==0) {
            assert(uart_transfers==t/100U && uart_contains("SYSTEM SW valid="));
            assert(uart_contains("4ws main start") == (t == 100U));
            HAL_UART_TxCpltCallback(&huart4);
        }
    }
    board_control_timer_stats_t s; board_control_timer_get_stats(&s); assert(s.calls==500);
    /* mainを停止しても制御IRQの入力取込みと実行回数は進む。 */
    adc_values[15]=3001; HAL_ADC_ConvHalfCpltCallback(&hadc1);
    mock_tim6.SR=TIM_FLAG_UPDATE; TIM6_DAC_IRQHandler();
    assert(app_control_user_switch()==USER_SWITCH_LEFT);
    assert(uart_transfers==10);
    mock_ipsr=1; assert(!p("IRQ forbidden")); mock_ipsr=0; assert(uart_transfers==10);
    mock_tick=1450; app_process(); assert(uart_transfers==11);
    app_process(); assert(uart_transfers==11); HAL_UART_TxCpltCallback(&huart4);
    mock_tick=1499; app_process(); assert(uart_transfers==11);
    mock_tick=1500; app_process(); assert(uart_transfers==12);
    mock_tick=1600; app_process(); assert(uart_transfers==12);
    HAL_UART_TxCpltCallback(&huart4); mock_tick=1699; app_process(); assert(uart_transfers==12);
    mock_tick=1700; app_process(); assert(uart_transfers==13);
    HAL_UART_TxCpltCallback(&huart4);
    mock_tick=UINT32_MAX-50U; app_process(); assert(uart_transfers==14);
    HAL_UART_TxCpltCallback(&huart4);
    mock_tick=3; app_process(); assert(uart_transfers==14);
    mock_tick=4; app_process(); assert(uart_transfers==15);
    HAL_UART_TxCpltCallback(&huart4);
    mock_tick=103; app_process(); assert(uart_transfers==15);
    mock_tick=104; app_process(); assert(uart_transfers==16);
    HAL_UART_TxCpltCallback(&huart4);
    /* CRLFは1ページだけ送り、接続なしの値を0として表示しない。 */
    *uart_rx = '\r'; HAL_UART_RxCpltCallback(&huart4);
    *uart_rx = '\n'; HAL_UART_RxCpltCallback(&huart4);
    mock_tick=204; app_process(); assert(uart_contains("UART RAW CM4_SPI=n/a"));
    HAL_UART_TxCpltCallback(&huart4);
    *uart_rx = '1'; HAL_UART_RxCpltCallback(&huart4);
    *uart_rx = 'q'; HAL_UART_RxCpltCallback(&huart4);
    mock_tick=304; app_process(); assert(uart_contains("MOTOR Mode=") && uart_contains("C610=4 rpm=--"));
}

extern uint32_t mock_gpio_low;

static void inject(unsigned int bus, uint16_t id, const uint8_t data[8])
{
    board_can_frame_t *frame = &incoming[bus][rx_count[bus]++];
    *frame = (board_can_frame_t){.id = id, .length = 8};
    memcpy(frame->data, data, 8);
}

static const board_can_frame_t *latest_tx(unsigned int bus, uint16_t id);
static bool skip_first_reset_ack, use_second_bus_alternate_offset;

static bool inject_startup_reply(unsigned int bus)
{
    static unsigned int last_sequence[2];
    const uint16_t id = bus == 0U || use_second_bus_alternate_offset ? 0x5E0U : 0x5D0U;
    const board_can_frame_t *request = latest_tx(bus, id);
    if (!request || request->data[2] != 0xF6U || request->data[3] == last_sequence[bus]) { return false; }
    last_sequence[bus] = request->data[3];
    if (skip_first_reset_ack && bus == 0U && request->data[1] == 31U) { return false; }
    uint8_t data[8] = {0xD4U, request->data[1] | 0x80U, request->data[2], request->data[3]};
    if (request->data[1] == 11U && request->data[4] == 20U) { data[4] = (uint8_t)bus; }
    inject(bus, id + 1U, data);
    return true;
}

static bool inject_range_reply(unsigned int bus)
{
    static unsigned int last_sequence[2];
    const uint16_t id = bus == 0U ? 0x5E0U : 0x5D0U;
    const board_can_frame_t *request = latest_tx(bus, id);
    if (!request || request->data[3] == last_sequence[bus]) { return false; }
    last_sequence[bus] = request->data[3];
    uint8_t data[8] = {0xD4, 0x8A, request->data[2], request->data[3]};
    const int32_t edge = (bus == 0U && (data[2] & 1U) == 0U) ? 40000 : 60000;
    int32_t value = request->data[4] == 7U ? -edge : request->data[4] == 8U ? edge : 0;
    for (unsigned int i = 0; i < 4U; ++i) { data[4U + i] = (uint8_t)((uint32_t)value >> (8U * i)); }
    inject(bus, id + 1U, data);
    return true;
}

static void motor_ticks(unsigned int ms, uint16_t key_adc, bool c610_can2, uint8_t calibration_state)
{
    while (ms-- != 0U) {
        mock_tick++;
        adc_values[15] = key_adc;
        HAL_ADC_ConvHalfCpltCallback(&hadc1);
        const uint8_t zeros[8] = {0};
        for (unsigned int bus = 0; bus < BOARD_CAN_COUNT; ++bus) {
            can_free[bus] = 3;
            const uint16_t first = (uint16_t)(2U * bus);
            if (mock_tick % 10U == 0U) {
                uint8_t cal[8] = {0,0,1,3,0,0,1,3};
                if (calibration_state == 2U) {
                    cal[1] = 0x80;
                    cal[2] = 0;
                    cal[3] = 2;
                    cal[5] = 0x80;
                    cal[6] = 0;
                    cal[7] = 2;
                }
                inject(bus, (uint16_t)(0x510U + first), cal);
            } else {
                inject(bus, (uint16_t)(0x230U + first), zeros);
                bool has_reply = inject_startup_reply(bus) || inject_range_reply(bus);
                if (bus == 0 || c610_can2) {
                    inject(bus, (uint16_t)(0x201U + first), zeros);
                    if (!has_reply) { inject(bus, (uint16_t)(0x202U + first), zeros); }
                }
            }
        }
        app_process();
        for (unsigned int bus = 0; bus < BOARD_CAN_COUNT; ++bus) {
            assert(rx_count[bus] == 0);
            rx_position[bus] = 0;
        }
    }
}

static const board_can_frame_t *latest_tx(unsigned int bus, uint16_t id)
{
    for (unsigned int n = can_sent_count[bus]; n != 0U; --n) {
        if (sent[bus][n - 1U].id == id) { return &sent[bus][n - 1U]; }
    }
    return NULL;
}

static void assert_stop(unsigned int bus)
{
    const uint8_t zero[8] = {0};
    const board_can_frame_t *current = latest_tx(bus, 0x200);
    const board_can_frame_t *steer = latest_tx(bus, (uint16_t)(0x350U + 2U * bus));
    assert(current && current->length == 8 && memcmp(current->data, zero, 8) == 0);
    assert(steer && steer->length == 2 && memcmp(steer->data, zero, 2) == 0);
}

static void assert_all_drive(int direction)
{
    for (unsigned int bus = 0; bus < BOARD_CAN_COUNT; ++bus) {
        const board_can_frame_t *current = latest_tx(bus, 0x200);
        assert(current && current->length == 8);
        for (unsigned int slot = 0; slot < 4U; ++slot) {
            const uint8_t *data = &current->data[2U * slot];
            int16_t ma = (int16_t)((uint16_t)data[0] << 8 | data[1]);
            if (slot / 2U == bus) {
                assert(ma * direction >= 130 && ma * direction <= 500);
            } else {
                assert(ma == 0);
            }
        }
    }
}

static int16_t angle_mrad(unsigned int bus, unsigned int axis)
{
    const board_can_frame_t *angle = latest_tx(bus, (uint16_t)(0x120U + 2U * bus));
    assert(angle && angle->length == 8);
    const uint8_t *field = &angle->data[4U * axis];
    assert(field[2] == 150U && field[3] == 40U);
    return (int16_t)((uint16_t)field[0] | (uint16_t)field[1] << 8);
}

static unsigned int count_reset_tx(unsigned int bus, uint16_t id)
{
    unsigned int resets = 0;
    for (unsigned int n = 0; n < can_sent_count[bus]; ++n) {
        const board_can_frame_t *frame = &sent[bus][n];
        if (frame->id == id && frame->data[1] == 31U) { resets++; }
    }
    return resets;
}

static void test_startup_reset_failure(void)
{
    skip_first_reset_ack = use_second_bus_alternate_offset = true;
    assert(app_init());
    mock_gpio_low = 3;
    motor_ticks(50, 4095, true, 3);
    const uint8_t pre_reset_error[8] = {1, 0, 4};
    inject(0, 0x000U, pre_reset_error);
    can_free[0] = can_free[1] = 3;
    app_process();
    rtt_input = "init\n";
    motor_ticks(1, 4095, true, 3);
    assert(strcmp(rtt_output, "REJECT init\n") == 0);
    motor_ticks(1000, 4095, true, 3);
    assert(count_reset_tx(0, 0x5E0U) == 1U);
    assert(count_reset_tx(1, 0x5D0U) == 0U);
    assert(count_reset_tx(1, 0x5E0U) == 1U);
    assert(app_board_reset_status() == BOARD_RESET_FAILED);
    assert(app_motor_debug_status() == MOTOR_DEBUG_FAULT);
    assert_stop(0); assert_stop(1);
}

static void test_motor_debug(void)
{
    assert(app_init());
    motor_ticks(50, 501, true, 3);
    const uint8_t pre_reset_error[8] = {1, 0, 4};
    inject(0, 0x000U, pre_reset_error);
    app_process();
    motor_ticks(650, 501, true, 3);
    assert(app_board_reset_status() == BOARD_RESET_READY);
    assert_stop(0); assert_stop(1);
    const board_can_frame_t *power_reset = latest_tx(0, 0x001);
    const uint8_t reset[8] = {0x5A, 0xA5};
    assert(power_reset && power_reset->length == 8 && memcmp(power_reset->data, reset, 8) == 0);
    assert(latest_tx(1, 0x001) == NULL);
    assert(count_reset_tx(0, 0x5E0U) == 1U);
    assert(count_reset_tx(1, 0x5D0U) == 1U);
    mock_gpio_low = 3;
    motor_ticks(80, 501, true, 3);
    assert(app_motor_debug_status() == MOTOR_DEBUG_WAIT);
    assert(latest_tx(0, 0x120) == NULL);
    assert_stop(0); assert_stop(1);
    orion_can_sample_t sample;
    assert(!orion_can_read(BOARD_CAN_1, 0x230, mock_tick, 50, &sample));

    motor_ticks(35, 4095, true, 3);
    assert(app_motor_debug_status() == MOTOR_DEBUG_READY);
    motor_ticks(131, 501, true, 3);
    assert(app_motor_debug_status() == MOTOR_DEBUG_STEER);
    assert(angle_mrad(0, 0) == 157 && angle_mrad(1, 1) == 157);
    motor_ticks(300, 501, true, 3);
    assert(angle_mrad(0, 0) == 174 && angle_mrad(0, 1) == 523);
    assert(angle_mrad(1, 0) == 523 && angle_mrad(1, 1) == 523);
    pending_buffers[0] = latest_buffer[0];
    motor_ticks(1, 4095, true, 3);
    assert(cancelled_buffers[0] != 0);
    assert_stop(0); assert_stop(1);

    /* 全輪の校正・帰還がそろうまで駆動を開始しない。 */
    motor_ticks(35, 4095, true, 2);
    motor_ticks(45, 2400, true, 2);
    assert(app_motor_debug_status() == MOTOR_DEBUG_WAIT);
    assert_stop(0); assert_stop(1);

    motor_ticks(60, 4095, false, 3);
    rtt_input = "drive+\n";
    motor_ticks(1, 4095, false, 3);
    assert(strcmp(rtt_output, "REJECT drive\n") == 0);
    assert_stop(0); assert_stop(1);

    motor_ticks(4, 4095, true, 3);
    rtt_input = "drive+\n";
    motor_ticks(22, 4095, true, 3);
    assert(strcmp(rtt_output, "OK drive\n") == 0);
    assert_all_drive(1);
    motor_ticks(2200, 4095, true, 3);
    assert_stop(0); assert_stop(1);

    motor_ticks(35, 4095, true, 3);
    motor_ticks(75, 110, true, 3);
    assert(app_motor_debug_status() == MOTOR_DEBUG_DRIVE);
    assert_all_drive(-1);
    motor_ticks(80, 110, false, 3);
    assert_stop(0); assert_stop(1);
    motor_ticks(60, 110, true, 3);
    assert(app_motor_debug_status() == MOTOR_DEBUG_WAIT);
    assert_stop(1);

    motor_ticks(35, 4095, true, 3);
    motor_ticks(32, 50, true, 3);
    assert(app_motor_debug_status() == MOTOR_DEBUG_INIT);
    for (unsigned int bus = 0; bus < BOARD_CAN_COUNT; ++bus) {
        const board_can_frame_t *control = latest_tx(bus, (uint16_t)(0x350U + 2U * bus));
        assert(control && control->length == 2 && control->data[0] == 3 && control->data[1] == 3);
    }
    motor_ticks(10, 4095, true, 2);
    assert(app_motor_debug_status() == MOTOR_DEBUG_INIT);
    motor_ticks(35, 4095, true, 3);
    assert_stop(1);

    motor_ticks(35, 4095, true, 3);
    rtt_input = "init\n";
    motor_ticks(1, 4095, true, 3);
    assert(strcmp(rtt_output, "OK init\n") == 0);
    for (unsigned int bus = 0; bus < BOARD_CAN_COUNT; ++bus) {
        const board_can_frame_t *control = latest_tx(bus, (uint16_t)(0x350U + 2U * bus));
        assert(control && control->data[0] == 3 && control->data[1] == 3);
    }
    motor_ticks(400, 4095, true, 2);
    rtt_input = "keep\n";
    motor_ticks(200, 4095, true, 2);
    assert(app_motor_debug_status() == MOTOR_DEBUG_INIT);
    motor_ticks(302, 4095, true, 2);
    assert_stop(0); assert_stop(1);

    motor_ticks(35, 4095, true, 3);
    motor_ticks(46, 2400, true, 3);
    assert(app_motor_debug_status() == MOTOR_DEBUG_DRIVE);
    assert_all_drive(1);
    mock_gpio_low = 0;
    motor_ticks(35, 2400, true, 3);
    assert(app_motor_debug_status() == MOTOR_DEBUG_OFF);
    assert_stop(0); assert_stop(1);
    mock_gpio_low = 3;
    motor_ticks(80, 2400, true, 3);
    assert(app_motor_debug_status() == MOTOR_DEBUG_WAIT);
    assert_stop(0); assert_stop(1);

    motor_ticks(35, 4095, true, 3);
    motor_ticks(131, 3001, true, 3);
    assert(angle_mrad(0, 0) == -157 && angle_mrad(1, 1) == -157);
    motor_ticks(300, 3001, true, 3);
    assert(angle_mrad(0, 0) == -174 && angle_mrad(1, 1) == -523);
    const uint8_t error[8] = {2,0,1};
    inject(1, 0x000, error);
    can_free[0] = can_free[1] = 3;
    app_process();
    assert(app_motor_debug_status() == MOTOR_DEBUG_FAULT);
    assert_stop(0); assert_stop(1);
    assert(latest_tx(0, 0x000) == NULL && latest_tx(1, 0x000) == NULL);
}

static void test_steer_update(void)
{
    assert(app_init());
    motor_ticks(700, 4095, true, 3);
    assert(app_board_reset_status() == BOARD_RESET_READY);

    /* g0は正温、g1は負温、g2は無効、g3は0℃。 */
    inject(0, 0x290U, (const uint8_t[8]){125, 0, 0xF1, 0xFF});
    incoming[0][0].length = 4;
    inject(0, 0x292U, (const uint8_t[8]){99, 0, 99, 0});
    incoming[0][1].length = 4;
    inject(1, 0x292U, (const uint8_t[8]){0, 0x80, 0, 0});
    incoming[1][0].length = 4;
    rtt_input = "temp\n";
    app_process();
    assert(strcmp(rtt_output, "TEMP seen=F valid=B decic=125,-15,-32768,0\n") == 0);
    rx_position[0] = rx_position[1] = 0;
    mock_tick += 2500;
    rtt_input = "temp\n";
    app_process();
    assert(strcmp(rtt_output, "TEMP seen=0 valid=0 decic=-32768,-32768,-32768,-32768\n") == 0);

    uint8_t image_bytes[521] = {
        [1] = 0x10, [3] = 0x20, [4] = 0x09, [5] = 0x40, [7] = 0x08,
        [512] = 0x44, [513] = 0x43, [514] = 0x44, [515] = 0x34,
        [516] = 3, [520] = 0xAB
    };
    const uint32_t expected_crc = 0x1269ABD7U;
    motor_ticks(1, 4095, true, 3);
    unsigned int can2_before = can_sent_count[1];
    assert(app_steer_update_begin(1, sizeof(image_bytes), 0, mock_tick));
    for (unsigned int at = 0; at < sizeof(image_bytes); at += 256U) {
        unsigned int count = at == 512U ? 9U : 256U;
        assert(app_steer_update_stage(at, image_bytes + at, count, mock_tick));
    }
    assert(!app_steer_update_commit(mock_tick));
    assert(can_sent_count[1] == can2_before);

    rtt_input = "fw_begin 1 521 1269abd7\n";
    motor_ticks(1, 4095, true, 3);
    assert(strcmp(rtt_output, "OK fw\n") == 0);
    assert(!app_steer_update_stage(2, image_bytes + 2, 256, mock_tick));
    char chunk[530] = "fw_data 0 ";
    for (unsigned int i = 0; i < 256U; ++i) {
        (void)snprintf(chunk + 10U + 2U * i, 3, "%02x", image_bytes[i]);
    }
    chunk[522] = '\n';
    chunk[523] = '\0';
    rtt_input = chunk;
    motor_ticks(6, 4095, true, 3);
    assert(strcmp(rtt_output, "OK fw\n") == 0);
    assert(app_steer_update_stage(0, image_bytes, 256, mock_tick));
    assert(app_steer_update_stage(256, image_bytes + 256, 256, mock_tick));
    assert(app_steer_update_stage(512, image_bytes + 512, 9, mock_tick));
    rtt_input = "fw_commit\n";
    motor_ticks(1, 4095, true, 3);
    assert(strcmp(rtt_output, "OK fw\n") == 0);
    assert(app_steer_update_locked() && !app_steer_update_abort());
    unsigned int received = 0, frame_index = 0, blocks = 0;
    unsigned int lost_open_replies = 5U;
    bool erased = false, finished = false, rebooted = false;
    for (unsigned int t = 0; t < 1200U && app_steer_update_locked(); ++t) {
        mock_tick++;
        can_free[0] = can_free[1] = 3;
        while (can2_before < can_sent_count[1]) {
            const board_can_frame_t *tx = &sent[1][can2_before++];
            uint8_t reply[8] = {0};
            uint32_t value = 0;
            if (tx->id == 0x5D0U) {
                const uint8_t id[8] = {0xD4, 0x8B, tx->data[2], tx->data[3], 1};
                inject(1, 0x5D1, id);
                continue;
            }
            if (tx->id == 0x601U) {
                assert(erased && tx->data[0] == frame_index && tx->data[1] == 0U);
                for (unsigned int i = 0; i < 6U; ++i) {
                    unsigned int at = received + frame_index * 6U + i;
                    uint8_t byte = frame_index * 6U + i >= 256U || at >= sizeof(image_bytes) ?
                        0xFFU : image_bytes[at];
                    assert(tx->data[2U + i] == byte);
                }
                reply[0] = 0x20;
                reply[2] = (uint8_t)frame_index++;
            } else if (tx->id == 0x600U) {
                reply[0] = tx->data[0];
                switch (reply[0]) {
                case 2: value = 0x18000; break;
                case 3: assert(finished); value = sizeof(image_bytes); break;
                case 4: value = finished ? expected_crc : 0x11223344U; break;
                case 5: value = 0x34444344U; break;
                case 0x10: erased = true; break;
                case 0x11:
                    frame_index = 0;
                    if (blocks == 1U && lost_open_replies != 0U) {
                        lost_open_replies--;
                        continue;
                    }
                    break;
                case 0x12:
                    assert(frame_index == 43U);
                    blocks++;
                    received = blocks < 3U ? blocks * 256U : sizeof(image_bytes);
                    value = received;
                    break;
                case 0x13: assert(received == sizeof(image_bytes)); finished = true; break;
                case 0x14: assert(finished); rebooted = true; break;
                default: break;
                }
            } else { continue; }
            for (unsigned int n = 0; n < 4U; ++n) { reply[4U + n] = (uint8_t)(value >> (8U * n)); }
            inject(1, 0x602, reply);
        }
        app_process();
        rx_position[1] = 0;
    }
    steer_update_status_t update = app_steer_update_status();
    assert(update.state == STEER_UPDATE_DONE && update.written == sizeof(image_bytes));
    assert(update.old_crc == 0x11223344U && update.image_crc == expected_crc && rebooted);
    assert(lost_open_replies == 0U);
    assert(!app_steer_update_locked() && latest_tx(0, 0x600U) == NULL);
}

int main(int argc, char **argv)
{
    void test_peripherals(void);
    if (argc>1 && strcmp(argv[1],"peripherals")==0) { test_peripherals(); return 0; }
    if (argc>1 && strcmp(argv[1],"app")==0) { test_app_periodic(); puts("PASS: 500 Hz ISR and 10 Hz main logging"); return 0; }
    if (argc>1 && strcmp(argv[1],"motor")==0) { test_motor_debug(); puts("PASS: Atlas/C610 motor debug, bus routing and stops"); return 0; }
    if (argc>1 && strcmp(argv[1],"boot")==0) { test_startup_reset_failure(); puts("PASS: startup reset timeout, offset fallback and interlock"); return 0; }
    if (argc>1 && strcmp(argv[1],"update")==0) { test_steer_update(); puts("PASS: steering temperatures and RTT-to-CAN firmware update"); return 0; }
    test_can(); test_uart(); test_adc_switch(); test_buzzer(); test_power();
    test_control_timer();
    assert(mock_primask==0); puts("PASS: CAN/UART/ADC/SW/PWM/power driver tests"); return 0;
}
