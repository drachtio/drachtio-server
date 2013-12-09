/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   form_date_time.cpp
 * \author Andrey Semashev
 * \date   07.02.2009
 *
 * \brief  This header contains tests for the date and time formatters.
 */

#define BOOST_TEST_MODULE form_date_time

#include <memory>
#include <locale>
#include <string>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <algorithm>
#include <boost/function.hpp>
#include <boost/date_time.hpp>
#include <boost/test/included/unit_test.hpp>
#include <boost/log/attributes/constant.hpp>
#include <boost/log/attributes/attribute_set.hpp>
#include <boost/log/formatters/date_time.hpp>
#include <boost/log/formatters/stream.hpp>
#include <boost/log/utility/string_literal.hpp>
#include <boost/log/core/record.hpp>
#include "char_definitions.hpp"
#include "make_record.hpp"

namespace logging = boost::log;
namespace attrs = logging::attributes;
namespace fmt = logging::formatters;
namespace keywords = logging::keywords;

typedef boost::posix_time::ptime ptime;
typedef boost::gregorian::date gdate;
typedef boost::posix_time::time_period period;

namespace {

    template< typename CharT >
    struct date_time_formats;

#ifdef BOOST_LOG_USE_CHAR
    template< >
    struct date_time_formats< char > :
        public fmt::aux::date_time_format_defaults< char >
    {
        static string_literal_type date_format() { return logging::str_literal("%d/%m/%Y"); }
        static string_literal_type time_format() { return logging::str_literal("%H.%M.%S"); }
        static string_literal_type date_time_format() { return logging::str_literal("%d/%m/%Y %H.%M.%S"); }
        static string_literal_type time_duration_format() { return logging::str_literal("%+%H.%M.%S.%f"); }
        static string_literal_type time_period_format() { return logging::str_literal("[%begin% - %end%)"); }
    };
#endif // BOOST_LOG_USE_CHAR

#ifdef BOOST_LOG_USE_WCHAR_T
    template< >
    struct date_time_formats< wchar_t > :
        public fmt::aux::date_time_format_defaults< wchar_t >
    {
        static string_literal_type date_format() { return logging::str_literal(L"%d/%m/%Y"); }
        static string_literal_type time_format() { return logging::str_literal(L"%H.%M.%S"); }
        static string_literal_type date_time_format() { return logging::str_literal(L"%d/%m/%Y %H.%M.%S"); }
        static string_literal_type time_duration_format() { return logging::str_literal(L"%+%H.%M.%S.%f"); }
        static string_literal_type time_period_format() { return logging::str_literal(L"[%begin% - %end%)"); }
    };
#endif // BOOST_LOG_USE_WCHAR_T

} // namespace

// The test checks that date_time formatting work
BOOST_AUTO_TEST_CASE_TEMPLATE(date_time, CharT, char_types)
{
    typedef logging::basic_attribute_set< CharT > attr_set;
    typedef std::basic_string< CharT > string;
    typedef std::basic_ostringstream< CharT > osstream;
    typedef logging::basic_record< CharT > record;
    typedef boost::function< void (osstream&, record const&) > formatter;
    typedef test_data< CharT > data;
    typedef date_time_formats< CharT > formats;
    typedef boost::date_time::time_facet< ptime, CharT > facet;

    ptime t1(gdate(2009, 2, 7), ptime::time_duration_type(14, 40, 15));
    attrs::constant< ptime > attr1(t1);

    attr_set set1;
    set1[data::attr1()] = attr1;

    record rec = make_record(set1);
    rec.message() = data::some_test_string();

    // Check for various formats specification
    {
        osstream strm1;
        formatter f = fmt::date_time(data::attr1());
        f(strm1, rec);
        osstream strm2;
        strm2.imbue(std::locale(strm2.getloc(), new facet(formats::default_date_time_format().c_str())));
        strm2 << t1;
        BOOST_CHECK(equal_strings(strm1.str(), strm2.str()));
    }
    {
        osstream strm1;
        formatter f = fmt::date_time(data::attr1(), keywords::format = formats::date_time_format().c_str());
        f(strm1, rec);
        osstream strm2;
        strm2.imbue(std::locale(strm2.getloc(), new facet(formats::date_time_format().c_str())));
        strm2 << t1;
        BOOST_CHECK(equal_strings(strm1.str(), strm2.str()));
    }
}

// The test checks that date formatting work
BOOST_AUTO_TEST_CASE_TEMPLATE(date, CharT, char_types)
{
    typedef logging::basic_attribute_set< CharT > attr_set;
    typedef std::basic_string< CharT > string;
    typedef std::basic_ostringstream< CharT > osstream;
    typedef logging::basic_record< CharT > record;
    typedef boost::function< void (osstream&, record const&) > formatter;
    typedef test_data< CharT > data;
    typedef date_time_formats< CharT > formats;
    typedef boost::date_time::date_facet< gdate, CharT > facet;

    gdate d1(2009, 2, 7);
    attrs::constant< gdate > attr1(d1);

    attr_set set1;
    set1[data::attr1()] = attr1;

    record rec = make_record(set1);
    rec.message() = data::some_test_string();

    // Check for various formats specification
    {
        osstream strm1;
        formatter f = fmt::date(data::attr1());
        f(strm1, rec);
        osstream strm2;
        strm2.imbue(std::locale(strm2.getloc(), new facet(formats::default_date_format().c_str())));
        strm2 << d1;
        BOOST_CHECK(equal_strings(strm1.str(), strm2.str()));
    }
    {
        osstream strm1;
        formatter f = fmt::date(data::attr1(), keywords::format = formats::date_format().c_str());
        f(strm1, rec);
        osstream strm2;
        strm2.imbue(std::locale(strm2.getloc(), new facet(formats::date_format().c_str())));
        strm2 << d1;
        BOOST_CHECK(equal_strings(strm1.str(), strm2.str()));
    }
}

// The test checks that time_duration formatting work
BOOST_AUTO_TEST_CASE_TEMPLATE(time_duration, CharT, char_types)
{
    typedef logging::basic_attribute_set< CharT > attr_set;
    typedef std::basic_string< CharT > string;
    typedef std::basic_ostringstream< CharT > osstream;
    typedef logging::basic_record< CharT > record;
    typedef boost::function< void (osstream&, record const&) > formatter;
    typedef test_data< CharT > data;
    typedef date_time_formats< CharT > formats;
    typedef boost::date_time::time_facet< ptime, CharT > facet;

    ptime::time_duration_type t1(14, 40, 15);
    attrs::constant< ptime::time_duration_type > attr1(t1);

    attr_set set1;
    set1[data::attr1()] = attr1;

    record rec = make_record(set1);
    rec.message() = data::some_test_string();

    // Check for various formats specification
    {
        osstream strm1;
        formatter f = fmt::time_duration(data::attr1());
        f(strm1, rec);
        osstream strm2;
        strm2.imbue(std::locale(strm2.getloc(), new facet(formats::default_time_duration_format().c_str())));
        strm2 << t1;
        BOOST_CHECK(equal_strings(strm1.str(), strm2.str()));
    }
    {
        osstream strm1;
        formatter f = fmt::time_duration(data::attr1(), keywords::format = formats::time_duration_format().c_str());
        f(strm1, rec);
        osstream strm2;
        strm2.imbue(std::locale(strm2.getloc(), new facet(formats::time_duration_format().c_str())));
        strm2 << t1;
        BOOST_CHECK(equal_strings(strm1.str(), strm2.str()));
    }
}

// The test checks that time_period formatting work
BOOST_AUTO_TEST_CASE_TEMPLATE(time_period, CharT, char_types)
{
    typedef logging::basic_attribute_set< CharT > attr_set;
    typedef std::basic_string< CharT > string;
    typedef std::basic_ostringstream< CharT > osstream;
    typedef logging::basic_record< CharT > record;
    typedef boost::function< void (osstream&, record const&) > formatter;
    typedef test_data< CharT > data;
    typedef date_time_formats< CharT > formats;
    typedef boost::date_time::time_facet< ptime, CharT > facet;

    ptime t1(gdate(2009, 2, 7), ptime::time_duration_type(14, 40, 15));
    period p1(t1, ptime::time_duration_type(2, 3, 44));
    attrs::constant< period > attr1(p1);

    attr_set set1;
    set1[data::attr1()] = attr1;

    record rec = make_record(set1);
    rec.message() = data::some_test_string();

    // Check for various formats specification
    {
        osstream strm1;
        formatter f = fmt::time_period(data::attr1());
        f(strm1, rec);
        osstream strm2;
        strm2.imbue(std::locale(strm2.getloc(), new facet(formats::default_date_time_format().c_str())));
        strm2 << "[" << p1.begin() << " - " << p1.last() << "]";
        BOOST_CHECK(equal_strings(strm1.str(), strm2.str()));
    }
    {
        osstream strm1;
        formatter f = fmt::time_period(data::attr1(), keywords::format = formats::time_period_format().c_str());
        f(strm1, rec);
        osstream strm2;
        strm2.imbue(std::locale(strm2.getloc(), new facet(formats::default_date_time_format().c_str())));
        strm2 << "[" << p1.begin() << " - " << p1.end() << ")";
        BOOST_CHECK(equal_strings(strm1.str(), strm2.str()));
    }
    {
        osstream strm1;
        formatter f = fmt::time_period(
            data::attr1(),
            keywords::format = formats::time_period_format().c_str(),
            keywords::unit_format = formats::date_time_format().c_str());
        f(strm1, rec);
        osstream strm2;
        strm2.imbue(std::locale(strm2.getloc(), new facet(formats::date_time_format().c_str())));
        strm2 << "[" << p1.begin() << " - " << p1.end() << ")";
        BOOST_CHECK(equal_strings(strm1.str(), strm2.str()));
    }
}
