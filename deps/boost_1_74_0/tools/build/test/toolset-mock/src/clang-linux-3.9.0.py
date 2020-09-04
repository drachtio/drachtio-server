#!/usr/bin/python
#
# Copyright 2017 Steven Watanabe
# Copyright 2020 Rene Rivera
#
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at
# http://www.boost.org/LICENSE_1_0.txt)

from MockProgram import *

command('clang++', '-print-prog-name=ar', stdout=script('ar.py'))
command('clang++', '-print-prog-name=ranlib', stdout=script('ranlib.py'))

if allow_properties('variant=debug', 'link=shared', 'threading=single', 'runtime-link=shared'):
    command('clang++', unordered(ordered('-x', 'c++'), '-O0', '-fno-inline', '-Wall', '-g', '-fPIC', '-c'), '-o', output_file('bin/clang-linux-3.9.0/debug/lib.o'), input_file(source='lib.cpp'))
    command('clang++', '-o', output_file('bin/clang-linux-3.9.0/debug/libl1.so'), arg_file('@bin/clang-linux-3.9.0/debug*/libl1.so.rsp'), unordered('-g', '-fPIC'))
    command('clang++', unordered(ordered('-x', 'c++'), '-O0', '-fno-inline', '-Wall', '-g', '-fPIC', '-c'), '-o', output_file('bin/clang-linux-3.9.0/debug/main.o'), input_file(source='main.cpp'))
    command('clang++', '-o', output_file('bin/clang-linux-3.9.0/debug/test'), arg_file('@bin/clang-linux-3.9.0/debug*/test.rsp'), unordered('-g', '-fPIC'))

if allow_properties('variant=release', 'link=shared', 'threading=single', 'runtime-link=shared', 'strip=on'):
    command('clang++', unordered(ordered('-x', 'c++'), '-O3', '-Wno-inline', '-Wall', '-fPIC', '-DNDEBUG', '-c'), '-o', output_file('bin/clang-linux-3.9.0/release/lib.o'), input_file(source='lib.cpp'))
    command('clang++', '-o', output_file('bin/clang-linux-3.9.0/release/strip-on/libl1.so'), arg_file('@bin/clang-linux-3.9.0/release/strip-on*/libl1.so.rsp'), unordered('-fPIC', '-Wl,--strip-all'))
    command('clang++', unordered(ordered('-x', 'c++'), '-O3', '-Wno-inline', '-Wall', '-fPIC', '-DNDEBUG', '-c'), '-o', output_file('bin/clang-linux-3.9.0/release/main.o'), input_file(source='main.cpp'))
    command('clang++', '-o', output_file('bin/clang-linux-3.9.0/release/test'), arg_file('@bin/clang-linux-3.9.0/release/strip-on*/test.rsp'), unordered('-fPIC', '-Wl,--strip-all'))

if allow_properties('variant=debug', 'link=shared', 'threading=multi', 'runtime-link=shared'):
    command('clang++', unordered(ordered('-x', 'c++'), '-O0', '-fno-inline', '-Wall', '-g', '-pthread', '-fPIC', '-c'), '-o', output_file('bin/clang-linux-3.9.0/debug/threading-multi/lib.o'), input_file(source='lib.cpp'))
    command('clang++', '-o', output_file('bin/clang-linux-3.9.0/debug/threading-multi/libl1.so'), arg_file('@bin/clang-linux-3.9.0/debug*/threading-multi/libl1.so.rsp'), unordered('-g', '-pthread', '-fPIC'))
    command('clang++', unordered(ordered('-x', 'c++'), '-O0', '-fno-inline', '-Wall', '-g', '-pthread', '-fPIC', '-c'), '-o', output_file('bin/clang-linux-3.9.0/debug/threading-multi/main.o'), input_file(source='main.cpp'))
    command('clang++', '-o', output_file('bin/clang-linux-3.9.0/debug/threading-multi/test'), arg_file('@bin/clang-linux-3.9.0/debug*/threading-multi/test.rsp'), unordered('-g', '-pthread', '-fPIC'))

if allow_properties('variant=debug', 'link=static', 'threading=single', 'runtime-link=shared'):
    command('clang++', unordered(ordered('-x', 'c++'), '-O0', '-fno-inline', '-Wall', '-g', '-c'), '-o', output_file('bin/clang-linux-3.9.0/debug/link-static/lib.o'), input_file(source='lib.cpp'))
    command('clang++', unordered(ordered('-x', 'c++'), '-O0', '-fno-inline', '-Wall', '-g', '-c'), '-o', output_file('bin/clang-linux-3.9.0/debug/link-static/main.o'), input_file(source='main.cpp'))
    command('clang++', '-o', output_file('bin/clang-linux-3.9.0/debug/link-static/test'), arg_file('@bin/clang-linux-3.9.0/debug/link-static*/test.rsp'), '-g')

if allow_properties('variant=debug', 'link=static', 'threading=single', 'runtime-link=static'):
    command('clang++', unordered(ordered('-x', 'c++'), '-O0', '-fno-inline', '-Wall', '-g', '-c'), '-o', output_file('bin/clang-linux-3.9.0/debug/link-static/runtime-link-static/lib.o'), input_file(source='lib.cpp'))
    command('clang++', unordered(ordered('-x', 'c++'), '-O0', '-fno-inline', '-Wall', '-g', '-c'), '-o', output_file('bin/clang-linux-3.9.0/debug/link-static/runtime-link-static/main.o'), input_file(source='main.cpp'))
    command('clang++', '-o', output_file('bin/clang-linux-3.9.0/debug/link-static/runtime-link-static/test'), arg_file('@bin/clang-linux-3.9.0/debug/link-static/runtime-link-static*/test.rsp'), unordered('-g', '-static'))

if allow_properties('variant=debug', 'link=shared', 'threading=single', 'runtime-link=shared', 'architecture=x86', 'address-model=32'):
    command('clang++', unordered(ordered('-x', 'c++'), '-O0', '-fno-inline', '-Wall', '-g', '-march=i686', '-m32', '-fPIC', '-c'), '-o', output_file('bin/clang-linux-3.9.0/debug/lib.o'), input_file(source='lib.cpp'))
    command('clang++', '-o', output_file('bin/clang-linux-3.9.0/debug/libl1.so'), arg_file('@bin/clang-linux-3.9.0/debug/address-model-32/architecture-x86*/libl1.so.rsp'), unordered('-g', '-march=i686', '-fPIC', '-m32'))
    command('clang++', unordered(ordered('-x', 'c++'), '-O0', '-fno-inline', '-Wall', '-g', '-march=i686', '-m32', '-fPIC', '-c'), '-o', output_file('bin/clang-linux-3.9.0/debug/main.o'), input_file(source='main.cpp'))
    command('clang++', '-o', output_file('bin/clang-linux-3.9.0/debug/test'), arg_file('@bin/clang-linux-3.9.0/debug/address-model-32/architecture-x86*/test.rsp'), unordered('-g', '-march=i686', '-fPIC', '-m32'))

main()
