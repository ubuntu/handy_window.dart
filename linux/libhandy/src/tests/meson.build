if get_option('tests')

test_env = [
  'G_TEST_SRCDIR=@0@'.format(meson.current_source_dir()),
  'G_TEST_BUILDDIR=@0@'.format(meson.current_build_dir()),
  'G_DEBUG=gc-friendly,fatal-warnings',
  'GSETTINGS_BACKEND=memory',
  'PYTHONDONTWRITEBYTECODE=yes',
  'MALLOC_CHECK_=2',
]

test_cflags = [
  '-DHDY_LOG_DOMAIN="Handy"',
  '-DTEST_DATA_DIR="@0@/data"'.format(meson.current_source_dir()),
]

test_link_args = [
  '-fPIC',
]

test_names = [
  'test-action-row',
  'test-application-window',
  'test-avatar',
  'test-carousel',
  'test-carousel-indicator-dots',
  'test-carousel-indicator-lines',
  'test-combo-row',
  'test-deck',
  'test-expander-row',
  'test-flap',
  'test-header-bar',
  'test-header-group',
  'test-keypad',
  'test-leaflet',
  'test-preferences-group',
  'test-preferences-page',
  'test-preferences-row',
  'test-preferences-window',
  'test-search-bar',
  'test-squeezer',
  'test-status-page',
  'test-style-manager',
  'test-swipe-group',
  'test-tab-bar',
  'test-tab-view',
  'test-value-object',
  'test-view-switcher',
  'test-view-switcher-bar',
  'test-window',
  'test-window-handle',
]

foreach test_name : test_names
  t = executable(test_name, [test_name + '.c'] + libhandy_generated_headers,
                       c_args: test_cflags,
                    link_args: test_link_args,
                 dependencies: libhandy_deps + [libhandy_dep],
                          pie: true,
                )
  test(test_name, t, env: test_env)
endforeach

endif
