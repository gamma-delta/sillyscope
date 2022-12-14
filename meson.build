project('sillyscope', 'cpp',
  version: '0.2.0',
  default_options: [
    'c_std=c2x',
    'cpp_std=c++20',
    'warning_level=3',
    'opt_level=3',
    'debug_symbols=true',
    'Werror=true',
  ])

qt_dep = dependency('qt5', required: true,
  modules: ['Core', 'Widgets', 'Gui'])
glib_dep = dependency('glib-2.0', version: '>= 2.32', required: true)

audacious_dep = dependency('audacious', version: '>= 4.1', required: true)
audtag_dep = declare_dependency(link_args: [
  '-L@0@'.format(audacious_dep.get_variable(pkgconfig: 'lib_dir')),
  '-laudtag'
])
audqt_dep = declare_dependency(link_args: [
  '-L@0@'.format(audacious_dep.get_variable(pkgconfig: 'lib_dir')),
  '-laudqt'
])
audgui_dep = declare_dependency(link_args: [
  '-L@0@'.format(audacious_dep.get_variable(pkgconfig: 'lib_dir')),
  '-laudgui'
])

eigen_dep = dependency('eigen3', version: '>= 3.4.0')

conf = configuration_data()
conf.set_quoted('PACKAGE', meson.project_name())
conf.set('EXPORT', '__attribute__((visibility("default")))')
configure_file(configuration: conf, output: 'sillyscope_config.h')

library('sillyscope', 
  sources : ['sillyscope.cpp'],
  dependencies: [audacious_dep, qt_dep, glib_dep, audqt_dep, eigen_dep]
)
