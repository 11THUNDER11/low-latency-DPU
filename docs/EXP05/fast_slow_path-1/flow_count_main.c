#include <stdlib.h>
#include <signal.h>
#include <stdbool.h>

#include <doca_argp.h>
#include <doca_flow.h>
#include <doca_log.h>
#include <dpdk_utils.h>

DOCA_LOG_REGISTER(FLOW_COUNT::MAIN);

bool force_quit = false;

static void signal_handler(int signum)
{
    if (signum == SIGINT || signum == SIGTERM) {
        DOCA_LOG_INFO("Segnale ricevuto — chiusura in corso...");
        force_quit = true;
    }
}

doca_error_t flow_count(int nb_queues);

int main(int argc, char **argv)
{
    doca_error_t result;
    struct doca_log_backend *sdk_log;
    int exit_status = EXIT_FAILURE;

    struct application_dpdk_config dpdk_config = {
        .port_config.nb_ports    = 1,
        .port_config.nb_queues   = 1,
        .port_config.nb_hairpin_q = 0,
        .port_config.isolated_mode = 0,
        .port_config.switch_mode   = 0,
    };

    result = doca_log_backend_create_standard();
    if (result != DOCA_SUCCESS) goto sample_exit;

    result = doca_log_backend_create_with_file_sdk(stderr, &sdk_log);
    if (result != DOCA_SUCCESS) goto sample_exit;

    doca_log_backend_set_sdk_level(sdk_log, DOCA_LOG_LEVEL_WARNING);

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    DOCA_LOG_INFO("Flow Count Quintuple — modalità dinamica");

    result = doca_argp_init("doca_flow_count", NULL);
    if (result != DOCA_SUCCESS) goto sample_exit;

    doca_argp_set_dpdk_program(dpdk_init);

    result = doca_argp_start(argc, argv);
    if (result != DOCA_SUCCESS) goto argp_cleanup;

    result = dpdk_queues_and_ports_init(&dpdk_config);
    if (result != DOCA_SUCCESS) goto dpdk_cleanup;

    result = flow_count(dpdk_config.port_config.nb_queues);
    if (result != DOCA_SUCCESS)
        DOCA_LOG_ERR("flow_count() failed: %s", doca_error_get_descr(result));
    else
        exit_status = EXIT_SUCCESS;

    dpdk_queues_and_ports_fini(&dpdk_config);
dpdk_cleanup:
    dpdk_fini();
argp_cleanup:
    doca_argp_destroy();
sample_exit:
    return exit_status;
}
