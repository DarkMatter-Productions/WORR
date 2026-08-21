project(
  'monocypher',
  'c',
  version: '4.0.3',
  license: ['BSD-2-Clause', 'CC0-1.0'],
  default_options: ['c_std=c99'],
)

monocypher_include = include_directories('src', 'src/optional')

monocypher_library = static_library(
  'monocypher',
  [
    'src/monocypher.c',
    'src/optional/monocypher-ed25519.c',
  ],
  include_directories: monocypher_include,
  install: false,
  pic: true,
)

monocypher_dep = declare_dependency(
  include_directories: monocypher_include,
  link_with: monocypher_library,
)
