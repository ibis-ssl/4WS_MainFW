/*
 * 実ソースにHALモックを接続し、転送順序・境界値・バッファ寿命・異常系を検証する。
 * IRQの実時間競合や電気的動作は実機検証の対象であり、この検証には含めない。
 */
#include "stm32g4xx_hal.h"
#include "Board/board_can.h"
#include "Board/board_debug_uart.h"
#include "Board/board_user_adc.h"
#include "Board/board_buzzer.h"
#include "Board/board_pwm_math.h"
#include "Device/buzzer.h"
#include "Device/user_switch.h"
#include "Device/power_board.h"
#include "App/app.h"
#include "App/app_control.h"
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
static bool fail_can_tx, fail_uart_tx, fail_uart_rx, fail_start;
static board_can_frame_t sent[2][256], incoming[2][64];
static FDCAN_RxHeaderTypeDef rx_header_override;
static bool override_header;
static uint8_t *uart_rx, *uart_tx;
static uint16_t uart_tx_length;
static uint16_t *adc_values;
static uint32_t apb_divider = 1, pclk2 = 170000000;
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
HAL_StatusTypeDef HAL_FDCAN_AddMessageToTxFifoQ(FDCAN_HandleTypeDef *h, FDCAN_TxHeaderTypeDef *header, uint8_t *data)
{
    if (fail_can_tx) { return HAL_ERROR; }
    assert(can_free[h->index] > 0);
    assert(header->DataLength == 8 && header->FDFormat == FDCAN_CLASSIC_CAN && header->IdType == FDCAN_STANDARD_ID);
    board_can_frame_t *f = &sent[h->index][can_sent_count[h->index]++];
    f->id = (uint16_t)header->Identifier; memcpy(f->data, data, 8); can_free[h->index]--;
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
    f.length=4; assert(!power_board_accept(BOARD_CAN_1,&f)); f.length=8;
    f.id=0x200; assert(!power_board_accept(BOARD_CAN_1,&f));
    f.id=0x224; f.data[0]=40; f.data[1]=41; f.data[2]=42; assert(power_board_accept(BOARD_CAN_1,&f));
    assert(power_board_get_status(BOARD_CAN_1,&s) && s.fet_temp==40 && s.coil_temp[1]==42);
    can_free[0]=0; can_free[1]=3;
    for (unsigned int i=0; i<20; ++i) { assert(board_can_send(BOARD_CAN_1,1,data)); }
    assert(power_board_set_on(true)==2);
    unsigned int count=can_sent_count[1]; assert(sent[1][count-1].id==0x010 && memcmp(sent[1][count-1].data,on,8)==0);
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
    assert(uart_transfers==0);
    adc_values[15]=501; HAL_ADC_ConvHalfCpltCallback(&hadc1);
    mock_dmb_hook=interrupt_adc_publish;
    adc_values[15]=3001; HAL_ADC_ConvHalfCpltCallback(&hadc1);
    app_control_step(); assert(app_control_user_switch()==USER_SWITCH_LEFT);
    for (uint32_t t=1;t<=1000;++t) {
        mock_tick=t;
        if (t%2U==0) { mock_ipsr=1; mock_tim6.SR=TIM_FLAG_UPDATE; TIM6_DAC_IRQHandler(); mock_ipsr=0; }
        app_process();
        if (t%100U==0) { assert(uart_transfers==t/100U); HAL_UART_TxCpltCallback(&huart4); }
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
}
int main(int argc, char **argv)
{
    if (argc>1 && strcmp(argv[1],"app")==0) { test_app_periodic(); puts("PASS: 500 Hz ISR and 10 Hz main logging"); return 0; }
    test_can(); test_uart(); test_adc_switch(); test_buzzer(); test_power();
    test_control_timer();
    assert(mock_primask==0); puts("PASS: CAN/UART/ADC/SW/PWM/power driver tests"); return 0;
}
