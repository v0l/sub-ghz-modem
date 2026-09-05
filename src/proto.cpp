#include "proto.h"

uint16_t protoCrc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

void FrameWriter::send(Stream &io) const
{
    uint8_t head[3] = { type_, (uint8_t)(len_ & 0xFF), (uint8_t)(len_ >> 8) };

    uint16_t crc = 0xFFFF;
    // One pass over header then value, without building a second buffer.
    for (int i = 0; i < 3; i++) {
        crc ^= (uint16_t)head[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    for (uint16_t i = 0; i < len_; i++) {
        crc ^= (uint16_t)buf_[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }

    uint8_t sof[2] = { PROTO_SOF0, PROTO_SOF1 };
    io.write(sof, 2);
    io.write(head, 3);
    if (len_) io.write(buf_, len_);
    uint8_t tail[2] = { (uint8_t)(crc & 0xFF), (uint8_t)(crc >> 8) };
    io.write(tail, 2);
}

bool TlvReader::next(uint8_t *id, const uint8_t **val, uint8_t *len)
{
    if (pos_ + 2 > n_) return false;
    uint8_t i = d_[pos_], l = d_[pos_ + 1];
    if (pos_ + 2 + l > n_) return false;
    *id = i;
    *len = l;
    *val = d_ + pos_ + 2;
    pos_ += 2 + l;
    return true;
}

uint32_t TlvReader::toU32(const uint8_t *v, uint8_t len)
{
    uint32_t out = 0;
    for (uint8_t i = 0; i < len && i < 4; i++) out |= (uint32_t)v[i] << (8 * i);
    return out;
}
