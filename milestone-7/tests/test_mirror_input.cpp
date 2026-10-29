// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test for the mirror input's SPS/PPS split (g++ -Isrc tests/test_mirror_input.cpp).
#include "mirror_input.hpp"
#include <assert.h>
#include <stdio.h>

int main() {
    // What airplay-mirrord sends before an IDR frame: SPS, PPS, then the IDR slice (4-byte start codes).
    const uint8_t idr[] = { 0, 0, 0, 1, 0x67, 0x64, 0x00, 0x1f,      // SPS
                            0, 0, 0, 1, 0x68, 0xee, 0x3c, 0x80,      // PPS
                            0, 0, 0, 1, 0x65, 0x88, 0x84, 0x00 };    // IDR slice
    assert(nbt::leadingConfigLen(idr, sizeof(idr)) == 16);

    const uint8_t p_frame[] = { 0, 0, 0, 1, 0x41, 0x9a, 0x02, 0x03 };
    assert(nbt::leadingConfigLen(p_frame, sizeof(p_frame)) == 0);

    const uint8_t sei_first[] = { 0, 0, 0, 1, 0x06, 0x05, 0x01, 0, 0, 0, 1, 0x65, 0x88 };   // SEI, IDR (no config)
    assert(nbt::leadingConfigLen(sei_first, sizeof(sei_first)) == 0);

    const uint8_t config_only[] = { 0, 0, 0, 1, 0x67, 0x64, 0, 0, 0, 1, 0x68, 0xee };
    assert(nbt::leadingConfigLen(config_only, sizeof(config_only)) == sizeof(config_only));

    const uint8_t short3[] = { 0, 0, 1, 0x67, 0x42, 0, 0, 1, 0x68, 0xce, 0, 0, 1, 0x65, 0xb8 };   // 3-byte start codes
    assert(nbt::leadingConfigLen(short3, sizeof(short3)) == 10);

    assert(nbt::leadingConfigLen(idr, 3) == 0);   // truncated
    printf("test_mirror_input: OK\n");
    return 0;
}
