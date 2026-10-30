# A separate executable, leaving the GTK 2 frontend and C core unchanged.
qt6_widgets = dependency('qt6', modules: ['Widgets'], version: '>= 6.2')
executable('hexchat-qt',
  sources: ['fe-qt.c', 'qtfrontend.cpp', 'ircformat.cpp', 'chatinput.cpp', 'nicklist.cpp',
            'network-bridge.c', 'networkdialog.cpp'],
  dependencies: [hexchat_common_dep, qt6_widgets],
  include_directories: config_h_include,
  c_args: ['-DHAVE_CONFIG_H'],
  # This frontend adapter uses C11; the original engine stays GNU89.
  override_options: ['c_std=gnu11'],
  install: true,
  gui_app: true,
)

# Parser tests do not need the HexChat engine or an IRC connection.
qt_ircformat_test = executable('qt-ircformat-test',
  sources: ['ircformat-test.cpp', 'ircformat.cpp'],
  dependencies: [qt6_widgets],
)
test('qt-ircformat', qt_ircformat_test, env: ['QT_QPA_PLATFORM=offscreen'])

# Validate that Qt's GLib dispatcher processes the default GMainContext.
qt_glib_dispatcher_test = executable('qt-glib-dispatcher-test',
  sources: ['glib-dispatcher-test.cpp'],
  dependencies: [qt6_widgets, dependency('glib-2.0')],
)
test('qt-glib-dispatcher', qt_glib_dispatcher_test, env: ['QT_QPA_PLATFORM=offscreen'])

qt_chatinput_test = executable('qt-chatinput-test',
  sources: ['chatinput-test.cpp', 'chatinput.cpp'],
  dependencies: [qt6_widgets],
)
test('qt-chatinput', qt_chatinput_test, env: ['QT_QPA_PLATFORM=offscreen'])

qt_nicklist_test = executable('qt-nicklist-test',
  sources: ['nicklist-test.cpp', 'nicklist.cpp'],
  dependencies: [qt6_widgets],
)
test('qt-nicklist', qt_nicklist_test, env: ['QT_QPA_PLATFORM=offscreen'])
