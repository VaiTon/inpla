project(
    'inpla',
    'c',
    default_options : ['c_std=gnu99', 'warning_level=2', 'optimization=3'],
)

src_dir = 'src'
obj_dir = 'build'
inc_dir = include_directories(src_dir, src_dir / 'linenoise')

c_args = [
    '-Wall',
    '-Winline',
    # increase inlining limits
    '--param', 'inline-unit-growth=1000',
    '--param', 'max-inline-insns-single=1200',
]

heap_type = get_option('heap_type')
if heap_type == 'expandable'
    c_args += ['-DEXPANDABLE_HEAP']
    message('Using expandable heap')
elif heap_type == 'fixed'
    c_args += ['-DFIXED_HEAP']
    message('Using fixed heap')
elif heap_type == 'flex_expandable'
    c_args += ['-DFLEX_EXPANDABLE_HEAP']
    message('Using flexible expandable heap')
else
    error('Invalid heap type: ' + heap_type)
endif

# Parser and lexer generation
bison = find_program('bison', required : true)
flex = find_program('flex', required : true)

parser_y = src_dir / 'parser.y'
lexer_l = src_dir / 'lex.l'

lex_c = custom_target(
    'lex.yy.c',
    input : lexer_l,
    output : 'lex.yy.c',
    command : [flex, '-o', '@OUTPUT@', '@INPUT@'],
    build_by_default : true,
    depend_files : [],
)

parser_c = custom_target(
    'parser.tab.c',
    input : parser_y,
    output : 'parser.tab.c',
    command : [bison, '-o', '@OUTPUT@', '@INPUT@'],
    depends : lex_c,
    build_by_default : true,
)

# Patch linenoise if needed
linenoise_c = src_dir / 'linenoise/linenoise.c'
linenoise_patch = src_dir / 'linenoise/linenoise-multiline.patch'
linenoise_patched = custom_target(
    'linenoise.c',
    input : [linenoise_c, linenoise_patch],
    output : 'linenoise.c',
    command : [
        'patch',
        '--dry-run',
        '-o', '@OUTPUT@',
        '@INPUT0@',
        '-i', '@INPUT1@',
    ],
    build_by_default : true,
)

sources = [
    linenoise_patched,
    src_dir / 'ast.c',
    src_dir / 'id_table.c',
    src_dir / 'name_table.c',
    src_dir / 'inpla.c',
    src_dir / 'heap.c',
    src_dir / 'cmenv.c',
    src_dir / 'imcode.c',
    src_dir / 'vm.c',
    src_dir / 'ruletable.c',
    src_dir / 'opt.c',
]

deps = src_dir / 'config.h'


inpla = executable(
    'inpla',
    sources,
    include_directories : inc_dir,
    link_depends : [parser_c, lex_c],
    c_args : c_args,
    install : true,
)

# Threaded version
thread_dep = dependency('threads', required : false)
executable(
    'inpla_thread',
    sources,
    include_directories : inc_dir,
    c_args : c_args + ['-DTHREAD'],
    dependencies : thread_dep.found() ? [thread_dep] : [],
    install : false,
)


test_cases = [
    'sample/lambda/245II.in',
    'sample/lambda/245II_abbr.in',
    'sample/lambda/245II_new.in',
    'sample/lambda/linear-systemT.in',
    #    'sample/process_networks/processnet1.in',
    #    'sample/process_networks/processnet_fib.in',
    'sample/pseudo_higher_order/map.in',
    'sample/pseudo_higher_order/reduce.in',
    'sample/puzzle/hanoi.in',
    'sample/puzzle/nqueen.in',
    'sample/recursive_functions/ack_3_5.in',
    'sample/recursive_functions/fib-tail_9.in',
    'sample/recursive_functions/fib_9.in',
    'sample/recursive_functions/fib_9_abbr.in',
    'sample/recursive_functions/gcd.in',
    'sample/recursive_functions/gcd_another.in',
    'sample/recursive_functions/pow.in',
    'sample/sort/bsort.in',
    'sample/sort/isort.in',
    'sample/sort/msort.in',
    'sample/sort/qsort.in',
    'sample/turing_machine/TuringMachine.in',
    'sample/unary_numbers/AckSZ-3_5.in',
    'sample/unary_numbers/FibSZ_9.in',
    'sample/unary_numbers/GCM2023/AckSZ-3_10-reuse.in',
    'sample/unary_numbers/GCM2023/AckSZ-3_10.in',
    'sample/unary_numbers/GCM2023/FibSZ_31-reuse.in',
    'sample/unary_numbers/GCM2023/FibSZ_31.in',
    'sample/unary_numbers/GCM2023/hanoi19-reuse.in',
    'sample/unary_numbers/GCM2023/hanoi19.in',
    'sample/unary_numbers/GCM2023/nqueenSZ_10-reuse.in',
    'sample/unary_numbers/GCM2023/nqueenSZ_10.in',
    'sample/unary_numbers/hanoiSZ.in',
    'sample/unary_numbers/nqueenSZ.in',
]

fs = import('fs')

foreach test_case : test_cases
    test_file = files(test_case)
    test_name = fs.stem(test_case)
    test(test_name, inpla, args : ['-f', test_file], depends : inpla)
endforeach
