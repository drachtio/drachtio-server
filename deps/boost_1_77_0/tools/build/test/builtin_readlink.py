#!/usr/bin/python

# Copyright 2012 Steven Watanabe
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE.txt or https://www.bfgroup.xyz/b2/LICENSE.txt)

import BoostBuild
import os

t = BoostBuild.Tester(pass_toolset=0)

t.write("link-target", "")
os.symlink("link-target", "link")

t.write("file.jam", """
ECHO [ READLINK link ] ;
EXIT [ READLINK link-target ] : 0 ;
""")

t.run_build_system(["-ffile.jam"], stdout="""link-target

""")

t.cleanup()
