# A separate executable, leaving the GTK 2 frontend and C core unchanged.
qt6_widgets = dependency('qt6', modules: ['Widgets'], version: '>= 6.2')
executable('hexchat-qt',
  sources: ['fe-qt.c', 'qtfrontend.cpp'],
  dependencies: [hexchat_common_dep, qt6_widgets],
  include_directories: config_h_include,
  c_args: ['-DHAVE_CONFIG_H'],
  install: true,
  gui_app: true,
)
