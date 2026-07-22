#include <stdlib.h>
#include <stdio.h>

#include <doca_argp.h>
#include <doca_flow.h>
#include <doca_log.h>
#include <dpdk_utils.h>

#include "trading_config.h"

DOCA_LOG_REGISTER(TRADING::MAIN);

/* Dichiarata in trading_sample.c */
doca_error_t run_trading_pipeline(int nb_queues, const flow_config_t *cfg);

int main(int argc, char **argv)
{
    doca_error_t result;
    struct doca_log_backend *sdk_log;
    int exit_status = EXIT_FAILURE;

    /* Configurazione DPDK:
     * - 1 porta  (p1 = 03:00.1)
     * - 1 coda   (la coda 0 riceve i pacchetti via RSS per il DPI)
     */
    struct application_dpdk_config dpdk_config = {
        .port_config.nb_ports    = 1,
        .port_config.nb_queues   = 1,
        .port_config.nb_hairpin_q = 0,
        .port_config.isolated_mode = 0,
        .port_config.switch_mode   = 0,
    };

    /* --- Logger --- */
    result = doca_log_backend_create_standard();
    if (result != DOCA_SUCCESS) return EXIT_FAILURE;

    result = doca_log_backend_create_with_file_sdk(stderr, &sdk_log);
    if (result != DOCA_SUCCESS) return EXIT_FAILURE;

    doca_log_backend_set_sdk_level(sdk_log, DOCA_LOG_LEVEL_WARNING);

    /* --- Argomenti command line ---
     * Uso: doca_trading -a 0000:03:00.1,dv_flow_en=2 -- --config flows.conf
     *
     * Il "--" separa i flag EAL DPDK dai flag dell'applicazione.
     * Qui gestiamo manualmente --config dopo il "--".
     */
    const char *config_path = "flows.conf"; /* default */

    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--config") == 0) {
            config_path = argv[i + 1];
            /* Rimuovi i due argomenti shiftando il resto */
            for (int j = i; j < argc - 2; j++)
                argv[j] = argv[j + 2];
            argc -= 2;
            break;
        }
    }

    /* --- Carica la configurazione dei flussi --- */
    flow_config_t cfg;
    if (load_flow_config(config_path, &cfg) != 0) {
        fprintf(stderr, "[MAIN] Errore caricamento config: %s\n", config_path);
        fprintf(stderr, "Uso: --config <path/to/flows.conf>\n");
        return EXIT_FAILURE;
    }
    print_flow_config(&cfg);

    /* --- Init DOCA argp + DPDK --- */
    result = doca_argp_init("doca_trading", NULL);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("doca_argp_init failed: %s", doca_error_get_descr(result));
        return EXIT_FAILURE;
    }

    doca_argp_set_dpdk_program(dpdk_init);

    result = doca_argp_start(argc, argv);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("doca_argp_start failed: %s", doca_error_get_descr(result));
        goto argp_cleanup;
    }

    result = dpdk_queues_and_ports_init(&dpdk_config);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("dpdk_queues_and_ports_init failed");
        goto dpdk_cleanup;
    }

    DOCA_LOG_INFO("Trading DPU Engine avviato — %d flussi configurati",
                  cfg.count);

    /* --- Avvia la pipeline DOCA Flow + worker DPI --- */
    result = run_trading_pipeline(dpdk_config.port_config.nb_queues, &cfg);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("run_trading_pipeline failed: %s",
                     doca_error_get_descr(result));
        goto dpdk_ports_cleanup;
    }

    exit_status = EXIT_SUCCESS;

dpdk_ports_cleanup:
    dpdk_queues_and_ports_fini(&dpdk_config);
dpdk_cleanup:
    dpdk_fini();
argp_cleanup:
    doca_argp_destroy();

    DOCA_LOG_INFO("Trading Engine terminato.");
    return exit_status;
}
