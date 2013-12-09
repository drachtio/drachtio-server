/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   form_attr.cpp
 * \author Andrey Semashev
 * \date   01.02.2009
 *
 * \brief  This header contains tests for the \c attr formatter.
 */

#define BOOST_TEST_MODULE form_attr

#include <memory>
#include <string>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <algorithm>
#include <boost/function.hpp>
#include <boost/test/included/unit_test.hpp>
#include <boost/log/attributes/constant.hpp>
#include <boost/log/attributes/attribute_set.hpp>
#include <boost/log/formatters/attr.hpp>
#include <boost/log/formatters/stream.hpp>
#include <boost/log/utility/type_dispatch/standard_types.hpp>
#include <boost/log/core/record.hpp>
#include "char_definitions.hpp"
#include "make_record.hpp"

namespace logging = boost::log;
namespace attrs = logging::attributes;
namespace fmt = logging::formatters;

namespace {

    class my_class
    {
        int m_Data;

    public:
        explicit my_class(int data) : m_Data(data) {}

        template< typename CharT, typename TraitsT >
        friend std::basic_ostream< CharT, TraitsT >& operator<< (std::basic_ostream< CharT, TraitsT >& strm, my_class const& obj)
        {
            strm << "[data: " << obj.m_Data << "]";
            return strm;
        }
    };

} // namespace

// The test checks that default formatting work
BOOST_AUTO_TEST_CASE_TEMPLATE(default_formatting, CharT, char_types)
{
    typedef logging::basic_attribute_set< CharT > attr_set;
    typedef std::basic_string< CharT > string;
    typedef std::basic_ostringstream< CharT > osstream;
    typedef logging::basic_record< CharT > record;
    typedef boost::function< void (osstream&, record const&) > formatter;
    typedef test_data< CharT > data;

    attrs::constant< int > attr1(10);
    attrs::constant< double > attr2(5.5);
    attrs::constant< my_class > attr3(my_class(77));

    attr_set set1;
    set1[data::attr1()] = attr1;
    set1[data::attr2()] = attr2;
    set1[data::attr3()] = attr3;

    record rec = make_record(set1);
    rec.message() = data::some_test_string();

    // Check for various modes of attribute value type specification
    {
        osstream strm1;
        formatter f = fmt::stream << fmt::attr(data::attr1()) << fmt::attr(data::attr2());
        f(strm1, rec);
        osstream strm2;
        strm2 << 10 << 5.5;
        BOOST_CHECK(equal_strings(strm1.str(), strm2.str()));
    }
    {
        osstream strm1;
        formatter f = fmt::stream << fmt::attr< int >(data::attr1()) << fmt::attr< double >(data::attr2());
        f(strm1, rec);
        osstream strm2;
        strm2 << 10 << 5.5;
        BOOST_CHECK(equal_strings(strm1.str(), strm2.str()));
    }
    {
        osstream strm1;
        formatter f = fmt::stream << fmt::attr< logging::numeric_types >(data::attr1()) << fmt::attr< logging::numeric_types >(data::attr2());
        f(strm1, rec);
        osstream strm2;
        strm2 << 10 << 5.5;
        BOOST_CHECK(equal_strings(strm1.str(), strm2.str()));
    }
    // Check that custom types as attribute values are also supported
    {
        osstream strm1;
        formatter f = fmt::stream << fmt::attr< my_class >(data::attr3());
        f(strm1, rec);
        osstream strm2;
        strm2 << my_class(77);
        BOOST_CHECK(equal_strings(strm1.str(), strm2.str()));
    }
    // Check how missing attribute values are handled
    {
        osstream strm1;
        formatter f = fmt::stream
            << fmt::attr< int >(data::attr1())
            << fmt::attr(data::attr4())
            << fmt::attr< double >(data::attr2());
        BOOST_CHECK_THROW(f(strm1, rec), logging::runtime_error);
    }
    {
        osstream strm1;
        formatter f = fmt::stream
            << fmt::attr< int >(data::attr1(), std::nothrow)
            << fmt::attr(data::attr4(), std::nothrow)
            << fmt::attr< double >(data::attr2(), std::nothrow);
        f(strm1, rec);
        osstream strm2;
        strm2 << 10 << 5.5;
        BOOST_CHECK(equal_strings(strm1.str(), strm2.str()));
    }
}

// The test checks that format specification also works
BOOST_AUTO_TEST_CASE_TEMPLATE(format_specification, CharT, char_types)
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

    {
        osstream strm1;
        formatter f = fmt::stream << fmt::attr< int >(data::attr1(), data::int_format1()) << fmt::attr< double >(data::attr2(), data::fp_format1());
        f(strm1, rec);
        osstream strm2;
        strm2 << std::fixed << std::setfill(data::abcdefg0123456789()[7]) << std::setw(8) << 10
            << std::setw(6) << std::setprecision(3) << 5.5;
        BOOST_CHECK(equal_strings(strm1.str(), strm2.str()));
    }
}
