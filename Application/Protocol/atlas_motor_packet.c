#include "Protocol/atlas_motor_packet.h"

static uint16_t read_le(const uint8_t *data)
{
    return (uint16_t)(data[0] | (uint16_t)data[1] << 8);
}

static uint16_t read_be(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] << 8 | data[1]);
}

void atlas_decode_feedback(const uint8_t data[8], atlas_axis_feedback_t axes[2])
{
    for (unsigned int axis = 0; axis < 2U; ++axis) {
        const uint8_t *field = &data[4U * axis];
        axes[axis].angle_mrad = (int16_t)read_le(field);
        axes[axis].current_ma = read_le(field + 2);
    }
}

bool atlas_decode_calibration(const uint8_t data[8], atlas_axis_calibration_t axes[2])
{
    atlas_axis_calibration_t decoded[2];
    for (unsigned int axis = 0; axis < 2U; ++axis) {
        const uint8_t *field = &data[4U * axis];
        int16_t phase = (int16_t)read_le(field);
        bool initialized = (field[2] & 1U) != 0U;
        if (field[2] > 3U || field[3] > 6U || initialized != (phase != INT16_MIN)) { return false; }
        decoded[axis] = (atlas_axis_calibration_t){phase, initialized, field[3]};
    }
    axes[0] = decoded[0];
    axes[1] = decoded[1];
    return true;
}

void atlas_encode_angle(uint8_t data[8], unsigned int axis, int16_t mrad)
{
    uint8_t *field = &data[4U * axis];
    uint16_t angle = (uint16_t)mrad;
    field[0] = (uint8_t)angle;
    field[1] = (uint8_t)(angle >> 8);
    /* 上限はデバッグ用の 0.5 A / 20%。保存上限と可動域の照合はステアFWが行う。 */
    field[2] = 50;
    field[3] = 40;
}

bool c610_decode_feedback(const uint8_t data[8], c610_feedback_t *feedback)
{
    uint16_t angle = read_be(data);
    if (angle > 8191U) { return false; }
    *feedback = (c610_feedback_t){angle, (int16_t)read_be(data + 2), (int16_t)read_be(data + 4)};
    return true;
}

void atlas_encode_axis_read(uint8_t data[8], uint8_t axis, uint8_t sequence, uint8_t field)
{
    /* CAN保守RPCのAXIS読取り。固定タグはMainの要求と応答の照合にのみ使う。 */
    data[0] = 0xD4;
    data[1] = 10;
    data[2] = 0xFCU | axis;
    data[3] = sequence;
    data[4] = field;
    data[5] = data[6] = data[7] = 0;
}

bool atlas_decode_axis_read(const uint8_t data[8], uint8_t axis, uint8_t sequence, int32_t *value)
{
    if (data[0] != 0xD4U || data[1] != 0x8AU || data[2] != (0xFCU | axis) || data[3] != sequence) {
        return false;
    }
    *value = (int32_t)((uint32_t)data[4] | (uint32_t)data[5] << 8 |
        (uint32_t)data[6] << 16 | (uint32_t)data[7] << 24);
    return true;
}

void c610_encode_current(uint8_t data[8], unsigned int slot, int16_t ma)
{
    uint16_t current = (uint16_t)ma;
    data[2U * slot] = (uint8_t)(current >> 8);
    data[2U * slot + 1U] = (uint8_t)current;
}
