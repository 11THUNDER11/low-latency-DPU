#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <doca_log.h>
#include <doca_flow.h>

#include "flow_common.h"
#include "trading_config.h"
#include "trading_worker.h"

DOCA_LOG_REGISTER(TRADING::SAMPLE);

/*
 * Crea la Control Pipe root.
 * Ogni entry corrisponde a un flusso dalla configurazione.
 * I flussi noti vengono inviati alla coda RSS 0 (ARM CPU → DPI).
 * Tutto il resto viene droppato.
 */
doca_error_t run_trading_pipeline(int nb_queues, const flow_config_t *cfg)
{
    struct flow_resources resource = { .nr_counters = MAX_FLOWS * 2 };
    uint32_t nr_shared_resources[SHARED_RESOURCE_NUM_VALUES] = {0};
    struct doca_flow_port *ports[1];
    struct doca_dev       *dev_arr[1];
    struct doca_flow_pipe *control_pipe;
    struct doca_flow_pipe_cfg *pipe_cfg;
    struct doca_flow_pipe_entry *entry;
    struct entries_status status = {0};
    doca_error_t result;

    /* Coda RSS verso ARM CPU */
    uint16_t rss_q[1] = {0};
    struct doca_flow_fwd fwd_rss = {
        .type         = DOCA_FLOW_FWD_RSS,
        .rss_queues   = rss_q,
        .num_of_queues = 1,
    };

    /* Drop per tutto il resto */
    struct doca_flow_fwd fwd_drop = { .type = DOCA_FLOW_FWD_DROP };

    /* Monitor con counter per ogni entry */
    struct doca_flow_monitor monitor = {
        .counter_type = DOCA_FLOW_RESOURCE_TYPE_NON_SHARED
    };

    /* 1. Init DOCA Flow */
    result = init_doca_flow(nb_queues, "vnf,hws", &resource, nr_shared_resources);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("init_doca_flow failed: %s", doca_error_get_descr(result));
        return result;
    }

    result = init_doca_flow_ports(1, ports, false, dev_arr);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("init_doca_flow_ports failed: %s", doca_error_get_descr(result));
        doca_flow_destroy();
        return result;
    }

    /* 2. Crea Control Pipe root */
    result = doca_flow_pipe_cfg_create(&pipe_cfg, ports[0]);
    if (result != DOCA_SUCCESS) goto cleanup;

    set_flow_pipe_cfg(pipe_cfg, "TRADING_PIPE", DOCA_FLOW_PIPE_CONTROL, true);
    result = doca_flow_pipe_create(pipe_cfg, NULL, NULL, &control_pipe);
    doca_flow_pipe_cfg_destroy(pipe_cfg);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Creazione pipe fallita: %s", doca_error_get_descr(result));
        goto cleanup;
    }

    /* 3. Installa una entry per ogni flusso in configurazione */
    DOCA_LOG_INFO("Installazione %d flussi in hardware...", cfg->count);

    for (int i = 0; i < cfg->count; i++) {
        const flow_key_t *k = &cfg->flows[i];
        struct doca_flow_match match = {0};
        struct doca_flow_match mask  = {0};

        /* Match sulla quintupla completa */
        match.outer.l3_type        = DOCA_FLOW_L3_TYPE_IP4;
        match.outer.ip4.src_ip     = k->src_ip;
        match.outer.ip4.dst_ip     = k->dst_ip;
        match.outer.ip4.next_proto = k->proto;

        mask.outer.l3_type         = 0xff;
        mask.outer.ip4.src_ip      = 0xffffffff;
        mask.outer.ip4.dst_ip      = 0xffffffff;
        mask.outer.ip4.next_proto  = 0xff;

        /* Porte L4 */
        if (k->proto == IPPROTO_UDP) {
            match.outer.udp.l4_port.src_port = k->src_port;
            match.outer.udp.l4_port.dst_port = k->dst_port;
            mask.outer.udp.l4_port.src_port  = 0xffff;
            mask.outer.udp.l4_port.dst_port  = 0xffff;
        } else {
            match.outer.tcp.l4_port.src_port = k->src_port;
            match.outer.tcp.l4_port.dst_port = k->dst_port;
            mask.outer.tcp.l4_port.src_port  = 0xffff;
            mask.outer.tcp.l4_port.dst_port  = 0xffff;
        }

        /*
         * Priorità = i+1 (1..N per i flussi noti).
         * Il flusso con indice 0 ha la priorità più alta.
         * L'entry catch-all DROP avrà priorità N+1.
         */
        result = doca_flow_pipe_control_add_entry(
            0,          /* queue_id */
            i + 1,      /* priority */
            control_pipe,
            &match, &mask,
            NULL, NULL, NULL, NULL,
            &monitor,   /* counter hardware */
            &fwd_rss,   /* → ARM CPU per DPI */
            &status,
            &entry);

        if (result != DOCA_SUCCESS) {
            DOCA_LOG_ERR("Installazione entry %d fallita: %s",
                         i, doca_error_get_descr(result));
            goto cleanup;
        }

        /* Log dell'entry installata */
        char src_s[INET_ADDRSTRLEN], dst_s[INET_ADDRSTRLEN];
        struct in_addr sa = { .s_addr = k->src_ip };
        struct in_addr da = { .s_addr = k->dst_ip };
        inet_ntop(AF_INET, &sa, src_s, sizeof(src_s));
        inet_ntop(AF_INET, &da, dst_s, sizeof(dst_s));
        DOCA_LOG_INFO("  [flow#%d] %s:%u -> %s:%u %s  [prio=%d]",
                      i,
                      src_s, ntohs(k->src_port),
                      dst_s, ntohs(k->dst_port),
                      (k->proto == IPPROTO_TCP) ? "TCP" : "UDP",
                      i + 1);
    }

    /* 4. Entry catch-all: DROP per tutto il traffico non configurato */
    {
        struct doca_flow_match empty = {0};
        result = doca_flow_pipe_control_add_entry(
            0,
            cfg->count + 1, /* priorità più bassa */
            control_pipe,
            &empty, &empty,
            NULL, NULL, NULL, NULL,
            NULL,       /* nessun counter per il drop */
            &fwd_drop,
            &status,
            &entry);

        if (result != DOCA_SUCCESS) {
            DOCA_LOG_ERR("Installazione entry catch-all fallita: %s",
                         doca_error_get_descr(result));
            goto cleanup;
        }
        DOCA_LOG_INFO("  [catch-all] tutto il resto → DROP [prio=%d]",
                      cfg->count + 1);
    }

    /* 5. Invia tutte le regole all'hardware */
    result = doca_flow_entries_process(ports[0], 0, DEFAULT_TIMEOUT_US, 0);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("entries_process fallito: %s", doca_error_get_descr(result));
        goto cleanup;
    }

    DOCA_LOG_INFO("Pipeline attiva. Avvio DPI worker...");

    /* 6. Avvia il worker DPI sulla CPU ARM */
    run_trading_worker(0, 0, cfg);

    /* Raggiunto solo se il worker termina (Ctrl+C) */
    result = DOCA_SUCCESS;

cleanup:
    stop_doca_flow_ports(1, ports);
    doca_flow_destroy();
    return result;
}
