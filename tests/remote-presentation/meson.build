native_rules = executable('remote-presentation-rules', 'rules.cpp', native: true, cpp_args: ['-UNDEBUG'])
test('remote-presentation-rules', native_rules)
executable('remote-presentation', 'presentation.cpp',
  dependencies: [cpp.find_library('dxgi'), cpp.find_library('dxguid'),
                 cpp.find_library('gdi32'), cpp.find_library('user32')],
  link_args: ['-municode'])
