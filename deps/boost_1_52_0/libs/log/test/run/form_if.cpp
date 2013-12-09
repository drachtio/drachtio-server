/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   form_if.cpp
 * \author Andrey Semashev
 * \date   05.02.2009
 *
 * \brief  This header contains tests for the \c if_ formatter.
 */

#define BOOST_TEST_MODULE form_if

#include <string>
#include <ostream>
#include <sstream>
#include <boost/function.hpp>
#include <boost/test/included/unit_test.hpp>
#include <boost/log/attributes/constant.hpp>
#include <boost/log/attributes/attribute_set.hpp>
#include <boost/log/formatters/if.hpp>
#include <boost/log/formatters/attr.hpp>
#include <boost/log/formatters/stream.hpp>
#include <boost/log/filters/has_attr.hpp>
#include <boost/log/core/record.hpp>
#include "char_definitions.hpp"
#include "make_record.hpp"

namespace logging = boost::log;
namespace attrs = logging::attributes;
namespace fmt = logging::formatters;
namespace flt = logging::filters;

// The test checks that conditional formatting work
BOOST_AUTO_TEST_CASE_TEMPLATE(conditional_formatting, CharT, char_types)
{
    typedef logging::basic_attribute_set< CharT > attr_set;
    typedef std::basic_string< CharT > string;
    typedef std::basic_ostringstream< CharT > osstream;
    typedef logging::basic_record< CharT > record;
    typedef boost::function< void (osstream&, record const&) > formatter;
    typedef test_data< CharT > data;

    attrs::constant< int > attr1(10);
    attrs::constant< double > attr2(5.5);

    attr_set set1;
    set1[data::attr1()] = attr1;
    set1[data::attr2()] = attr2;

    record rec = make_record(set1);
    rec.message() = data::some_test_string();

    // Check for various modes of attribute value type specification
    {
        osstream strm1;
        formatter f =
            fmt::if_(flt::has_attr< int >(data::attr1()))
            [
                fmt::stream << fmt::attr(data::attr1())
            ];
        f(strm1, rec);
        osstream strm2;
        strm2 << 10;
        BOOST_CHECK(equal_strings(strm1.str(), strm2.str()));
    }
    {
        osstream strm1;
        formatter f =
            fmt::if_(flt::has_attr< int >(data::attr2()))
            [
                fmt::stream << fmt::attr(data::attr2())
            ];
        f(strm1, rec);
        BOOST_CHECK(equal_strings(strm1.str(), string()));
    }
    {
        osstream strm1;
        formatter f =
            fmt::if_(flt::has_attr< int >(data::attr1()))
            [
                fmt::stream << fmt::attr(data::attr1())
            ]
            .else_
            [
                fmt::stream << fmt::attr(data::attr2())
            ];
        f(strm1, rec);
        osstream strm2;
        strm2 << 10;
        BOOST_CHECK(equal_strings(strm1.str(), strm2.str()));
    }
    {
        osstream strm1;
        formatter f =
            fmt::if_(flt::has_attr< int >(data::attr2()))
            [
                fmt::stream << fmt::attr(data::attr1())
            ]
            .else_
            [
                fmt::stream << fmt::attr(data::attr2())
            ];
        f(strm1, rec);
        osstream strm2;
        strm2 << 5.5;
        BOOST_CHECK(equal_strings(strm1.str(), strm2.str()));
    }
}
