# Copyright 2022 Google LLC
# SPDX-License-Identifier: MIT

project(
  'vatest',
  'c',
  version: '0.1',
  license: 'MIT',
  meson_version: '>=0.53',
  default_options: ['c_std=c11', 'warning_level=3'],
)

cc = meson.get_compiler('c')

warning_args = cc.get_supported_arguments([
  '-Wno-pedantic',
  '-Wno-missing-field-initializers',
  '-Wno-unused-parameter',
])

dep_dl = cc.find_library('dl')
dep_m = cc.find_library('m', required: false)

dep_libdrm = dependency('libdrm')
dep_libva = dependency('libva')
dep_libva_drm = dependency('libva-drm')

add_project_arguments(['-D_GNU_SOURCE', warning_args], language: 'c')

idep_vautil = declare_dependency(
  sources: ['vautil.h'],
  dependencies: [dep_dl, dep_m, dep_libdrm, dep_libva, dep_libva_drm],
)

tests = [
  'info',
]

foreach t : tests
  test_deps = [idep_vautil]

  executable(
    t,
    sources: t + '.c',
    dependencies: test_deps,
  )
endforeach
