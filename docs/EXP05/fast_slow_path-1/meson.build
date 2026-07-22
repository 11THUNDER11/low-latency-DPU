project('DOCA_SAMPLE', 'C', 'CPP',
        version: run_command(find_program('cat'),
                files('/opt/mellanox/doca/applications/VERSION'), check: true).stdout().strip(),
        license: 'Proprietary',
        default_options: ['buildtype=debug'],
        meson_version: '>= 0.61.2'
)

SAMPLE_NAME = 'flow_count'

add_project_arguments('-D DOCA_ALLOW_EXPERIMENTAL_API', language: ['c', 'cpp'])

sample_dependencies = []
sample_dependencies += dependency('doca-common')
sample_dependencies += dependency('doca-flow')
sample_dependencies += dependency('doca-dpdk-bridge')
sample_dependencies += dependency('doca-argp')
sample_dependencies += dependency('libdpdk')

DOCA_FLOW_DIR = '/opt/mellanox/doca/samples/doca_flow/'
DOCA_APPS_DIR = '/opt/mellanox/doca/applications/common/'
DOCA_SAMP_DIR = '/opt/mellanox/doca/samples/'

sample_srcs = [
        SAMPLE_NAME + '_main.c',
        SAMPLE_NAME + '_sample.c',
        DOCA_FLOW_DIR + 'flow_common.c',
        DOCA_SAMP_DIR + 'common.c',
        DOCA_APPS_DIR + 'dpdk_utils.c',
]

sample_inc_dirs = []
sample_inc_dirs += include_directories('.')
sample_inc_dirs += include_directories(DOCA_FLOW_DIR)
sample_inc_dirs += include_directories(DOCA_SAMP_DIR)
sample_inc_dirs += include_directories(DOCA_APPS_DIR)

executable('doca_' + SAMPLE_NAME, sample_srcs,
        c_args : '-Wno-missing-braces',
        dependencies : sample_dependencies,
        include_directories: sample_inc_dirs,
        install: false)
