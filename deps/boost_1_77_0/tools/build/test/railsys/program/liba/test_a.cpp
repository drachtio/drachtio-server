//  Copyright (c) 2003 Institute of Transport,
//             Railway Construction and Operation,
//             University of Hanover, Germany
//
//  Use, modification and distribution are subject to the
//  Boost Software License, Version 1.0. (See accompanying file
//  LICENSE.txt or copy at https://www.bfgroup.xyz/b2/LICENSE.txt)

#include "../include/test_a.h"

#include <test_libx.h>

TestA::TestA()
{
    TestLibX aTestLibX;
    aTestLibX.do_something();
}
