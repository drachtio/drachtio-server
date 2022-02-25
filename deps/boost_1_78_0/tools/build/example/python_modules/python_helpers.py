# Copyright 2006 Vladimir Prus
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE.txt or https://www.bfgroup.xyz/b2/LICENSE.txt)

# Declare a couple of functions called from B2
#
# Each function will receive as many arguments as there ":"-separated
# arguments in bjam call. Each argument is a list of strings.
# As a special exception (aka bug), if no arguments are passed in bjam,
# Python function will be passed a single empty list.
#
# All Python functions must return a list of strings, which may be empty.

def test1(l):
    return ["foo", "bar"]

def test2(l, l2):
    return [l[0], l2[0]]