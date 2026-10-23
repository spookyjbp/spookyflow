/* ps3_webman.c - send a web command to webMAN MOD on this console
 * (http://127.0.0.1/<command>). Used for a clean power off now and for game
 * launching later.
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
#include "log.h"
#include "ps3_platform.h"

int ps3_webman_get(const char *command)
{
    struct sockaddr_in addr;
    struct timeval tv;
    char req[512], resp[64];
    s32 s;
    int n, rc = -1;
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
    } else {
        n = snprintf(req, sizeof(req), "GET %s HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n", command);
        if (n > 0 && n < (int)sizeof(req) && netSend(s, req, (size_t)n, 0) == n) {
            /* any reply means webMAN took the command */
            n = (int)netRecv(s, resp, sizeof(resp) - 1, 0);
            if (n > 0) { resp[n] = '\0'; rc = 0; log_printf("webman: %s -> %.12s", command, resp); }
            else log_printf("webman: %s sent, no reply", command);
        } else {
            log_printf("webman: send failed for %s", command);
        }
    }
    netClose(s);
    netDeinitialize();
    return rc;
}
