#include <unistd.h>
#include <stdbool.h>
#include <signal.h>
#include <rte_mbuf.h>
#include <rte_ethdev.h>
#include <doca_log.h>
#include "flow_dpi_worker.h"

DOCA_LOG_REGISTER(FLOW_DPI::WORKER);

#define PACKET_BURST 32

/* Variabile globale atomica per gestire l'uscita pulita */
static volatile bool force_quit = false;

/* Gestore dei segnali (Ctrl+C) */
static void signal_handler(int signum)
{
    if (signum == SIGINT || signum == SIGTERM) {
        DOCA_LOG_INFO("Signal %d received, preparing to exit worker...", signum);
        force_quit = true;
    }
}

void analyze_packet(struct rte_mbuf *m)
{
    uint8_t *data = rte_pktmbuf_mtod(m, uint8_t *);
    uint32_t len = rte_pktmbuf_pkt_len(m);

    // DPI: Stampa info base
    DOCA_LOG_INFO("Worker - Packet Received! Size: %u bytes", len);

    // Se il pacchetto ha un payload (oltre Ethernet(14)+IP(20)+UDP(8) = 42 bytes)
    if (len > 42) {
        uint8_t *payload = data + 42;
        uint32_t payload_len = len - 42;

        // Visualizzazione Esadecimale dei primi 4 byte
        DOCA_LOG_INFO("Payload Start (Hex): %02x %02x %02x %02x",
                      payload[0], payload[1], payload[2], payload[3]);

        // Visualizzazione ASCII (utile per i messaggi dello script Python)
        char ascii_hint[5] = {0};
        for (int i = 0; i < 4 && i < payload_len; i++) {
            ascii_hint[i] = (payload[i] >= 32 && payload[i] <= 126) ? payload[i] : '.';
        }
        DOCA_LOG_INFO("Payload Start (ASCII): %s", ascii_hint);
    }
}

void run_dpi_worker(uint16_t port_id, uint16_t queue_id)
{
    struct rte_mbuf *mbufs[PACKET_BURST];

    /* Registra il gestore per SIGINT (Ctrl+C) */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    DOCA_LOG_INFO("Worker started on Port %u, Queue %u. Polling (Press Ctrl+C to stop)...", port_id, queue_id);

    while (!force_quit) {
        /* Ricezione burst di pacchetti dalla coda DPDK */
        uint16_t nb_rx = rte_eth_rx_burst(port_id, queue_id, mbufs, PACKET_BURST);
        
        if (nb_rx > 0) {
            for (int j = 0; j < nb_rx; j++) {
                analyze_packet(mbufs[j]);
                rte_pktmbuf_free(mbufs[j]); // Libera la memoria del pacchetto
            }
        }
        
        /* * usleep(1) riduce il consumo di CPU ARM durante i test. 
         * In produzione si rimuove per latenza sub-microsecondo.
         */
        usleep(1);
    }

    DOCA_LOG_INFO("Worker loop stopped gracefully.");
}
