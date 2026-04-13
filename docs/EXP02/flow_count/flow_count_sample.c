#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <doca_log.h>
#include <doca_flow.h>
#include "flow_common.h"

DOCA_LOG_REGISTER(FLOW_COUNT);

doca_error_t
flow_count(int nb_queues)
{
    struct flow_resources resource = { .nr_counters = 10 };
    uint32_t nr_shared_resources[SHARED_RESOURCE_NUM_VALUES] = {0};
    struct doca_flow_port *ports[1];
    struct doca_dev *dev_arr[1];
    struct doca_flow_pipe *control_pipe;
    struct doca_flow_pipe_entry *tcp_e, *udp_e, *other_e;
    struct doca_flow_query stats;
    struct entries_status status = {0};
    struct doca_flow_match match = {0};
    struct doca_flow_match mask = {0};
    struct doca_flow_monitor monitor = {.counter_type = DOCA_FLOW_RESOURCE_TYPE_NON_SHARED};
    struct doca_flow_fwd fwd_drop = {.type = DOCA_FLOW_FWD_DROP};
    struct doca_flow_pipe_cfg *cfg;
    doca_error_t result;

    /* 1. Initialize DOCA Flow */
    result = init_doca_flow(nb_queues, "vnf,hws", &resource, nr_shared_resources);
    if (result != DOCA_SUCCESS) return result;

    result = init_doca_flow_ports(1, ports, false, dev_arr);
    if (result != DOCA_SUCCESS) return result;

    /* 2. Create the Control Pipe (The Manual Override) */
    result = doca_flow_pipe_cfg_create(&cfg, ports[0]);
    if (result != DOCA_SUCCESS) return result;

    set_flow_pipe_cfg(cfg, "CONTROL_PIPE", DOCA_FLOW_PIPE_CONTROL, true);
    result = doca_flow_pipe_create(cfg, NULL, NULL, &control_pipe);
    doca_flow_pipe_cfg_destroy(cfg);
    if (result != DOCA_SUCCESS) return result;

    /* 3. Add Entries with Explicit Priorities */

    /* TCP Entry (Priority 1) */
    memset(&match, 0, sizeof(match));
    memset(&mask, 0, sizeof(mask));
    match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
    match.outer.ip4.next_proto = IPPROTO_TCP; // 6
    mask.outer.l3_type = 0xff;
    mask.outer.ip4.next_proto = 0xff;

    result = doca_flow_pipe_control_add_entry(0, 1, control_pipe, &match, &mask,
                                             NULL, NULL, NULL, NULL, &monitor,
                                             &fwd_drop, &status, &tcp_e);
    if (result != DOCA_SUCCESS) return result;

    /* UDP Entry (Priority 2) */
    memset(&match, 0, sizeof(match));
    match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
    match.outer.ip4.next_proto = IPPROTO_UDP; // 17
    // Use same mask as above
    result = doca_flow_pipe_control_add_entry(0, 2, control_pipe, &match, &mask,
                                             NULL, NULL, NULL, NULL, &monitor,
                                             &fwd_drop, &status, &udp_e);
    if (result != DOCA_SUCCESS) return result;

    /* Catch-all Entry (Priority 3) */
    memset(&match, 0, sizeof(match));
    memset(&mask, 0, sizeof(mask)); // Zero mask matches everything
    result = doca_flow_pipe_control_add_entry(0, 3, control_pipe, &match, &mask,
                                             NULL, NULL, NULL, NULL, &monitor,
                                             &fwd_drop, &status, &other_e);
    if (result != DOCA_SUCCESS) return result;

    /* 4. Process entries to hardware */
    doca_flow_entries_process(ports[0], 0, DEFAULT_TIMEOUT_US, 0);
    DOCA_LOG_INFO("Hardware monitoring active (Control Pipe Mode).");

    /* 5. Statistics Loop */
    for (int i = 0; i < 20; i++) {
        sleep(5);

        doca_flow_query_entry(tcp_e, &stats);
        uint64_t t = stats.total_pkts;

        doca_flow_query_entry(udp_e, &stats);
        uint64_t u = stats.total_pkts;

        doca_flow_query_entry(other_e, &stats);
        uint64_t o = stats.total_pkts;

        DOCA_LOG_INFO("[STATS] TCP: %lu | UDP: %lu | OTHER: %lu", t, u, o);
    }

    /* 6. Cleanup */
    stop_doca_flow_ports(1, ports);
    doca_flow_destroy();

    return DOCA_SUCCESS;
}