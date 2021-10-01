//  Copyright (c) 2003 Institute of Transport,
//             Railway Construction and Operation,
//             University of Hanover, Germany
//
//  Use, modification and distribution are subject to the
//  Boost Software License, Version 1.0. (See accompanying file
//  LICENSE.txt or copy at https://www.bfgroup.xyz/b2/LICENSE.txt)


#include <qobject.h>

class TestA : public QObject
{
    Q_OBJECT

public:

    TestA();

    // Needed to suppress 'unused variable' varning.
    void do_something() { }
};
