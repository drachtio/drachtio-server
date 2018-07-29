#!/usr/bin/python

# Copyright 2003 Vladimir Prus
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or http://www.boost.org/LICENSE_1_0.txt)


import BoostBuild

t = BoostBuild.Tester(use_test_config=False)

t.write("a.cpp", """
#ifdef CF_IS_OFF
int main() {}
#endif
""")

t.write("b.cpp", """
#if defined(CF_1) && !defined(CF_IS_OFF)
int main() {}
#endif
""")

t.write("c.cpp", """
#ifdef FOO
int main() {}
#endif
""")

t.write("d.cpp", """
#ifndef CF_IS_OFF
int main() {}
#endif
""")

t.write("e.cpp", """
#if !defined(CF_IS_OFF) && defined(CF_2) && !defined(CF_1)
int main() {}
#endif
""")

t.write("f.cpp", """
#if defined(CF_1)
int main() {}
#endif
""")

t.write("g.cpp", """
#if defined(FOPT_2)
int main() {}
#endif
""")

t.write("h.cpp", """
#if defined(CX_2)
int main() {}
#endif
""")

t.write("jamfile.jam", """
# See if default value of composite feature 'cf' will be expanded to
# <define>CF_IS_OFF.
exe a : a.cpp ;

# See if subfeature in requirements in expanded.
exe b : b.cpp : <cf>on-1 ;

# See if conditional requirements are recursively expanded.
exe c : c.cpp : <toolset>$toolset:<variant>release <variant>release:<define>FOO
    ;

# Composites specified in the default build should not
# be expanded if they are overridden in the the requirements.
exe d : d.cpp : <cf>on : <cf>off ;

# Overriding a feature should clear subfeatures and
# apply default values of subfeatures.
exe e : e.cpp : <cf>always : <cf>on-1 ;

# Subfeatures should not be changed if the parent feature doesn't change
exe f : f.cpp : <cf>on : <cf>on-1 ;

# If a subfeature is not specific to the value of the parent feature,
# then changing the parent value should not clear the subfeature.
exe g : g.cpp : <fopt>off : <fopt>on-2 ;

# If the default value of a composite feature adds an optional
# feature which has a subfeature with a default, then that
# default should be added.
exe h : h.cpp ;
""")

t.write("jamroot.jam", """
import feature ;
feature.feature cf : off on always : composite incidental ;
feature.compose <cf>off : <define>CF_IS_OFF ;
feature.subfeature cf on : version : 1 2 : composite optional incidental ;
feature.compose <cf-on:version>1 : <define>CF_1 ;
feature.subfeature cf always : version : 1 2 : composite incidental ;
feature.compose <cf-always:version>1 : <define>CF_2 ;
feature.feature fopt : on off : optional incidental ;
feature.subfeature fopt : version : 1 2 : composite incidental ;
feature.compose <fopt-version>2 : <define>FOPT_2 ;

feature.feature cx1 : on : composite incidental ;
feature.feature cx2 : on : optional incidental ;
feature.subfeature cx2 on : sub : 1 : composite incidental ;
feature.compose <cx1>on : <cx2>on ;
feature.compose <cx2-on:sub>1 : <define>CX_2 ;
""")

t.expand_toolset("jamfile.jam")

t.run_build_system()
t.expect_addition(["bin/$toolset/debug*/a.exe",
                   "bin/$toolset/debug*/b.exe",
                   "bin/$toolset/release*/c.exe",
                   "bin/$toolset/debug*/d.exe",
                   "bin/$toolset/debug*/e.exe",
                   "bin/$toolset/debug*/f.exe",
                   "bin/$toolset/debug*/g.exe",
                   "bin/$toolset/debug*/h.exe"])

t.rm("bin")


# Test for issue BB60.

t.write("test.cpp", """
#include "header.h"
int main() {}
""")

t.write("jamfile.jam", """
project : requirements <toolset>$toolset:<include>foo ;
exe test : test.cpp : <toolset>$toolset ;
""")

t.expand_toolset("jamfile.jam")
t.write("foo/header.h", "\n")
t.write("jamroot.jam", "")

t.run_build_system()
t.expect_addition("bin/$toolset/debug*/test.exe")

t.cleanup()
