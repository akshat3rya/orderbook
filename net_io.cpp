#include "protocol.hpp"
#include <sys/socket.h>
#include <errno.h>

// TCP makes no promise that one send() on one end arrives as one recv() on
// the other — a message can (and, under load, will) show up split across
// multiple recv() calls. This loop is the actual "prove framing/parsing"
// deliverable for Day 3: everything else is just structs.
bool readExact(int fd, void* buf, size_t n) {
    char* p = static_cast<char*>(buf);
    size_t total = 0;
    while (total < n) {
        ssize_t r = recv(fd, p + total, n - total, 0);
        if (r > 0) {
            total += static_cast<size_t>(r);
        } else if (r == 0) {
            return false; // peer closed — clean EOF if total==0, a framing
                           // violation (mid-message close) if total>0; either
                           // way there's no more data coming, so it's fatal
                           // to this read either way.
        } else { // r < 0
            if (errno == EINTR) continue; // interrupted by a signal, just retry
            return false; // real error (ECONNRESET etc.)
        }
    }
    return true;
}

bool writeExact(int fd, const void* buf, size_t n) {
    const char* p = static_cast<const char*>(buf);
    size_t total = 0;
    while (total < n) {
        ssize_t w = send(fd, p + total, n - total, 0);
        if (w > 0) {
            total += static_cast<size_t>(w);
        } else if (w < 0 && errno == EINTR) {
            continue; // interrupted, retry
        } else {
            return false; // w == 0 (shouldn't happen for a blocking stream
                           // socket with n>0) or a real error like EPIPE
        }
    }
    return true;
}
