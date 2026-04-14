project('DOCA_SAMPLE', 'C', 'CPP',
        version: run_command(find_program('cat'),
                files('/opt/mellanox/doca/applications/VERSION'), check: true).stdout().strip(),
        license: 'Proprietary',
        default_options: ['buildtype=debug'],
        meson_version: '>= 0.61.2'
)

# Nome dell'eseguibile finale
SAMPLE_NAME = 'flow_dpi'

add_project_arguments('-D DOCA_ALLOW_EXPERIMENTAL_API', language: ['c', 'cpp'])

# Dipendenze di sistema
sample_dependencies = []
sample_dependencies += dependency('doca-common')
sample_dependencies += dependency('doca-flow')
sample_dependencies += dependency('doca-dpdk-bridge')
sample_dependencies += dependency('doca-argp')
sample_dependencies += dependency('libdpdk')

# Lista dei sorgenti con percorsi assoluti per i file comuni di sistema
sample_srcs = [
        'flow_dpi_main.c',
        'flow_dpi_sample.c',
        'flow_dpi_worker.c',
        '/opt/mellanox/doca/samples/doca_flow/flow_common.c',
        '/opt/mellanox/doca/samples/common.c',
        '/opt/mellanox/doca/applications/common/dpdk_utils.c',
]

# Directory di inclusione assolute
sample_inc_dirs = []
sample_inc_dirs += include_directories('.')
sample_inc_dirs += include_directories('/opt/mellanox/doca/samples/doca_flow/')
sample_inc_dirs += include_directories('/opt/mellanox/doca/samples/')
sample_inc_dirs += include_directories('/opt/mellanox/doca/applications/common/')

# Generazione dell'eseguibile
executable('doca_' + SAMPLE_NAME, sample_srcs,
        c_args : '-Wno-missing-braces',
        dependencies : sample_dependencies,
        include_directories: sample_inc_dirs,
        install: false)
