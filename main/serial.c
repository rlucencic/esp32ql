/*
 * The QL's serial ports on the network: SER1 on TCP port 2301, SER2 on 2302
 * (`nc sinclair-ql.local 2301`). One client a port, the newest connection
 * replacing the one before. What the QL sends goes to the client, what the
 * client sends the QL receives (QDOS: OPEN #3,ser1 / LOAD ser1z / COPY
 * ser1z TO ...); with no client the QL's output is lost, as on a port with
 * nothing plugged in. Bytes are passed as they are (no telnet negotiation:
 * use nc, or telnet in raw mode).
 */
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "ql.h"
#include "serial.h"

#define PORT_BASE       2301
#define BUF             512

static const char *TAG = "serial";

static int s_listen[QL_SER_PORTS] = { -1, -1 };
static int s_client[QL_SER_PORTS] = { -1, -1 };
static volatile bool s_connected[QL_SER_PORTS];

/* Bytes read from a client the QL hasn't taken yet, and bytes from the QL
 * the client hasn't taken yet
 */
static struct {
    uint8_t data[BUF];
    int pos, len;
} s_in[QL_SER_PORTS], s_out[QL_SER_PORTS];

static void drop(int p)
{
    if (s_client[p] >= 0) {
        close(s_client[p]);
        s_client[p] = -1;
    }
    s_connected[p] = false;
    ql_ser_connect(p, false);
    s_in[p].pos = s_in[p].len = 0;
    s_out[p].pos = s_out[p].len = 0;
}

static int listener(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        return -1;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(fd, 1) != 0) {
        close(fd);
        return -1;
    }
    fcntl(fd, F_SETFL, O_NONBLOCK);
    return fd;
}

static void serial_task(void *arg)
{
    for (int p = 0; p < QL_SER_PORTS; p++) {
        s_listen[p] = listener(PORT_BASE + p);
        if (s_listen[p] < 0) {
            ESP_LOGW(TAG, "SER%d: no TCP port %d", p + 1, PORT_BASE + p);
        }
    }
    for (;;) {
        fd_set rd;
        FD_ZERO(&rd);
        int top = -1;
        for (int p = 0; p < QL_SER_PORTS; p++) {
            if (s_listen[p] >= 0) {
                FD_SET(s_listen[p], &rd);
                top = s_listen[p] > top ? s_listen[p] : top;
            }
            if (s_client[p] >= 0 && s_in[p].pos >= s_in[p].len) {
                FD_SET(s_client[p], &rd);       /* more from the client once the QL took the last */
                top = s_client[p] > top ? s_client[p] : top;
            }
        }
        struct timeval tv = { .tv_sec = 0, .tv_usec = 20000 };
        if (top < 0) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        select(top + 1, &rd, NULL, NULL, &tv);

        for (int p = 0; p < QL_SER_PORTS; p++) {
            if (s_listen[p] >= 0 && FD_ISSET(s_listen[p], &rd)) {
                int fd = accept(s_listen[p], NULL, NULL);
                if (fd >= 0) {
                    drop(p);                    /* the newest connection wins */
                    int one = 1;
                    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
                    fcntl(fd, F_SETFL, O_NONBLOCK);
                    s_client[p] = fd;
                    s_connected[p] = true;
                    ql_ser_connect(p, true);
                    ESP_LOGI(TAG, "SER%d: a client on TCP port %d", p + 1, PORT_BASE + p);
                }
            }
            if (s_client[p] < 0) {
                continue;
            }
            /* client to the QL */
            if (FD_ISSET(s_client[p], &rd)) {
                int n = recv(s_client[p], s_in[p].data, BUF, 0);
                if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                    ESP_LOGI(TAG, "SER%d: the client left", p + 1);
                    drop(p);
                    continue;
                }
                if (n > 0) {
                    s_in[p].pos = 0;
                    s_in[p].len = n;
                }
            }
            if (s_in[p].pos < s_in[p].len) {
                s_in[p].pos += ql_ser_give(p, s_in[p].data + s_in[p].pos, s_in[p].len - s_in[p].pos);
            }
            /* the QL to the client */
            if (s_out[p].pos >= s_out[p].len) {
                s_out[p].pos = 0;
                s_out[p].len = ql_ser_take(p, s_out[p].data, BUF);
            }
            if (s_out[p].pos < s_out[p].len) {
                int n = send(s_client[p], s_out[p].data + s_out[p].pos, s_out[p].len - s_out[p].pos, MSG_DONTWAIT);
                if (n > 0) {
                    s_out[p].pos += n;
                } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    drop(p);
                }
            }
        }
    }
}

/* Its stack in PSRAM, and started once the machine runs (its speaker set
 * up): the MaTouch's internal RAM is short, and the speaker's I2S buffers
 * come first (without them no sound, and the emulation, paced by the
 * sound, at 0 frames a second: CLAUDE.md's lessons).
 */
void serial_start(void)
{
    static bool started;
    if (!started) {
        started = true;
        if (xTaskCreatePinnedToCoreWithCaps(serial_task, "serial", 4096, NULL, 3, NULL, 0,
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
            ESP_LOGW(TAG, "no serial task");
        }
    }
}

bool serial_connected(int port)
{
    return port >= 0 && port < QL_SER_PORTS && s_connected[port];
}

int serial_tcp_port(int port)
{
    return PORT_BASE + port;
}
