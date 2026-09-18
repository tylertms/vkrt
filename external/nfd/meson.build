file_dialogs_opt = get_option('file_dialogs')
file_dialogs_enabled = false
file_dialogs_backend = 'disabled'
app_dialog_source = files('../../src/app/editor/dialogs_stub.c')
app_link_with = [vkrt, dcimgui, cjson]

if not file_dialogs_opt.disabled()
  nfd_source = []
  nfd_deps = []
  nfd_cpp_args = cpp_args

  if is_windows
    nfd_source = files('src/nfd_win.cpp')
    nfd_deps += [cc.find_library('ole32'), cc.find_library('shell32'), cc.find_library('uuid')]
    file_dialogs_enabled = true
    file_dialogs_backend = 'win32'
  elif is_linux
    nfd_backend = get_option('nfd_backend')

    dbus_dep = dependency('dbus-1', required: false, method: 'pkg-config')
    gtk3_dep = dependency('gtk+-3.0', required: false, method: 'pkg-config')

    if nfd_backend == 'portal'
      if not dbus_dep.found()
        if file_dialogs_opt.enabled()
          error('file_dialogs enabled with nfd_backend=portal requires dbus-1 development files')
        endif
      else
        nfd_source = files('src/nfd_portal.cpp')
        nfd_deps += dbus_dep
        nfd_cpp_args += ['-DNFD_PORTAL']
        file_dialogs_enabled = true
        file_dialogs_backend = 'portal'
      endif
    elif nfd_backend == 'gtk'
      if not gtk3_dep.found()
        if file_dialogs_opt.enabled()
          error('file_dialogs enabled with nfd_backend=gtk requires gtk+-3.0 development files')
        endif
      else
        nfd_source = files('src/nfd_gtk.cpp')
        nfd_deps += gtk3_dep
        file_dialogs_enabled = true
        file_dialogs_backend = 'gtk'
      endif
    elif nfd_backend == 'auto'
      if dbus_dep.found()
        nfd_source = files('src/nfd_portal.cpp')
        nfd_deps += dbus_dep
        nfd_cpp_args += ['-DNFD_PORTAL']
        file_dialogs_enabled = true
        file_dialogs_backend = 'portal'
        message('NFD backend: portal (dbus-1)')
      elif gtk3_dep.found()
        nfd_source = files('src/nfd_gtk.cpp')
        nfd_deps += gtk3_dep
        file_dialogs_enabled = true
        file_dialogs_backend = 'gtk'
        message('NFD backend: gtk (gtk+-3.0)')
      elif file_dialogs_opt.enabled()
        error('file_dialogs enabled but no Linux NFD backend was found. Install dbus-1 or gtk+-3.0 development files.')
      endif
    else
      error('Invalid nfd_backend option: ' + nfd_backend)
    endif
  elif file_dialogs_opt.enabled()
    error('file_dialogs is currently only supported by this Meson build on Windows and Linux.')
  endif

  if file_dialogs_enabled
    if is_linux
      if glfw_build_x11
        nfd_cpp_args += ['-DNFD_X11']
      endif
      if glfw_build_wayland
        nfd_cpp_args += ['-DNFD_WAYLAND']
        nfd_deps += dependency('wayland-client')
        wayland_protocols = dependency('wayland-protocols')
        xdg_foreign_protocol = wayland_protocols.get_variable(pkgconfig: 'pkgdatadir') / 'unstable/xdg-foreign/xdg-foreign-unstable-v1.xml'
        nfd_source += custom_target('nfd_wayland_header',
          input: xdg_foreign_protocol,
          output: 'xdg-foreign-unstable-v1.h',
          command: [linux_wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
        )
        nfd_source += custom_target('nfd_wayland_code',
          input: xdg_foreign_protocol,
          output: 'xdg-foreign-unstable-v1.c',
          command: [linux_wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
        )
      endif
    endif
    nfd = static_library('nfd',
      include_directories: include_directories('src/include', '.'),
      sources: nfd_source,
      dependencies: nfd_deps,
      cpp_args: nfd_cpp_args,
      override_options: ['warning_level=0'],
    )
    app_dialog_source = files('../../src/app/editor/dialogs.c')
    app_link_with += [nfd]
  else
    message('Native file dialogs disabled for this build')
  endif
else
  message('Native file dialogs disabled by option')
endif

