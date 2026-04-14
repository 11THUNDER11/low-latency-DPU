#include <stdlib.h>
#include <doca_argp.h>
#include <doca_flow.h>
#include <doca_log.h>
#include <dpdk_utils.h>

DOCA_LOG_REGISTER(FLOW_DPI::MAIN);

/* Dichiarazione della funzione definita in flow_dpi_sample.c */
doca_error_t flow_dpi(int nb_queues);

int main(int argc, char **argv)
{
    doca_error_t result;
    struct doca_log_backend *sdk_log;
    int exit_status = EXIT_FAILURE;

    /* Configurazione DPDK: 1 porta, 1 coda CPU per ricevere i pacchetti */
    struct application_dpdk_config dpdk_config = {
        .port_config.nb_ports = 1,
        .port_config.nb_queues = 1, 
        .port_config.nb_hairpin_q = 0,
        .port_config.isolated_mode = 0,
        .port_config.switch_mode = 0,
    };

    result = doca_log_backend_create_standard();
    if (result != DOCA_SUCCESS) return EXIT_FAILURE;

    result = doca_log_backend_create_with_file_sdk(stderr, &sdk_log);
    if (result != DOCA_SUCCESS) return EXIT_FAILURE;

    doca_log_backend_set_sdk_level(sdk_log, DOCA_LOG_LEVEL_WARNING);

    DOCA_LOG_INFO("Starting flow_dpi (Clean Architecture mode)");

    result = doca_argp_init("doca_flow_dpi", NULL);
    if (result != DOCA_SUCCESS) return EXIT_FAILURE;

    doca_argp_set_dpdk_program(dpdk_init);

    result = doca_argp_start(argc, argv);
    if (result != DOCA_SUCCESS) goto argp_cleanup;

    result = dpdk_queues_and_ports_init(&dpdk_config);
    if (result != DOCA_SUCCESS) goto dpdk_cleanup;

    /* Esecuzione della logica DPI */
    result = flow_dpi(dpdk_config.port_config.nb_queues);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("flow_dpi() failed: %s", doca_error_get_descr(result));
        goto dpdk_ports_cleanup;
    }

    exit_status = EXIT_SUCCESS;

dpdk_ports_cleanup:
    dpdk_queues_and_ports_fini(&dpdk_config);
dpdk_cleanup:
    dpdk_fini();
argp_cleanup:
    doca_argp_destroy();

    return exit_status;
}
