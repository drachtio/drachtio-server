/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   form_format.cpp
 * \author Andrey Semashev
 * \date   07.02.2009
 *
 * \brief  This header contains tests for the Boost.Format-style formatting.
 */

#define BOOST_TEST_MODULE form_format

#include <string>
#include <sstream>
#include <boost/function.hpp>
#include <boost/test/included/unit_test.hpp>
#include <boost/log/attributes/constant.hpp>
#include <boost/log/attributes/attribute_set.hpp>
#include <boost/log/formatters/attr.hpp>
#include <boost/log/formatters/format.hpp>
#include <boost/log/core/record.hpp>
#include "char_definitions.hpp"
#include "make_record.hpp"

namespace logging = boost::log;
namespace attrs = logging::attributes;
namespace fmt = logging::formatters;

namespace {

    template< typename >
    struct format_test_data;

#ifdef BOOST_LOG_USE_CHAR
    template< >
    struct format_test_data< char > :
        public test_data< char >
    {
        static const char* format1() { return "%1%, %2%"; }
    };
#endif // BOOST_LOG_USE_CHAR

#ifdef BOOST_LOG_USE_WCHAR_T
    template< >
    struct format_test_data< wchar_t > :
        public test_data< wchar_t >
    {
        static const wchar_t* format1() { return L"%1%, %2%"; }
    };
#endif // BOOST_LOG_USE_WCHAR_T

} // namespace

// The test checks that Boost.Format formatting works
BOOST_AUTO_TEST_CASE_TEMPLATE(formatting, CharT, char_types)
{
    typedef logging::basic_attribute_set< CharT > attr_set;
    typedef std::basic_string< CharT > string;
    typedef std::basic_ostringstream< CharT > osstream;
    typedef logging::basic_record< CharT > record;
    typedef boost::function< void (osstream&, record const&) > formatter;
    typedef format_test_data< CharT > data;

    attrs::constant< int > attr1(10);
    attrs::constant< double > attr2(5.5);

    attr_set set1;
    set1[data::attr1()] = attr1;
    set1[data::attr2()] = attr2;

    record rec = make_record(set1);
    rec.message() = data::some_test_string();

    {
        osstream strm1;
        formatter f = fmt::format(data::format1()) % fmt::attr< int >(data::attr1()) % fmt::attr< double >(data::attr2());
        f(strm1, rec);
        osstream strm2;
        strm2 << 10 << ", " << 5.5;
        BOOST_CHECK(equal_strings(strm1.str(), strm2.str()));
    }
}
