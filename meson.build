project('openffa', 'c',
  license: 'GPL-2.0-or-later',
  version: run_command(find_program('python3'), 'version.py', check: true).stdout().strip(),
  meson_version: '>= 0.59.0',
  default_options: [
    'c_std=gnu99',
    'buildtype=debugoptimized',
  ],
)

src = [
  'g_bans.c',
  'g_chase.c',
  'g_cmds.c',
  'g_combat.c',
  'g_func.c',
  'g_items.c',
  'g_main.c',
  'g_misc.c',
  'g_phys.c',
  'g_spawn.c',
  'g_svcmds.c',
  'g_target.c',
  'g_trigger.c',
  'g_utils.c',
  'g_vote.c',
  'g_weapon.c',
  'p_client.c',
  'p_hud.c',
  'p_menu.c',
  'p_view.c',
  'p_weapon.c',
  'q_shared.c',
]

cc = meson.get_compiler('c')

win32 = host_machine.system() == 'windows'
x86 = host_machine.cpu_family() == 'x86'

cpuremap = {
  'x86': win32 ? 'x86' : 'i386',
  'aarch64': 'arm64',
}

cpu = host_machine.cpu_family()
if cpu in cpuremap
  cpu = cpuremap[cpu]
endif

args = ['-DHAVE_CONFIG_H']
link_args = []

if win32
  args += '-D_WIN32_WINNT=0x0601'
endif

if cc.get_argument_syntax() == 'gcc'
  if x86
    add_global_arguments('-msse2', '-mfpmath=sse', language: 'c')
  endif

  test_args = [
    '-fsigned-char',
    '-Wpointer-arith',
    '-Wformat-security',
    '-Werror=vla',
    '-Wstrict-prototypes',
  ]

  args += cc.get_supported_arguments(test_args)

  if win32
    args += '-D__USE_MINGW_ANSI_STDIO=1'
    link_args += '-Wl,--nxcompat,--dynamicbase'

    if cpu == 'x86_64'
      link_args += '-Wl,--high-entropy-va,--image-base=0x180000000'
    endif

    link_args += '-static-libgcc'
  endif
elif cc.get_id() == 'msvc'
  args += ['/D_USE_MATH_DEFINES', '/wd4146', '/wd4244', '/wd4305']
endif

add_project_arguments(args, language: 'c')
add_project_link_arguments(link_args, language: 'c')

fallback_opt = ['default_library=static']

sqlite3 = dependency('sqlite3',
  required:        get_option('sqlite3'),
  default_options: fallback_opt,
)
if sqlite3.found()
  src += 'g_sqlite.c'
endif

curl = dependency('libcurl',
  required:        get_option('libcurl'),
  default_options: fallback_opt,
)
if curl.found()
  src += 'g_curl.c'
endif

if get_option('udp-logger').require(not win32).allowed()
  src += 'g_udp.c'
endif

deps = [
  sqlite3,
  curl,
  cc.find_library('m', required: false),
]

if win32
  src += import('windows').compile_resources('openffa.rc', args: '-DHAVE_CONFIG_H', include_directories: '.')
  deps += cc.find_library('ws2_32')
endif

shared_library('game' + cpu, src,
  name_prefix:           '',
  dependencies:          deps,
  gnu_symbol_visibility: 'hidden',
)

config = configuration_data()
config.set('OPENFFA_REVISION',       meson.project_version().substring(1).split('~')[0].to_int())
config.set_quoted('OPENFFA_VERSION', meson.project_version())
config.set_quoted('CPUSTRING',       cpu)
config.set('USE_CURL',               curl.found())
config.set('USE_SQLITE',             sqlite3.found())
config.set('USE_UDP',                get_option('udp-logger').require(not win32).allowed())
config.set('USE_FPS',                get_option('variable-fps'))
config.set('USE_DEBUG',              get_option('debug'))

configure_file(output: 'config.h', configuration: config)
