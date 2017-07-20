project('VFRToCFR',
        'cpp',
        default_options : 'buildtype=release',
        license : 'BSD-3-Clause',
        version : '1')


# Dependencies
vapoursynth = dependency('vapoursynth')


# Sources
src = [
    'VFRToCFR.cpp']


# Libs
library(
    'vfrtocfr',
    src,
    dependencies : [vapoursynth],
    install_dir : join_paths(get_option('prefix'), get_option('libdir'), 'vapoursynth'),
    install : true
)
