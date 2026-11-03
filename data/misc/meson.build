appdir = join_paths(get_option('datadir'), 'applications')
metainfodir = join_paths(get_option('datadir'), 'metainfo')
mimedir = join_paths(get_option('datadir'), 'mime', 'packages')
desktop_utils = find_program('desktop-file-validate', required: false)
appstreamcli = find_program('appstreamcli', required: false)

if get_option('qt-frontend') and get_option('install-appdata')
  install_data('net.zoite.Zoitechat.Qt.metainfo.xml', install_dir: metainfodir)
  if appstreamcli.found()
    test('Validate hexchat-ng appdata', appstreamcli,
      args: ['validate', '--no-net', files('net.zoite.Zoitechat.Qt.metainfo.xml')])
  endif
endif

if get_option('plugin')
  plugin_metainfo = []

  # FIXME: These should all get translated somewhere
  if get_option('with-checksum')
    plugin_metainfo += [
      ['Checksum', 'Calculates a checksum for all sent and recieved DCC files', 'MIT']
    ]
  endif

  if get_option('with-fishlim')
    plugin_metainfo += [
      ['Fishlim', 'Allows setting a key for encrypted conversations', 'MIT AND GPL-2.0+']
    ]
  endif

  if get_option('with-lua') != 'false'
    plugin_metainfo += [
      ['Lua', 'Provides a scripting interface in Lua', 'MIT']
    ]
  endif

  if get_option('with-perl') != 'false'
    plugin_metainfo += [
      ['Perl', 'Provides a scripting interface in Perl', 'GPL-2.0+']
    ]
  endif

  if get_option('with-python') != 'false'
    plugin_metainfo += [
      ['Python', 'Provides a scripting interface in Python', 'GPL-2.0+']
    ]
  endif

  if get_option('with-sysinfo')
    plugin_metainfo += [
      ['Sysinfo', 'Adds command to display system information', 'GPL-2.0+']
    ]
  endif

  foreach metainfo : plugin_metainfo
    name = metainfo[0]
    conf = configuration_data()
    conf.set('NAME', name)
    conf.set('SUMMARY', metainfo[1])
    conf.set('LICENSE', metainfo[2])

    plugin_appdata = configure_file(
      input: 'net.zoite.Zoitechat.Plugin.metainfo.xml.in',
      output: 'net.zoite.Zoitechat.Plugin.@0@.metainfo.xml'.format(name),
      configuration: conf,
      install_dir: get_option('install-plugin-metainfo') ? metainfodir : '',
    )

    if appstreamcli.found()
      test('Validate net.zoite.Zoitechat.Plugin.@0@.metainfo.xml'.format(name), appstreamcli,
        args: ['validate', plugin_appdata]
    )
    endif

  endforeach
endif

if get_option('qt-frontend')
  install_data('net.zoite.Zoitechat.Qt.desktop', install_dir: appdir)
  if desktop_utils.found()
    test('Validate Qt desktop file', desktop_utils,
      args: [files('net.zoite.Zoitechat.Qt.desktop')])
  endif
endif
