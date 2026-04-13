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

sample_srcs = [
        SAMPLE_NAME + '_sample.c',
        SAMPLE_NAME + '_main.c',
        '../flow_common.c',
        '../../common.c',
        '../../../applications/common/dpdk_utils.c',
]

sample_inc_dirs = []
sample_inc_dirs += include_directories('../..')
sample_inc_dirs += include_directories('..')
sample_inc_dirs += include_directories('../../../applications/common/')

executable('doca_' + SAMPLE_NAME, sample_srcs,
        c_args : '-Wno-missing-braces',
        dependencies : sample_dependencies,
        include_directories: sample_inc_dirs,
        install: false)