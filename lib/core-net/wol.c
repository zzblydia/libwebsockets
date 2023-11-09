/*
 * libwebsockets - small server side websockets and web server implementation
 *
 * Copyright (C) 2010 - 2023 Andy Green <andy@warmcat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "private-lib-core.h"

int
lws_wol(struct lws_context *ctx, const char *ip_or_NULL, uint8_t *mac_6_bytes)
{
        int n, m, ofs = 0, fd, optval = 1, ret = 1;
        uint8_t pkt[17 * IFHWADDRLEN];
        struct sockaddr_in addr;

        fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (fd < 0) {
                lwsl_cx_err(ctx, "failed to open UDP, errno %d\n", errno);
                goto bail;
        }

        if (setsockopt(fd, SOL_SOCKET, SO_BROADCAST,
                        (char *)&optval, sizeof(optval)) < 0) {
                lwsl_cx_err(ctx, "failed to set broadcast, errno %d\n", errno);
                goto bail;
        }

        /*
         * Lay out the magic packet
         */

        for (n = 0; n < IFHWADDRLEN; n++)
                pkt[ofs++] = 0xff;
        for (m = 0; m < 16; m++)
                for (n = 0; n < IFHWADDRLEN; n++)
                        pkt[ofs++] = mac_6_bytes[n];

        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(9);

        if (!inet_aton(ip_or_NULL ? ip_or_NULL : "255.255.255.255",
                        &addr.sin_addr)) {
                lwsl_cx_err(ctx, "failed to convert broadcast ads, errno %d\n",
                                 errno);
                goto bail;
        }

        lwsl_cx_notice(ctx, "Sending WOL to %02X:%02X:%02X:%02X:%02X:%02X %s\n",
                mac_6_bytes[0], mac_6_bytes[1], mac_6_bytes[2], mac_6_bytes[3],
                mac_6_bytes[4], mac_6_bytes[5], ip_or_NULL ? ip_or_NULL : "");

        if (sendto(fd, pkt, sizeof(pkt), 0, (struct sockaddr *)&addr,
                        sizeof(addr)) < 0) {
                lwsl_cx_err(ctx, "failed to sendto broadcast ads, errno %d\n",
                                 errno);
                goto bail;
        }

        ret = 0;

bail:
        close(fd);

        return ret;
}
