q2aas_inc = include_directories(
  '.',
  'deps',
  'deps/botlib',
  'deps/qcommon',
)

q2aas_python = find_program('python3')

q2aas_src = files(
  '_files.c',
  'aas_areamerging.c',
  'aas_cfg.c',
  'aas_create.c',
  'aas_edgemelting.c',
  'aas_facemerging.c',
  'aas_file.c',
  'aas_gsubdiv.c',
  'aas_map.c',
  'aas_prunenodes.c',
  'aas_store.c',
  'be_aas_bspc.c',
  'deps/botlib/be_aas_bspq3.c',
  'deps/botlib/be_aas_cluster.c',
  'deps/botlib/be_aas_move.c',
  'deps/botlib/be_aas_optimize.c',
  'deps/botlib/be_aas_reach.c',
  'deps/botlib/be_aas_sample.c',
  'brushbsp.c',
  'bspc.c',
  'deps/qcommon/cm_load.c',
  'deps/qcommon/cm_patch.c',
  'deps/qcommon/cm_test.c',
  'deps/qcommon/cm_trace.c',
  'csg.c',
  'glfile.c',
  'l_bsp_ent.c',
  'l_bsp_hl.c',
  'l_bsp_q1.c',
  'l_bsp_q2.c',
  'l_bsp_q3.c',
  'l_bsp_sin.c',
  'l_cmd.c',
  'deps/botlib/l_libvar.c',
  'l_log.c',
  'l_math.c',
  'l_mem.c',
  'l_poly.c',
  'deps/botlib/l_precomp.c',
  'l_qfiles.c',
  'deps/botlib/l_script.c',
  'deps/botlib/l_struct.c',
  'l_threads.c',
  'l_utils.c',
  'leakfile.c',
  'map.c',
  'map_hl.c',
  'map_q1.c',
  'map_q2.c',
  'map_q3.c',
  'map_sin.c',
  'deps/qcommon/md4.c',
  'nodraw.c',
  'portals.c',
  'textures.c',
  'tree.c',
  'deps/qcommon/unzip.c',
  'worr_q2aas_q2trace.c',
)

q2aas_c_args = [
  '-DBSPC',
  '-DCom_Memcpy=memcpy',
  '-DCom_Memset=memset',
]

if host_machine.system() == 'linux'
  q2aas_c_args += '-DLINUX'
endif

if win32
  if host_machine.cpu_family() == 'x86_64'
    q2aas_c_args += '-DARCH_STRING="x86_64"'
  elif host_machine.cpu_family() == 'x86'
    q2aas_c_args += '-DARCH_STRING="x86"'
  endif
endif

if cc.get_argument_syntax() == 'gcc'
  q2aas_c_args += ['-include', 'worr_q2aas_compat.h']
  q2aas_c_args += cc.get_supported_arguments([
    '-fno-common',
    '-Wno-format',
    '-Wno-implicit-fallthrough',
    '-Wno-missing-prototypes',
    '-Wno-pointer-sign',
    '-Wno-sign-compare',
    '-Wno-unused-but-set-variable',
    '-Wno-unused-function',
    '-Wno-unused-variable',
  ])
  if not win32
    q2aas_c_args += '-Dstricmp=strcasecmp'
  endif
elif cc.get_id() == 'msvc'
  q2aas_c_args += '/FIworr_q2aas_compat.h'
  q2aas_c_args += [
    '/wd4018',
    '/wd4244',
    '/wd4267',
    '/wd4305',
    '/wd4701',
    '/wd4706',
    '/wd4996',
  ]
endif

q2aas_deps = [dependency('threads')]
if not win32
  libm_dep = cc.find_library('m', required: false)
  if libm_dep.found()
    q2aas_deps += libm_dep
  endif
endif

worr_q2aas_exe = executable('worr_q2aas',
  q2aas_src,
  include_directories: q2aas_inc,
  dependencies: q2aas_deps,
  c_args: q2aas_c_args,
  link_args: common_link_args + exe_link_args,
  install: false,
  build_by_default: true,
  override_options: ['c_std=gnu11'],
)

run_target('q2aas-config-smoke',
  command: [
    q2aas_python,
    files('validate_worr_q2aas.py'),
    '--tool', worr_q2aas_exe.full_path(),
    '--cfg', files('cfg/worr_q2.cfg'),
  ],
)

run_target('q2aas-staged-smoke',
  command: [
    q2aas_python,
    files('validate_worr_q2aas.py'),
    '--tool', worr_q2aas_exe.full_path(),
    '--cfg', files('cfg/worr_q2.cfg'),
    '--manifest', files('validation_manifest.json'),
    '--skip-missing-manifest-maps',
    '--allow-empty-map-set',
    '--require-reachability',
    '--require-q2-bsp',
    '--require-clean-bsp-lumps',
    '--require-spawn-coverage',
    '--require-item-coverage',
    '--require-high-value-reachability',
    '--write-aas-metadata',
    '--invalid-input-smoke',
    '--manifest-schema-smoke',
    '--report-json', '.tmp/q2aas/validation-report.json',
  ],
)

run_target('q2aas-package-map-smoke',
  command: [
    q2aas_python,
    files('validate_worr_q2aas.py'),
    '--tool', worr_q2aas_exe.full_path(),
    '--cfg', files('cfg/worr_q2.cfg'),
    '--package-map-smoke',
    '--require-reachability',
    '--require-q2-bsp',
    '--require-clean-bsp-lumps',
    '--require-spawn-coverage',
    '--require-item-coverage',
    '--require-high-value-reachability',
    '--write-aas-metadata',
    '--report-json', '.tmp/q2aas/package-map-smoke-report.json',
  ],
)

run_target('q2aas-stage-aas',
  command: [
    q2aas_python,
    files('validate_worr_q2aas.py'),
    '--tool', worr_q2aas_exe.full_path(),
    '--cfg', files('cfg/worr_q2.cfg'),
    '--manifest', files('validation_manifest.json'),
    '--skip-missing-manifest-maps',
    '--allow-empty-map-set',
    '--require-reachability',
    '--require-q2-bsp',
    '--require-clean-bsp-lumps',
    '--require-spawn-coverage',
    '--require-item-coverage',
    '--require-high-value-reachability',
    '--write-aas-metadata',
    '--stage-aas',
    '--stage-aas-dir', '.install/basew/maps',
    '--report-json', '.tmp/q2aas/stage-report.json',
  ],
)

run_target('q2aas-stage-audit',
  command: [
    q2aas_python,
    files('audit_worr_q2aas_stage.py'),
    '--report-json', '.tmp/q2aas/stage-report.json',
    '--stage-dir', '.install/basew/maps',
    '--require-staged-output',
    '--audit-report-json', '.tmp/q2aas/stage-audit-report.json',
  ],
)

run_target('q2aas-package-audit',
  command: [
    q2aas_python,
    files('audit_worr_q2aas_package.py'),
    '--report-json', '.tmp/q2aas/stage-report.json',
    '--install-dir', '.install',
    '--base-game', 'basew',
    '--archive-name', 'pak0.pkz',
    '--audit-report-json', '.tmp/q2aas/package-audit-report.json',
  ],
)

run_target('q2aas-package-aas',
  command: [
    q2aas_python,
    files('package_worr_q2aas_archive.py'),
    '--report-json', '.tmp/q2aas/stage-report.json',
    '--install-dir', '.install',
    '--base-game', 'basew',
    '--archive-name', 'pak0.pkz',
    '--package-report-json', '.tmp/q2aas/package-archive-report.json',
  ],
)

run_target('q2aas-package-archive-audit',
  command: [
    q2aas_python,
    files('audit_worr_q2aas_package.py'),
    '--report-json', '.tmp/q2aas/stage-report.json',
    '--install-dir', '.install',
    '--base-game', 'basew',
    '--archive-name', 'pak0.pkz',
    '--require-archive-member',
    '--audit-report-json', '.tmp/q2aas/package-archive-audit-report.json',
  ],
)
