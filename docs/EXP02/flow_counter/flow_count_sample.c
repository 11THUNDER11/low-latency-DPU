#include <string.h>
#include <unistd.h>
#include <rte_byteorder.h>
#include <doca_log.h>
#include <doca_flow.h>
#include "flow_common.h"

DOCA_LOG_REGISTER(FLOW_PROTO_DROP);

/* Create pipe with DROP as default */
static doca_error_t create_proto_drop_pipe(struct doca_flow_port *port, struct doca_flow_pipe **pipe)
{
    struct doca_flow_match match;
    struct doca_flow_monitor monitor;
    struct doca_flow_fwd fwd;
    struct doca_flow_fwd fwd_miss;
    struct doca_flow_pipe_cfg *pipe_cfg;
    doca_error_t result;

    memset(&match, 0, sizeof(match));
    memset(&monitor, 0, sizeof(monitor));
    memset(&fwd, 0, sizeof(fwd));
    memset(&fwd_miss, 0, sizeof(fwd_miss));

    /* Match template: L4 protocol */
    match.outer.l4_type_ext = 0xff;

    /* Enable counters */
    monitor.counter_type = DOCA_FLOW_RESOURCE_TYPE_NON_SHARED;

    result = doca_flow_pipe_cfg_create(&pipe_cfg, port);
    if (result != DOCA_SUCCESS)
        return result;

    set_flow_pipe_cfg(pipe_cfg, "PROTO_DROP_PIPE", DOCA_FLOW_PIPE_BASIC, true);

    doca_flow_pipe_cfg_set_match(pipe_cfg, &match, NULL);
    doca_flow_pipe_cfg_set_monitor(pipe_cfg, &monitor);

    /* Drop packets that match */
    fwd.type = DOCA_FLOW_FWD_DROP;

    /* Drop packets that miss */
    fwd_miss.type = DOCA_FLOW_FWD_DROP;

    result = doca_flow_pipe_create(pipe_cfg, &fwd, &fwd_miss, pipe);

    doca_flow_pipe_cfg_destroy(pipe_cfg);

    return result;
}

/* IMPORTANT: name must be flow_count */
doca_error_t flow_count(int nb_queues)
{
    const int nb_ports = 2;

    struct flow_resources resource = { .nr_counters = 10 };
    uint32_t nr_shared_resources[SHARED_RESOURCE_NUM_VALUES] = {0};

    struct doca_flow_port *ports[2];
    struct doca_dev *dev_arr[2];

    memset(ports, 0, sizeof(ports));
    memset(dev_arr, 0, sizeof(dev_arr));

    struct doca_flow_pipe *pipe;
    struct doca_flow_pipe_entry *tcp_entry;
    struct doca_flow_pipe_entry *udp_entry;

    struct doca_flow_query stats;
    struct entries_status status = {0};

    struct doca_flow_actions actions = {0};

    doca_error_t result;

    /* Initialize DOCA Flow */
    result = init_doca_flow(nb_queues, "vnf,hws", &resource, nr_shared_resources);
    if (result != DOCA_SUCCESS)
        return result;

    /* Initialize ports */
    result = init_doca_flow_ports(nb_ports, ports, true, dev_arr);
    if (result != DOCA_SUCCESS)
        return result;

    /* Create pipe */
    result = create_proto_drop_pipe(ports[0], &pipe);
    if (result != DOCA_SUCCESS)
        goto stop_ports;

    /* TCP rule */
    struct doca_flow_match tcp_match;
    memset(&tcp_match, 0, sizeof(tcp_match));

    tcp_match.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_TCP;

    result = doca_flow_pipe_add_entry(
        0,
        pipe,
        &tcp_match,
        &actions,
        NULL,
        NULL,
        0,
        &status,
        &tcp_entry
    );

    if (result != DOCA_SUCCESS)
        goto stop_ports;

    /* UDP rule */
    struct doca_flow_match udp_match;
    memset(&udp_match, 0, sizeof(udp_match));

    udp_match.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_UDP;

    result = doca_flow_pipe_add_entry(
        0,
        pipe,
        &udp_match,
        &actions,
        NULL,
        NULL,
        0,
        &status,
        &udp_entry
    );

    if (result != DOCA_SUCCESS)
        goto stop_ports;

    /* Push rules to hardware */
    doca_flow_entries_process(ports[0], 0, DEFAULT_TIMEOUT_US, 0);

    DOCA_LOG_INFO("Hardware firewall active (BF3)");
    DOCA_LOG_INFO("Counting TCP/UDP packets for 15 seconds...");

    sleep(15);

    doca_flow_query_entry(tcp_entry, &stats);
    DOCA_LOG_INFO("TCP Packets: %ld", stats.total_pkts);

    doca_flow_query_entry(udp_entry, &stats);
    DOCA_LOG_INFO("UDP Packets: %ld", stats.total_pkts);

stop_ports:

    stop_doca_flow_ports(nb_ports, ports);

    doca_flow_destroy();

    return result;
}