#ifndef TRADING_WORKER_H
#define TRADING_WORKER_H

#include <stdint.h>
#include "trading_config.h"

/*
 * Avvia il worker DPI sulla CPU ARM.
 * Legge pacchetti dalla coda DPDK 0, decodifica il protocollo FIX/ITCH
 * e stampa ticker e tipo ordine.
 *
 * @port_id:   porta DPDK (0)
 * @queue_id:  coda DPDK (0)
 * @cfg:       configurazione dei flussi (per lookup)
 */
void run_trading_worker(uint16_t port_id, uint16_t queue_id,
                        const flow_config_t *cfg);

#endif /* TRADING_WORKER_H */
