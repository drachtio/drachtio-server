#!/usr/bin/python

# (c) Copyright Juergen Hunold 2012
# Use, modification, and distribution are subject to the
# Boost Software License, Version 1.0. (See accompanying file
# LICENSE.txt or copy at https://www.bfgroup.xyz/b2/LICENSE.txt)

import BoostBuild
import os

# Run test in real directory in order to find Boost.Test via Boost Top-Level
# Jamroot.
qt5_dir = os.getcwd() + "/qt5"

t = BoostBuild.Tester(workdir=qt5_dir)

t.run_build_system()

t.cleanup()
