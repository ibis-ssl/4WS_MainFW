#ifndef ATLAS_MOTOR_PACKET_H
#define ATLAS_MOTOR_PACKET_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { ATLAS_COAST, ATLAS_ANGLE, ATLAS_INITIALIZE = 3 } atlas_request_t;

typedef struct {
    int16_t angle_mrad;
    uint16_t current_ma;
} atlas_axis_feedback_t;

typedef struct {
    int16_t initial_phase_mrad;
    bool initialized;
    uint8_t state;
} atlas_axis_calibration_t;

typedef struct {
    uint16_t angle;
    int16_t rpm;
    int16_t raw_current;
} c610_feedback_t;

void atlas_decode_feedback(const uint8_t data[8], atlas_axis_feedback_t axes[2]);
bool atlas_decode_calibration(const uint8_t data[8], atlas_axis_calibration_t axes[2]);
void atlas_encode_angle(uint8_t data[8], unsigned int axis, int16_t mrad);
bool c610_decode_feedback(const uint8_t data[8], c610_feedback_t *feedback);
void c610_encode_current(uint8_t data[8], unsigned int slot, int16_t ma);

#endif
