// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "mirror_input.hpp"
#include "video_manager.hpp"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#ifdef __QNXNTO__
#include <process.h>
#endif

namespace nbt {

static VideoManager* s_video = NULL;
static bool (*s_busy)() = NULL;
static volatile bool s_active = false;
static int s_listen = -1;
static pid_t s_receiver = -1;

bool mirrorActive() { return s_active; }

static bool readAll(int fd, uint8_t* p, size_t n) {
    while (n) {
        ssize_t r = read(fd, p, n);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return false;
        p += r;
        n -= (size_t)r;
    }
    return true;
}

static void serveClient(int fd) {
    s_active = true;   // the decoder is (re)built for the stream's size when the size message arrives
    printf("[Mirror] iPhone screen mirroring started\n");
    std::vector<uint8_t> buf, cfg;
    unsigned frames = 0;
    bool aa_took_over = false;
    for (;;) {
        uint8_t hdr[5];
        if (!readAll(fd, hdr, sizeof(hdr))) break;
        uint32_t len = ((uint32_t)hdr[1] << 24) | ((uint32_t)hdr[2] << 16) | ((uint32_t)hdr[3] << 8) | hdr[4];
        if (len > (8u << 20)) { printf("[Mirror] bad message length %u: dropping the session\n", len); break; }
        buf.resize(len);
        if (len && !readAll(fd, &buf[0], len)) break;
        if (s_busy && s_busy()) {
            printf("[Mirror] Android Auto connected: mirroring stops\n");
            aa_took_over = true;
            break;
        }
        if (hdr[0] == 1 && len >= 4) {
            unsigned w = (buf[0] << 8) | buf[1], h = (buf[2] << 8) | buf[3];
            printf("[Mirror] stream size %ux%u\n", w, h);   // portrait iPhone at 1280x720 offered: 332x720
            if (w && h) {   // first size, or the phone rotated: fresh decoder for this size, old frames dropped
                s_video->endSession();
                s_video->setSourceGeometry(w, h);
                s_video->prepareForSession();
                cfg.clear();
            }
        } else if (hdr[0] == 2 && len) {
            size_t c = leadingConfigLen(&buf[0], len);
            if (c && (cfg.size() != c || memcmp(&cfg[0], &buf[0], c) != 0)) {
                cfg.assign(buf.begin(), buf.begin() + c);
                s_video->postCodecConfig(&cfg[0], c);
            }
            if (c < len) s_video->postVideoFrame(&buf[c], len - c, 0);
            if (++frames == 1) printf("[Mirror] first frame (%u bytes, %u bytes SPS/PPS)\n", len, (unsigned)c);
        }
    }
    s_active = false;
    // Android Auto's connect already reset the size and re-prepared the decoder; endSession() would drop its SPS/PPS.
    if (!aa_took_over) {
        s_video->endSession();
        s_video->setSourceGeometry(0, 0);   // the next Android Auto connect rebuilds for its 1280x720 canvas
    }
    printf("[Mirror] iPhone screen mirroring ended (%u frames)\n", frames);
}

static void* listenThread(void*) {
    for (;;) {
        int fd = accept(s_listen, NULL, NULL);
        if (fd < 0) {
            if (errno == EINTR) continue;
            printf("[Mirror] accept failed: %s; mirror input stopped\n", strerror(errno));
            return NULL;
        }
        if (s_busy && s_busy()) {
            printf("[Mirror] mirroring refused: Android Auto is connected\n");
        } else {
            serveClient(fd);
        }
        close(fd);
    }
}

static void startReceiver() {
    const char* cmd = getenv("NBT_MIRROR_CMD");
    if (!cmd || !*cmd) cmd = "/opt/aa/airplay-mirrord";
    struct stat st;
    if (stat(cmd, &st) != 0) {
        printf("[Mirror] %s not installed: waiting for an externally started receiver\n", cmd);
        return;
    }
    const char* name = getenv("NBT_MIRROR_NAME");
    if (!name || !*name) name = "iDrive";
#ifdef __QNXNTO__
    // QNX: fork() is unsupported once the process has threads; spawn is. Output goes to our log (inherited stdout).
    pid_t pid = spawnl(P_NOWAIT, cmd, cmd, "-n", name, (char*)NULL);
#else
    pid_t pid = fork();
    if (pid == 0) {
        execl(cmd, cmd, "-n", name, (char*)NULL);
        _exit(127);
    }
#endif
    if (pid > 0) s_receiver = pid;
    printf("[Mirror] receiver %s \"%s\": %s\n", cmd, name, pid > 0 ? "started" : strerror(errno));
}

void stopMirrorReceiver() {
    if (s_receiver > 0) kill(s_receiver, SIGTERM);
    s_receiver = -1;
}

bool startMirrorInput(VideoManager* video, bool (*busy)()) {
    const char* en = getenv("NBT_MIRROR");
    if (en && en[0] == '0') { printf("[Mirror] off (NBT_MIRROR=0)\n"); return false; }
    s_video = video;
    s_busy = busy;
    const char* ep = getenv("NBT_MIRROR_PORT");
    uint16_t port = (ep && *ep) ? (uint16_t)atoi(ep) : 52791;
    s_listen = socket(AF_INET, SOCK_STREAM, 0);
    if (s_listen < 0) return false;
    int one = 1;
    setsockopt(s_listen, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // local receiver only, never the hotspot
    if (bind(s_listen, (struct sockaddr*)&sa, sizeof(sa)) != 0 || listen(s_listen, 1) != 0) {
        printf("[Mirror] cannot listen on 127.0.0.1:%u: %s\n", (unsigned)port, strerror(errno));
        close(s_listen);
        s_listen = -1;
        return false;
    }
    pthread_t t;
    if (pthread_create(&t, NULL, &listenThread, NULL) != 0) return false;
    pthread_detach(t);
    printf("[Mirror] input listening on 127.0.0.1:%u\n", (unsigned)port);
    startReceiver();
    return true;
}

} // namespace nbt
