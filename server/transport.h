#ifndef __PRISKV_SERVER_UCP__
#define __PRISKV_SERVER_UCP__

#if defined(__cplusplus)
extern "C" {
#endif

#include <limits.h>
#include <stdbool.h>

#include "priskv-protocol.h"

#define PRISKV_TRANSPORT_MAX_BIND_ADDR 32
#define PRISKV_TRANSPORT_DEFAULT_PORT ('H' << 8 | 'P')
#define PRISKV_TRANSPORT_MAX_INFLIGHT_COMMAND 4096
#define PRISKV_TRANSPORT_DEFAULT_INFLIGHT_COMMAND 128
#define PRISKV_TRANSPORT_MAX_SGL 8
#define PRISKV_TRANSPORT_DEFAULT_SGL 4
#define PRISKV_TRANSPORT_MAX_KEY (1 << 30)
#define PRISKV_TRANSPORT_DEFAULT_KEY (16 * 1024)
#define PRISKV_TRANSPORT_MAX_KEY_LENGTH 1024
#define PRISKV_TRANSPORT_DEFAULT_KEY_LENGTH 128
#define PRISKV_TRANSPORT_MAX_VALUE_BLOCK_SIZE (1 << 20)
#define PRISKV_TRANSPORT_DEFAULT_VALUE_BLOCK_SIZE 4096
#define PRISKV_TRANSPORT_MAX_VALUE_BLOCK (1UL << 30)
#define PRISKV_TRANSPORT_DEFAULT_VALUE_BLOCK (1024UL * 1024)
#define SLOW_QUERY_THRESHOLD_LATENCY_US 1000000

extern uint32_t g_slow_query_threshold_latency_us;

typedef struct priskv_transport_stats {
    uint64_t ops;
    uint64_t bytes;
} priskv_transport_stats;

typedef struct priskv_transport_conn_cap {
    uint16_t max_sgl;
    uint16_t max_key_length;
    uint16_t max_inflight_command;
} priskv_transport_conn_cap;

typedef struct priskv_transport_client {
    char address[PRISKV_ADDR_LEN];
    priskv_transport_stats stats[PRISKV_COMMAND_MAX];
    uint64_t resps;
    bool closing;
} priskv_transport_client;

typedef struct priskv_transport_listener {
    char address[PRISKV_ADDR_LEN];
    int nclients;
    priskv_transport_client *clients;
} priskv_transport_listener;

int priskv_transport_listen(char **addr, int naddr, int port, void *kv, priskv_transport_conn_cap *cap);
int priskv_transport_get_fd(void);
void priskv_transport_process(void);
void *priskv_transport_get_kv(void);
priskv_transport_listener *priskv_transport_get_listeners(int *nlisteners);
void priskv_transport_free_listeners(priskv_transport_listener *listeners, int nlisteners);

int priskv_transport_send_response(ucp_ep_h ep, uint64_t request_id, priskv_resp_status status, uint32_t length);
int priskv_transport_rw_req(ucp_ep_h ep, priskv_request *req, uint8_t *buf, uint32_t valuelen, int is_set, void (*cb)(void *), void *cbarg);

#if defined(__cplusplus)
}
#endif

#endif
