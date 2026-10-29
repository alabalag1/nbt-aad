// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef MIRROR_INPUT_HPP
#define MIRROR_INPUT_HPP

// iPhone screen mirroring (AirPlay) input. A separate receiver process (airplay-mirrord, GPLv3, built from UxPlay's
// lib/) does discovery, pairing, FairPlay and decryption, and connects to 127.0.0.1:NBT_MIRROR_PORT (52791) for
// each mirroring session. Messages: [u8 type][u32 BE payload length][payload]
//   1 = stream size (u16 BE width, u16 BE height), 2 = Annex-B access unit (SPS/PPS inline before IDR frames).
// Closing the connection ends the session. Display only: AirPlay mirroring has no input channel to the phone.
// Android Auto has priority: no mirror session starts while it is connected, and a running one is dropped when
// Android Auto connects.

#include <stddef.h>
#include <stdint.h>

namespace nbt {

class VideoManager;

// Starts the listener thread and, if NBT_MIRROR_CMD (default /opt/aa/airplay-mirrord) exists, the receiver.
// busy() returns true while Android Auto owns the screen. NBT_MIRROR=0 turns all of it off.
bool startMirrorInput(VideoManager* video, bool (*busy)());
void stopMirrorReceiver();     // shutdown: SIGTERM to the receiver started above
bool mirrorActive();           // a mirroring session is feeding the decoder

// Byte length of the SPS / PPS NAL units (types 7, 8) at the start of an Annex-B access unit; 0 if it does not start
// with one, n if the whole unit is config. The mirror input posts them through postCodecConfig(), the way Android
// Auto delivers its codec config.
inline size_t leadingConfigLen(const uint8_t* d, size_t n) {
    size_t pos = 0;
    for (;;) {
        size_t sc = (pos + 4 <= n && d[pos] == 0 && d[pos + 1] == 0 && d[pos + 2] == 0 && d[pos + 3] == 1) ? 4
                  : (pos + 3 <= n && d[pos] == 0 && d[pos + 1] == 0 && d[pos + 2] == 1) ? 3 : 0;
        if (!sc || pos + sc >= n) return pos;
        int type = d[pos + sc] & 0x1F;
        if (type != 7 && type != 8) return pos;
        size_t next = pos + sc + 1;   // next start code ends this NAL
        while (next + 3 <= n && !(d[next] == 0 && d[next + 1] == 0 && d[next + 2] == 1)) ++next;
        if (next + 3 > n) return n;    // this config NAL runs to the end: the whole unit is config
        if (next > 0 && d[next - 1] == 0) --next;   // 4-byte start code
        pos = next;
    }
}

} // namespace nbt

#endif // MIRROR_INPUT_HPP
