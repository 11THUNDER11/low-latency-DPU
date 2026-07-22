#ifndef TRADING_CONFIG_H
#define TRADING_CONFIG_H

#include <stdint.h>

#define MAX_FLOWS 64

/* Quintupla che identifica un flusso di trading */
typedef struct {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t  proto;   /* IPPROTO_TCP=6 o IPPROTO_UDP=17 */
} flow_key_t;

/* Configurazione caricata dal file flows.conf */
typedef struct {
    flow_key_t flows[MAX_FLOWS];
    int        count;
} flow_config_t;

/*
 * Carica i flussi dal file di configurazione.
 * Formato: src_ip dst_ip src_port dst_port proto(tcp|udp)
 * Ritorna 0 in caso di successo, -1 in caso di errore.
 */
int load_flow_config(const char *path, flow_config_t *cfg);

/* Stampa la configurazione caricata (per debug) */
void print_flow_config(const flow_config_t *cfg);

#endif /* TRADING_CONFIG_H */
