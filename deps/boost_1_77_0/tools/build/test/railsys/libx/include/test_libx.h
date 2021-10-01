//  Copyright (c) 2003 Institute of Transport,
//             Railway Construction and Operation,
//             University of Hanover, Germany
//
//  Use, modification and distribution are subject to the
//  Boost Software License, Version 1.0. (See accompanying file
//  LICENSE.txt or copy at https://www.bfgroup.xyz/b2/LICENSE.txt)

#ifdef _WIN32
#ifdef LIBX_SOURCE
__declspec(dllexport)
#else
__declspec(dllimport)
#endif
#endif
class TestLibX
{
public:

    TestLibX();

    // Needed to suppress 'unused variable' warning
    // in some cases.
    void do_something() {}
};
