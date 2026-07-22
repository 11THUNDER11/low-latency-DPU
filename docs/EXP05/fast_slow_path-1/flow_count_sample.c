#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdbool.h>

#include <rte_mbuf.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>

#include <doca_log.h>
#include <doca_flow.h>

#include "flow_common.h"

DOCA_LOG_REGISTER(FLOW_COUNT_QUINTUPLE);

#define MAX_FLOWS    1024
#define BURST_SIZE   32
#define ETH_HDR_LEN  14
#define IP_HDR_LEN   20
#define UDP_HDR_LEN   8
#define TCP_HDR_LEN  20

//struct doca_flow_counter *my_counters[MAX_FLOWS];

extern bool force_quit;

typedef struct {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t  proto;
    struct doca_flow_pipe_entry *hw_entry;
    uint64_t sw_pkts;   /* pacchetti contati in software (primo + pre-offload) */
    bool active;
} flow_record_t;

static flow_record_t flow_table[MAX_FLOWS];
static int           n_flows = 0;

void dump_pipe_to_report(struct doca_flow_pipe *pipe, const char *pipe_name) {
    char filename[128];
    snprintf(filename, sizeof(filename), "report_%s.txt", pipe_name);
    
    FILE *fp = fopen(filename, "w");
    if (fp == NULL) {
        fprintf(stderr, "Error: Could not open file %s for dumping pipe %s\n", filename, pipe_name);
        return;
    }

    fprintf(fp, "=== Hardware Dump for Pipe: %s ===\n", pipe_name);
    doca_flow_pipe_dump(pipe, fp);
    
    fclose(fp);
    printf("Report for pipe '%s' generated: %s\n", pipe_name, filename);
}

static int find_flow(uint32_t src_ip, uint32_t dst_ip,
                     uint16_t src_port, uint16_t dst_port,
                     uint8_t proto)
{
    for (int i = 0; i < n_flows; i++) {
        flow_record_t *f = &flow_table[i];
        if (f->active &&
            f->src_ip   == src_ip   && f->dst_ip   == dst_ip   &&
            f->src_port == src_port && f->dst_port == dst_port &&
            f->proto    == proto)
            return i;
    }
    return -1;
}

static int install_flow_entry(struct doca_flow_port *port,
                              struct doca_flow_pipe *pipe,
                              uint32_t src_ip, uint32_t dst_ip,
                              uint16_t src_port, uint16_t dst_port,
                              uint8_t proto)
{
    if (n_flows >= MAX_FLOWS) {
        DOCA_LOG_WARN("Tabella flussi piena (%d)", MAX_FLOWS);
        return -1;
    }

    struct doca_flow_match match = {0};

	struct doca_flow_monitor monitor = {
        .counter_type = DOCA_FLOW_RESOURCE_TYPE_NON_SHARED
    };
    
struct doca_flow_fwd fwd = { .type = DOCA_FLOW_FWD_DROP };
    struct entries_status status = {0};
    struct doca_flow_pipe_entry *entry;
    doca_error_t result;

match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
    match.outer.ip4.src_ip     = src_ip;
    match.outer.ip4.dst_ip     = dst_ip;

    if (proto == IPPROTO_TCP) {
	match.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_TCP;
        match.outer.tcp.l4_port.src_port = src_port;
        match.outer.tcp.l4_port.dst_port = dst_port;
    } else {
	match.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_UDP;
        match.outer.udp.l4_port.src_port = src_port;
        match.outer.udp.l4_port.dst_port = dst_port;
    }

    result = doca_flow_pipe_add_entry(
        0, pipe, &match, NULL, &monitor, &fwd,
        DOCA_FLOW_NO_WAIT, &status, &entry);

    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("add_entry fallito: %s", doca_error_get_descr(result));
        return -1;
    }

    doca_flow_entries_process(port, 0, 10000, 1);

    if (status.failure) {
        DOCA_LOG_ERR("Entry non accettata dall'hardware");
        return -1;
    }

    flow_table[n_flows] = (flow_record_t){
        .src_ip   = src_ip,   .dst_ip   = dst_ip,
        .src_port = src_port, .dst_port = dst_port,
        .proto    = proto,
        .hw_entry = entry,
        .sw_pkts  = 0,
        .active   = true
    };

    char src_s[INET_ADDRSTRLEN], dst_s[INET_ADDRSTRLEN];
    struct in_addr sa = { .s_addr = src_ip };
    struct in_addr da = { .s_addr = dst_ip };
    inet_ntop(AF_INET, &sa, src_s, sizeof(src_s));
    inet_ntop(AF_INET, &da, dst_s, sizeof(dst_s));
    DOCA_LOG_INFO("[NEW FLOW #%d] %s:%u -> %s:%u %s",
                  n_flows,
                  src_s, ntohs(src_port),
                  dst_s, ntohs(dst_port),
                  proto == IPPROTO_TCP ? "TCP" : "UDP");

    return n_flows++;
}

static void process_packet(struct rte_mbuf *m,
                           struct doca_flow_port *port,
                           struct doca_flow_pipe *tcp_pipe,
                           struct doca_flow_pipe *udp_pipe)
{
    uint8_t  *data   = rte_pktmbuf_mtod(m, uint8_t *);
    uint32_t  pktlen = rte_pktmbuf_pkt_len(m);

    if (pktlen < ETH_HDR_LEN + IP_HDR_LEN) return;

    struct rte_ether_hdr *eth = (struct rte_ether_hdr *)data;
    if (rte_be_to_cpu_16(eth->ether_type) != RTE_ETHER_TYPE_IPV4) return;

    struct rte_ipv4_hdr *ip = (struct rte_ipv4_hdr *)(data + ETH_HDR_LEN);

    uint16_t src_port = 0, dst_port = 0;
    struct doca_flow_pipe *target_pipe = NULL; /* Pipe in cui inseriremo il flusso */

    if (ip->next_proto_id == IPPROTO_TCP) {
        if (pktlen < ETH_HDR_LEN + IP_HDR_LEN + TCP_HDR_LEN) return;
        struct rte_tcp_hdr *tcp = (struct rte_tcp_hdr *)(data + ETH_HDR_LEN + IP_HDR_LEN);
        src_port = tcp->src_port;
        dst_port = tcp->dst_port;
        target_pipe = tcp_pipe; /* Seleziona la pipe TCP */
    } else if (ip->next_proto_id == IPPROTO_UDP) {
        if (pktlen < ETH_HDR_LEN + IP_HDR_LEN + UDP_HDR_LEN) return;
        struct rte_udp_hdr *udp = (struct rte_udp_hdr *)(data + ETH_HDR_LEN + IP_HDR_LEN);
        src_port = udp->src_port;
        dst_port = udp->dst_port;
        target_pipe = udp_pipe; /* Seleziona la pipe UDP */
    } else {
        return;
    }

    int idx = find_flow(ip->src_addr, ip->dst_addr, src_port, dst_port, ip->next_proto_id);

    if (idx >= 0) {
        /* Flusso già noto in software (burst rapido prima dell'HW) */
        flow_table[idx].sw_pkts++;
        return;
    }

    /* Flusso nuovo: passiamo la target_pipe corretta! */
    int new_idx = install_flow_entry(port, target_pipe,
                                     ip->src_addr, ip->dst_addr,
                                     src_port, dst_port,
                                     ip->next_proto_id);
    if (new_idx >= 0)
        flow_table[new_idx].sw_pkts = 1;
}

static void print_report(void)
{
    char src_s[INET_ADDRSTRLEN], dst_s[INET_ADDRSTRLEN];
    struct in_addr sa, da;
    uint64_t total_pkts = 0;

    DOCA_LOG_INFO("==========================================");
    DOCA_LOG_INFO("  REPORT FLUSSI — %d flussi rilevati", n_flows);
    DOCA_LOG_INFO("==========================================");

    for (int i = 0; i < n_flows; i++) {
        flow_record_t *f = &flow_table[i];
        struct doca_flow_query stats = {0};

        if (f->hw_entry)
            doca_flow_query_entry(f->hw_entry, &stats);

        uint64_t total = stats.total_pkts + f->sw_pkts;

        sa.s_addr = f->src_ip; inet_ntop(AF_INET, &sa, src_s, sizeof(src_s));
        da.s_addr = f->dst_ip; inet_ntop(AF_INET, &da, dst_s, sizeof(dst_s));

        DOCA_LOG_INFO("[#%03d] %s:%5u -> %s:%5u  %-3s  pkts=%lu (hw=%lu sw=%lu)",
                      i,
                      src_s, ntohs(f->src_port),
                      dst_s, ntohs(f->dst_port),
                      f->proto == IPPROTO_TCP ? "TCP" : "UDP",
                      total,
                      stats.total_pkts,
                      f->sw_pkts);

        total_pkts += total;
    }

    DOCA_LOG_INFO("------------------------------------------");
    DOCA_LOG_INFO("  TOTALE: %d flussi, %lu pacchetti", n_flows, total_pkts);
    DOCA_LOG_INFO("==========================================");
}

doca_error_t flow_count(int nb_queues)
{
    struct flow_resources resource = { .nr_counters = MAX_FLOWS };
    uint32_t nr_shared_resources[SHARED_RESOURCE_NUM_VALUES] = {0};
    struct doca_flow_port *ports[1];
    struct doca_dev       *dev_arr[1];
    
    /* Pipeline pipes */
    struct doca_flow_pipe *root_pipe;
    struct doca_flow_pipe *tcp_pipe;
    struct doca_flow_pipe *udp_pipe;
    struct doca_flow_pipe *rss_miss_pipe;
    
    /* Config and management structures */
    struct doca_flow_pipe_cfg *cfg;
    struct doca_flow_pipe_entry *entry;
    struct entries_status status = {0};
    doca_error_t result;

    /* Forwarding configurations (Declared at the top to satisfy the compiler) */
    struct doca_flow_fwd fwd_drop = { .type = DOCA_FLOW_FWD_DROP };
    struct doca_flow_fwd pipe_miss_to_rss = {0}; 
    
    /* Common monitoring template */
    struct doca_flow_monitor mon_tmpl = {
        .counter_type = DOCA_FLOW_RESOURCE_TYPE_NON_SHARED
    };

    memset(flow_table, 0, sizeof(flow_table));
    n_flows = 0;

    result = init_doca_flow(nb_queues, "vnf,hws", &resource, nr_shared_resources);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("init_doca_flow: %s", doca_error_get_descr(result));
        return result;
    }

    result = init_doca_flow_ports(1, ports, false, dev_arr);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("init_doca_flow_ports: %s", doca_error_get_descr(result));
        doca_flow_destroy();
        return result;
    }


    /* ------------------------------------------------------------------
     * 1. RSS_MISS_PIPE — Intermediate pipe to handle fwd_miss in HWS
     * ------------------------------------------------------------------ */
    {
        uint16_t rss_q[1] = {0};
        struct doca_flow_fwd rss_fwd = {
            .type          = DOCA_FLOW_FWD_RSS,
            .rss_queues    = rss_q,
            .num_of_queues = 1,
        };
        
        struct doca_flow_match empty_match = {0};
        struct doca_flow_match empty_mask = {0};

        result = doca_flow_pipe_cfg_create(&cfg, ports[0]);
        if (result != DOCA_SUCCESS) goto cleanup;

        set_flow_pipe_cfg(cfg, "RSS_MISS_PIPE", DOCA_FLOW_PIPE_BASIC, false);
        doca_flow_pipe_cfg_set_match(cfg, &empty_match, &empty_mask);

        result = doca_flow_pipe_create(cfg, &rss_fwd, NULL, &rss_miss_pipe);
        doca_flow_pipe_cfg_destroy(cfg);
        if (result != DOCA_SUCCESS) {
            DOCA_LOG_ERR("RSS_MISS_PIPE: %s", doca_error_get_descr(result));
            goto cleanup;
        }

        result = doca_flow_pipe_add_entry(0, rss_miss_pipe, &empty_match, NULL, NULL, &rss_fwd, DOCA_FLOW_NO_WAIT, &status, &entry);
        if (result != DOCA_SUCCESS) {
            DOCA_LOG_ERR("RSS_MISS_PIPE entry error: %s", doca_error_get_descr(result));
            goto cleanup;
        }
    }

    /* Now that rss_miss_pipe is safely built, configure our miss target */
    pipe_miss_to_rss.type = DOCA_FLOW_FWD_PIPE;
    pipe_miss_to_rss.next_pipe = rss_miss_pipe;

    /* ------------------------------------------------------------------
     * 2. TCP_PIPE — non-root, BASIC, checks registered TCP flows
     * ------------------------------------------------------------------ */
	{
    

struct doca_flow_match match_tmpl = {0};

match_tmpl.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
match_tmpl.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_TCP;

/* campi che varieranno per entry */
match_tmpl.outer.ip4.src_ip = UINT32_MAX;
match_tmpl.outer.ip4.dst_ip = UINT32_MAX;
match_tmpl.outer.tcp.l4_port.src_port = UINT16_MAX;
match_tmpl.outer.tcp.l4_port.dst_port = UINT16_MAX;
    result = doca_flow_pipe_cfg_create(&cfg, ports[0]);
    if (result != DOCA_SUCCESS) goto cleanup;

    set_flow_pipe_cfg(cfg, "TCP_PIPE", DOCA_FLOW_PIPE_BASIC, false);
    
    // Pass NULL as the third parameter to activate Implicit Matching
    doca_flow_pipe_cfg_set_match(cfg, &match_tmpl, NULL);
    doca_flow_pipe_cfg_set_monitor(cfg, &mon_tmpl);

    result = doca_flow_pipe_create(cfg, &fwd_drop, &pipe_miss_to_rss, &tcp_pipe);
    doca_flow_pipe_cfg_destroy(cfg);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("TCP_PIPE: %s", doca_error_get_descr(result));
        goto cleanup;
    }
}


    /* ------------------------------------------------------------------
     * 3. UDP_PIPE — non-root, BASIC, checks registered UDP flows
     * ------------------------------------------------------------------ */
	
{

	struct doca_flow_match match_tmpl = {0};

match_tmpl.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
match_tmpl.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_UDP;

match_tmpl.outer.ip4.src_ip = UINT32_MAX;
match_tmpl.outer.ip4.dst_ip = UINT32_MAX;
match_tmpl.outer.udp.l4_port.src_port = UINT16_MAX;
match_tmpl.outer.udp.l4_port.dst_port = UINT16_MAX;

    result = doca_flow_pipe_cfg_create(&cfg, ports[0]);
    if (result != DOCA_SUCCESS) goto cleanup;

    set_flow_pipe_cfg(cfg, "UDP_PIPE", DOCA_FLOW_PIPE_BASIC, false);
    
    // Pass NULL here as well
    doca_flow_pipe_cfg_set_match(cfg, &match_tmpl, NULL);
    doca_flow_pipe_cfg_set_monitor(cfg, &mon_tmpl);

    result = doca_flow_pipe_create(cfg, &fwd_drop, &pipe_miss_to_rss, &udp_pipe);
    doca_flow_pipe_cfg_destroy(cfg);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("UDP_PIPE: %s", doca_error_get_descr(result));
        goto cleanup;
    }
}


    /* ------------------------------------------------------------------
     * 4. ROOT_PIPE — root, CONTROL, sorts initial incoming traffic
     * ------------------------------------------------------------------ */
    {
        result = doca_flow_pipe_cfg_create(&cfg, ports[0]);
        if (result != DOCA_SUCCESS) goto cleanup;

        set_flow_pipe_cfg(cfg, "ROOT_PIPE", DOCA_FLOW_PIPE_CONTROL, true);
        result = doca_flow_pipe_create(cfg, NULL, NULL, &root_pipe);
        doca_flow_pipe_cfg_destroy(cfg);
        if (result != DOCA_SUCCESS) {
            DOCA_LOG_ERR("ROOT_PIPE: %s", doca_error_get_descr(result));
            goto cleanup;
        }

        struct doca_flow_fwd fwd_to_tcp = { .type = DOCA_FLOW_FWD_PIPE, .next_pipe = tcp_pipe };
        struct doca_flow_fwd fwd_to_udp = { .type = DOCA_FLOW_FWD_PIPE, .next_pipe = udp_pipe };

        /* L4 TCP Entry → TCP_PIPE */
        struct doca_flow_match m_tcp = {0}, msk_tcp = {0};
	
	m_tcp.outer.l3_type          = DOCA_FLOW_L3_TYPE_IP4;
        msk_tcp.outer.l3_type        = 0xff; // Tell hardware to validate L3 type
        m_tcp.outer.ip4.next_proto   = IPPROTO_TCP;
        msk_tcp.outer.ip4.next_proto = 0xff;


        result = doca_flow_pipe_control_add_entry(
            0, 1, root_pipe, &m_tcp, &msk_tcp,
            NULL, NULL, NULL, NULL, NULL, &fwd_to_tcp, &status, &entry);
        if (result != DOCA_SUCCESS) {
            DOCA_LOG_ERR("root TCP: %s", doca_error_get_descr(result));
            goto cleanup;
        }

        /* L4 UDP Entry → UDP_PIPE */
        struct doca_flow_match m_udp = {0}, msk_udp = {0};
	
	m_udp.outer.l3_type          = DOCA_FLOW_L3_TYPE_IP4;
        msk_udp.outer.l3_type        = 0xff; // Tell hardware to validate L3 type
        m_udp.outer.ip4.next_proto   = IPPROTO_UDP;
        msk_udp.outer.ip4.next_proto = 0xff;

        result = doca_flow_pipe_control_add_entry(
            0, 2, root_pipe, &m_udp, &msk_udp,
            NULL, NULL, NULL, NULL, NULL, &fwd_to_udp, &status, &entry);
        if (result != DOCA_SUCCESS) {
            DOCA_LOG_ERR("root UDP: %s", doca_error_get_descr(result));
            goto cleanup;
        }

        /* Catch-all → DROP */
        struct doca_flow_match empty = {0};
        result = doca_flow_pipe_control_add_entry(
            0, 3, root_pipe, &empty, &empty,
            NULL, NULL, NULL, NULL, NULL, &fwd_drop, &status, &entry);
        if (result != DOCA_SUCCESS) {
            DOCA_LOG_ERR("root catch-all: %s", doca_error_get_descr(result));
            goto cleanup;
        }
    }

    result = doca_flow_entries_process(ports[0], 0, 10000, 0);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("entries_process: %s", doca_error_get_descr(result));
        goto cleanup;
    }

    DOCA_LOG_INFO("Pipeline active. Waiting for traffic... (Ctrl+C for report)");

    /* Software processing loop for packets sent to CPU */
    {
        struct rte_mbuf *pkts[BURST_SIZE];

        while (!force_quit) {
            uint16_t nb_rx = rte_eth_rx_burst(0, 0, pkts, BURST_SIZE);

            for (int i = 0; i < nb_rx; i++) {
                process_packet(pkts[i], ports[0], tcp_pipe, udp_pipe);
                rte_pktmbuf_free(pkts[i]);
            }

            if (nb_rx == 0)
                usleep(1000);
        }
    }

    print_report();
    result = DOCA_SUCCESS;
	
	dump_pipe_to_report(tcp_pipe, "tcp_rules");
    dump_pipe_to_report(udp_pipe, "udp_rules");	

cleanup:
    stop_doca_flow_ports(1, ports);
    doca_flow_destroy();
    return result;
}
