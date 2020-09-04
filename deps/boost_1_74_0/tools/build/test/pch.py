#!/usr/bin/python

# Copyright 2006 Vladimir Prus.
# Distributed under the Boost Software License, Version 1.0. (See
# accompanying file LICENSE_1_0.txt or copy at
# http://www.boost.org/LICENSE_1_0.txt)

import BoostBuild
from time import sleep


t = BoostBuild.Tester()

t.write("jamroot.jam", """
import pch ;
project : requirements <warnings-as-errors>on ;
cpp-pch pch : pch.hpp : <toolset>msvc:<source>pch.cpp <include>. ;
cpp-pch pch-afx : pch.hpp : <define>HELLO <toolset>msvc:<source>pch.cpp <include>. ;
exe hello : hello.cpp pch : <include>. ;
exe hello-afx : hello-afx.cpp pch-afx : <define>HELLO <include>. ;
""")

t.write("pch.hpp.bad", """
THIS WILL NOT COMPILE
""")

# Note that pch.hpp is written after pch.hpp.bad, so its timestamp will not be
# less than timestamp of pch.hpp.bad.
sleep(1)
t.write("pch.hpp", """
#undef HELLO
class TestClass
{
public:
    TestClass( int, int ) {}
};
""")

t.write("pch.cpp", """#include <pch.hpp>
""")

for name in ("hello.cpp", "hello-afx.cpp"):
    t.write(name, """#include <pch.hpp>
int main() { TestClass c(1, 2); }
""")

t.run_build_system()
t.expect_addition("bin/$toolset/debug*/hello.exe")
t.expect_addition("bin/$toolset/debug*/hello-afx.exe")


# Now make the header unusable, without changing timestamp. If everything is OK,
# B2 will not recreate PCH, and compiler will happily use pre-compiled
# header, not noticing that the real header is bad.

t.copy_preserving_timestamp("pch.hpp.bad", "pch.hpp")

t.rm("bin/$toolset/debug*/hello.obj")
t.rm("bin/$toolset/debug*/hello-afx.obj")

t.run_build_system()
t.expect_addition("bin/$toolset/debug*/hello.obj")
t.expect_addition("bin/$toolset/debug*/hello-afx.obj")

t.cleanup()
