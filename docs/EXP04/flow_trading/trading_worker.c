#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <arpa/inet.h>

#include <rte_mbuf.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>

#include <doca_log.h>

#include "trading_worker.h"
#include "trading_config.h"

DOCA_LOG_REGISTER(TRADING::WORKER);

#define BURST_SIZE      32
#define ETH_HDR_LEN     14
#define IP_HDR_LEN      20   /* senza opzioni */
#define UDP_HDR_LEN      8
#define TCP_HDR_LEN     20   /* senza opzioni */

static volatile bool force_quit = false;

static void sig_handler(int s)
{
    (void)s;
    force_quit = true;
}

/* ------------------------------------------------------------------ */
/* Decodifica FIX Protocol (semplificata)                              */
/*                                                                     */
/* FIX usa messaggi ASCII del tipo:                                    */
/*   8=FIX.4.2\x0135=D\x0149=SENDER\x0155=AAPL\x01...               */
/* I campi sono separati da SOH (0x01).                                */
/* Tag 35 = MsgType, Tag 55 = Symbol (ticker)                         */
/* ------------------------------------------------------------------ */

/* Tipi di messaggio FIX principali */
static const char *fix_msgtype_name(const char *msgtype)
{
    if (strcmp(msgtype, "D")  == 0) return "NEW_ORDER";
    if (strcmp(msgtype, "F")  == 0) return "CANCEL";
    if (strcmp(msgtype, "G")  == 0) return "MODIFY";
    if (strcmp(msgtype, "8")  == 0) return "EXECUTION_REPORT";
    if (strcmp(msgtype, "V")  == 0) return "MARKET_DATA_REQUEST";
    if (strcmp(msgtype, "W")  == 0) return "MARKET_DATA_SNAPSHOT";
    if (strcmp(msgtype, "X")  == 0) return "MARKET_DATA_INCR";
    return "UNKNOWN";
}

/*
 * Cerca il valore di un tag FIX nel payload.
 * I campi FIX sono separati da SOH (0x01), formato: "tag=value\x01"
 * Ritorna puntatore al valore, oppure NULL se non trovato.
 * Copia al massimo val_size-1 caratteri in val_buf.
 */
static int fix_get_tag(const char *payload, uint32_t len,
                       int tag, char *val_buf, size_t val_size)
{
    char search[16];
    int  search_len;
    const char *p, *end, *eq, *soh;

    /* Costruisce la stringa da cercare: "\x01TAG=" o inizio stringa */
    snprintf(search, sizeof(search), "\x01%d=", tag);
    search_len = strlen(search);

    end = payload + len;

    /* Cerca sia all'inizio (tag=35) che dopo SOH (\x01tag=) */
    p = payload;

    /* Prima cerca senza SOH iniziale (primo campo) */
    {
        char first[16];
        snprintf(first, sizeof(first), "%d=", tag);
        if (strncmp(p, first, strlen(first)) == 0) {
            eq  = p + strlen(first) - 1;
            soh = memchr(eq + 1, 0x01, end - eq - 1);
            size_t vlen = soh ? (size_t)(soh - eq - 1) : (size_t)(end - eq - 1);
            if (vlen >= val_size) vlen = val_size - 1;
            memcpy(val_buf, eq + 1, vlen);
            val_buf[vlen] = '\0';
            return 1;
        }
    }

    /* Poi cerca con SOH iniziale */
    while (p < end) {
        p = memchr(p, 0x01, end - p);
        if (!p) break;

        if ((size_t)(end - p) >= (size_t)search_len &&
            memcmp(p, search, search_len) == 0) {
            eq  = p + search_len - 1;
            soh = memchr(eq + 1, 0x01, end - eq - 1);
            size_t vlen = soh ? (size_t)(soh - eq - 1) : (size_t)(end - eq - 1);
            if (vlen >= val_size) vlen = val_size - 1;
            memcpy(val_buf, eq + 1, vlen);
            val_buf[vlen] = '\0';
            return 1;
        }
        p++;
    }
    return 0;
}

/* Controlla se il payload inizia con "8=FIX" */
static int is_fix_message(const uint8_t *payload, uint32_t len)
{
    return (len >= 7 && memcmp(payload, "8=FIX", 5) == 0);
}

/* Decodifica e stampa il messaggio FIX */
static void decode_fix(const uint8_t *payload, uint32_t len,
                       const char *flow_label)
{
    char msgtype[8]  = "?";
    char ticker[16]  = "?";
    char price[16]   = "?";
    char qty[16]     = "?";
    char side[4]     = "?";
    const char *p    = (const char *)payload;

    fix_get_tag(p, len, 35, msgtype, sizeof(msgtype));
    fix_get_tag(p, len, 55, ticker,  sizeof(ticker));
    fix_get_tag(p, len, 44, price,   sizeof(price));
    fix_get_tag(p, len, 38, qty,     sizeof(qty));
    fix_get_tag(p, len, 54, side,    sizeof(side));

    const char *side_name = "?";
    if (strcmp(side, "1") == 0)      side_name = "BUY";
    else if (strcmp(side, "2") == 0) side_name = "SELL";

    DOCA_LOG_INFO("[DPI][%s] FIX %s | Ticker=%-6s | Side=%-4s | Qty=%-8s | Price=%s",
                  flow_label,
                  fix_msgtype_name(msgtype),
                  ticker, side_name, qty, price);
}

/* ------------------------------------------------------------------ */
/* Identificazione del flusso                                          */
/* ------------------------------------------------------------------ */

static const char *identify_flow(const flow_config_t *cfg,
                                 uint32_t src_ip, uint32_t dst_ip,
                                 uint16_t src_port, uint16_t dst_port,
                                 uint8_t proto,
                                 char *label_buf, size_t label_size)
{
    struct in_addr sa, da;
    char src_s[INET_ADDRSTRLEN], dst_s[INET_ADDRSTRLEN];

    for (int i = 0; i < cfg->count; i++) {
        const flow_key_t *k = &cfg->flows[i];
        if (k->src_ip   == src_ip   &&
            k->dst_ip   == dst_ip   &&
            k->src_port == src_port &&
            k->dst_port == dst_port &&
            k->proto    == proto) {
            snprintf(label_buf, label_size, "flow#%d", i);
            return label_buf;
        }
    }

    /* Flusso non in configurazione — non dovrebbe arrivare qui
     * perché la Control Pipe droppa il traffico non noto,
     * ma lo logghiamo per debug */
    sa.s_addr = src_ip; inet_ntop(AF_INET, &sa, src_s, sizeof(src_s));
    da.s_addr = dst_ip; inet_ntop(AF_INET, &da, dst_s, sizeof(dst_s));
    snprintf(label_buf, label_size, "UNKNOWN %s:%u->%s:%u",
             src_s, ntohs(src_port), dst_s, ntohs(dst_port));
    return label_buf;
}

/* ------------------------------------------------------------------ */
/* Analisi singolo pacchetto                                           */
/* ------------------------------------------------------------------ */

static void analyze_packet(struct rte_mbuf *m, const flow_config_t *cfg)
{
    uint8_t  *data     = rte_pktmbuf_mtod(m, uint8_t *);
    uint32_t  pkt_len  = rte_pktmbuf_pkt_len(m);

    /* Serve almeno Ethernet + IP */
    if (pkt_len < ETH_HDR_LEN + IP_HDR_LEN)
        return;

    struct rte_ether_hdr *eth = (struct rte_ether_hdr *)data;

    /* Considera solo IPv4 */
    if (rte_be_to_cpu_16(eth->ether_type) != RTE_ETHER_TYPE_IPV4)
        return;

    struct rte_ipv4_hdr *ip =
        (struct rte_ipv4_hdr *)(data + ETH_HDR_LEN);

    uint8_t   *payload     = NULL;
    uint32_t   payload_len = 0;
    uint16_t   src_port    = 0, dst_port = 0;

    if (ip->next_proto_id == IPPROTO_UDP) {
        if (pkt_len < ETH_HDR_LEN + IP_HDR_LEN + UDP_HDR_LEN)
            return;
        struct rte_udp_hdr *udp =
            (struct rte_udp_hdr *)(data + ETH_HDR_LEN + IP_HDR_LEN);
        src_port    = udp->src_port;
        dst_port    = udp->dst_port;
        payload     = (uint8_t *)(udp + 1);
        payload_len = pkt_len - ETH_HDR_LEN - IP_HDR_LEN - UDP_HDR_LEN;

    } else if (ip->next_proto_id == IPPROTO_TCP) {
        if (pkt_len < ETH_HDR_LEN + IP_HDR_LEN + TCP_HDR_LEN)
            return;
        struct rte_tcp_hdr *tcp =
            (struct rte_tcp_hdr *)(data + ETH_HDR_LEN + IP_HDR_LEN);
        src_port    = tcp->src_port;
        dst_port    = tcp->dst_port;
        uint8_t tcp_hdr_len = ((tcp->data_off >> 4) & 0xf) * 4;
        payload     = (uint8_t *)tcp + tcp_hdr_len;
        payload_len = pkt_len - ETH_HDR_LEN - IP_HDR_LEN - tcp_hdr_len;
    } else {
        return;
    }

    /* Identifica il flusso */
    char label[64];
    identify_flow(cfg,
                  ip->src_addr, ip->dst_addr,
                  src_port, dst_port,
                  ip->next_proto_id,
                  label, sizeof(label));

    /* DPI sul payload */
    if (payload && payload_len > 0) {
        if (is_fix_message(payload, payload_len)) {
            decode_fix(payload, payload_len, label);
        } else {
            /* Payload non FIX: stampa hex dei primi 8 byte */
            DOCA_LOG_INFO("[DPI][%s] proto=%s len=%u | payload(hex): "
                          "%02x %02x %02x %02x %02x %02x %02x %02x",
                          label,
                          (ip->next_proto_id == IPPROTO_TCP) ? "TCP" : "UDP",
                          payload_len,
                          payload_len > 0 ? payload[0] : 0,
                          payload_len > 1 ? payload[1] : 0,
                          payload_len > 2 ? payload[2] : 0,
                          payload_len > 3 ? payload[3] : 0,
                          payload_len > 4 ? payload[4] : 0,
                          payload_len > 5 ? payload[5] : 0,
                          payload_len > 6 ? payload[6] : 0,
                          payload_len > 7 ? payload[7] : 0);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Worker loop                                                          */
/* ------------------------------------------------------------------ */

void run_trading_worker(uint16_t port_id, uint16_t queue_id,
                        const flow_config_t *cfg)
{
    struct rte_mbuf *pkts[BURST_SIZE];

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    DOCA_LOG_INFO("Trading DPI Worker avviato — porta %u coda %u (Ctrl+C per uscire)",
                  port_id, queue_id);

    while (!force_quit) {
        uint16_t nb_rx = rte_eth_rx_burst(port_id, queue_id,
                                          pkts, BURST_SIZE);
        for (int i = 0; i < nb_rx; i++) {
            analyze_packet(pkts[i], cfg);
            rte_pktmbuf_free(pkts[i]);
        }

        /* Piccola pausa per non saturare la CPU durante i test.
         * In produzione HFT: rimuovere usleep e girare a piena velocità. */
        if (nb_rx == 0)
            usleep(10);
    }

    DOCA_LOG_INFO("Worker terminato.");
}
