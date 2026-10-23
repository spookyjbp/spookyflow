/* ps3_webman.c - send a web command to webMAN MOD on this console
 * (http://127.0.0.1/<command>): clean power off, and game launching.
 *
 * Verified against PSL1GHT ppu/include/net/net.h (netInitialize,
 * netDeinitialize, netSocket, netConnect, netSend, netRecv, netClose,
 * netSetSockOpt), net/socket.h (SOL_SOCKET, SO_RCVTIMEO, SO_SNDTIMEO) and
 * arpa/inet.h (htons/htonl are no-ops: the PS3 is big-endian). */
#include <net/net.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include "log.h"
#include "ps3_platform.h"

/* connect to webMAN; returns the socket or -1 (network initialized on success) */
static s32 webman_connect(void)
{
    struct sockaddr_in addr;
    struct timeval tv;
    s32 s;
    if (netInitialize() < 0) { log_printf("webman: network init failed"); return -1; }
    s = netSocket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s < 0) { log_printf("webman: socket failed (%d)", (int)s); netDeinitialize(); return -1; }
    tv.tv_sec = 2; tv.tv_usec = 0;
    netSetSockOpt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    netSetSockOpt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    addr.sin_addr.s_addr = htonl(0x7F000001);          /* 127.0.0.1 */
    if (netConnect(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_printf("webman: can't connect to 127.0.0.1:80 - is webMAN MOD running?");
        netClose(s);
        netDeinitialize();
        return -1;
    }
    return s;
}

/* Send a request that webMAN answers only later (a launch: it first waits for
 * the XMB). 0 = connected and sent. */
int ps3_webman_send(const char *command)
{
    char req[2300];
    s32 s;
    int n, rc = -1;
    n = snprintf(req, sizeof(req), "GET %s HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n", command);
    if (n <= 0 || n >= (int)sizeof(req)) { log_printf("webman: request too long"); return -1; }
    s = webman_connect();
    if (s < 0) return -1;
    if (netSend(s, req, (size_t)n, 0) == n) {
        rc = 0;
        usleep(300 * 1000);            /* let webMAN read it before the socket closes */
        log_printf("webman: sent %s", command);
    } else {
        log_printf("webman: send failed for %s", command);
    }
    netClose(s);
    netDeinitialize();
    return rc;
}

int ps3_webman_get(const char *command)
{
    char req[512], resp[64];
    s32 s;
    int n, rc = -1;
    n = snprintf(req, sizeof(req), "GET %s HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n", command);
    if (n <= 0 || n >= (int)sizeof(req)) { log_printf("webman: request too long"); return -1; }
    s = webman_connect();
    if (s < 0) return -1;
    if (netSend(s, req, (size_t)n, 0) == n) {
        /* any reply means webMAN took the command */
        n = (int)netRecv(s, resp, sizeof(resp) - 1, 0);
        if (n > 0) { resp[n] = '\0'; rc = 0; log_printf("webman: %s -> %.12s", command, resp); }
        else log_printf("webman: %s sent, no reply", command);
    } else {
        log_printf("webman: send failed for %s", command);
    }
    netClose(s);
    netDeinitialize();
    return rc;
}

/* Send a command and keep the text of webMAN's answer (HTML tags removed,
 * whitespace squeezed; the part from " => " on when there is one, which is
 * where /xmb.ps3$xregistry(...) puts the value). 0 = webMAN answered. */
int ps3_webman_query(const char *command, char *out, size_t cap)
{
    char req[512], buf[4096];
    s32 s;
    int n, got = 0;
    if (cap) out[0] = '\0';
    n = snprintf(req, sizeof(req), "GET %s HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n", command);
    if (n <= 0 || n >= (int)sizeof(req)) return -1;
    s = webman_connect();
    if (s < 0) return -1;
    if (netSend(s, req, (size_t)n, 0) == n) {
        while (got < (int)sizeof(buf) - 1) {
            int r = (int)netRecv(s, buf + got, sizeof(buf) - 1 - (size_t)got, 0);
            if (r <= 0) break;
            got += r;
        }
    }
    netClose(s);
    netDeinitialize();
    if (got <= 0) return -1;
    buf[got] = '\0';
    {
        const char *body = strstr(buf, "\r\n\r\n"), *p, *arrow;
        size_t o = 0;
        int intag = 0, sp = 0;
        char text[4096];
        body = body ? body + 4 : buf;
        for (p = body; *p && o + 1 < sizeof(text); p++) {
            if (*p == '<') { intag = 1; continue; }
            if (*p == '>') { intag = 0; sp = 1; continue; }
            if (intag) continue;
            if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') { sp = 1; continue; }
            if (sp && o) text[o++] = ' ';
            sp = 0;
            text[o++] = *p;
        }
        text[o] = '\0';
        arrow = strstr(text, "=>");
        snprintf(out, cap, "%s", arrow ? arrow : text);
    }
    return 0;
}
