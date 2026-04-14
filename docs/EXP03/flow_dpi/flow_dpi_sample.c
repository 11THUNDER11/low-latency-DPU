#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <doca_log.h>
#include <doca_flow.h>

#include "flow_common.h"
#include "flow_dpi_worker.h"

DOCA_LOG_REGISTER(FLOW_DPI::SAMPLE);

doca_error_t
flow_dpi(int nb_queues)
{
    struct flow_resources resource = { .nr_counters = 10 };
    uint32_t nr_shared_resources[SHARED_RESOURCE_NUM_VALUES] = {0};
    struct doca_flow_port *ports[1];
    struct doca_dev *dev_arr[1];
    struct doca_flow_pipe *control_pipe;
    struct doca_flow_pipe_entry *entry;
    struct entries_status status = {0};
    struct doca_flow_match match = {0};
    struct doca_flow_match mask = {0};
    
    /* AZIONE RSS: Inoltra i pacchetti che matchano alla coda 0 della CPU ARM */
    uint16_t rss_queues[1] = {0}; 
    struct doca_flow_fwd fwd_rss = {
        .type = DOCA_FLOW_FWD_RSS,
        .rss_queues = rss_queues,
        .num_of_queues = 1
    };
    
    struct doca_flow_pipe_cfg *cfg;
    doca_error_t result;

    /* 1. Init DOCA Flow */
    result = init_doca_flow(nb_queues, "vnf,hws", &resource, nr_shared_resources);
    if (result != DOCA_SUCCESS) return result;

    result = init_doca_flow_ports(1, ports, false, dev_arr);
    if (result != DOCA_SUCCESS) return result;

    /* 2. Build Control Pipe */
    result = doca_flow_pipe_cfg_create(&cfg, ports[0]);
    if (result != DOCA_SUCCESS) return result;

    set_flow_pipe_cfg(cfg, "DPI_PIPE", DOCA_FLOW_PIPE_CONTROL, true);
    result = doca_flow_pipe_create(cfg, NULL, NULL, &control_pipe);
    doca_flow_pipe_cfg_destroy(cfg);
    if (result != DOCA_SUCCESS) return result;

    /* 3. Match UDP Traffic (e.g., Trading Multicast Feed) */
    memset(&match, 0, sizeof(match));
    memset(&mask, 0, sizeof(mask));
    match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
    match.outer.ip4.next_proto = IPPROTO_UDP;
    mask.outer.l3_type = 0xff;
    mask.outer.ip4.next_proto = 0xff;

    /* Aggiunta entry: Match UDP -> Action RSS (Send to ARM) */
    result = doca_flow_pipe_control_add_entry(0, 1, control_pipe, &match, &mask,
                                             NULL, NULL, NULL, NULL, NULL,
                                             &fwd_rss, &status, &entry);
    if (result != DOCA_SUCCESS) return result;

    /* 4. Push rules to Hardware */
    doca_flow_entries_process(ports[0], 0, DEFAULT_TIMEOUT_US, 0);
    DOCA_LOG_INFO("Hardware logic set to RSS. Starting software worker...");

    /* 5. Passaggio di consegne al Worker (in flow_dpi_worker.c) */
    run_dpi_worker(0, 0); 

    /* Cleanup (eseguito se il worker termina) */
    stop_doca_flow_ports(1, ports);
    doca_flow_destroy();

    return DOCA_SUCCESS;
}
