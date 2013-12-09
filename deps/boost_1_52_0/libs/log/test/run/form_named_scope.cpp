/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   form_named_scope.cpp
 * \author Andrey Semashev
 * \date   07.02.2009
 *
 * \brief  This header contains tests for the \c named_scope formatter.
 */

#define BOOST_TEST_MODULE form_named_scope

#include <string>
#include <sstream>
#include <boost/function.hpp>
#include <boost/preprocessor/cat.hpp>
#include <boost/test/included/unit_test.hpp>
#include <boost/log/attributes/constant.hpp>
#include <boost/log/attributes/attribute_set.hpp>
#include <boost/log/attributes/named_scope.hpp>
#include <boost/log/formatters/named_scope.hpp>
#include <boost/log/formatters/stream.hpp>
#include <boost/log/utility/string_literal.hpp>
#include <boost/log/core/record.hpp>
#include "char_definitions.hpp"
#include "make_record.hpp"

namespace logging = boost::log;
namespace attrs = logging::attributes;
namespace fmt = logging::formatters;
namespace keywords = logging::keywords;

namespace {

    template< typename CharT >
    struct named_scope_test_data;

#ifdef BOOST_LOG_USE_CHAR
    template< >
    struct named_scope_test_data< char > :
        public test_data< char >
    {
        static logging::string_literal scope1() { return logging::str_literal("scope1"); }
        static logging::string_literal scope2() { return logging::str_literal("scope2"); }
        static logging::string_literal file() { return logging::str_literal(__FILE__); }
        static logging::string_literal delimiter1() { return logging::str_literal("|"); }
    };
#endif // BOOST_LOG_USE_CHAR

#ifdef BOOST_LOG_USE_WCHAR_T
    template< >
    struct named_scope_test_data< wchar_t > :
        public test_data< wchar_t >
    {
        static logging::wstring_literal scope1() { return logging::str_literal(L"scope1"); }
        static logging::wstring_literal scope2() { return logging::str_literal(L"scope2"); }
        static logging::wstring_literal file() { return logging::str_literal(BOOST_PP_CAT(L, __FILE__)); }
        static logging::wstring_literal delimiter1() { return logging::str_literal(L"|"); }
    };
#endif // BOOST_LOG_USE_WCHAR_T

} // namespace

// The test checks that named scopes stack formatting works
BOOST_AUTO_TEST_CASE_TEMPLATE(scopes_formatting, CharT, char_types)
{
    typedef attrs::basic_named_scope< CharT > named_scope;
    typedef typename named_scope::sentry sentry;
    typedef attrs::basic_named_scope_list< CharT > scopes;
    typedef attrs::basic_named_scope_entry< CharT > scope;

    typedef logging::basic_attribute_set< CharT > attr_set;
    typedef std::basic_string< CharT > string;
    typedef std::basic_ostringstream< CharT > osstream;
    typedef logging::basic_record< CharT > record;
    typedef boost::function< void (osstream&, record const&) > formatter;
    typedef named_scope_test_data< CharT > data;

    named_scope attr;

    // First scope
    const unsigned int line1 = __LINE__;
    sentry scope1(data::scope1(), data::file(), line1);
    const unsigned int line2 = __LINE__;
    sentry scope2(data::scope2(), data::file(), line2);

    attr_set set1;
    set1[data::attr1()] = attr;

    record rec = make_record(set1);
    rec.message() = data::some_test_string();

    // Default format
    {
        osstream strm1;
        formatter f = fmt::stream << fmt::named_scope(data::attr1());
        f(strm1, rec);
        osstream strm2;
        strm2 << data::scope1() << "->" << data::scope2();
        BOOST_CHECK(equal_strings(strm1.str(), strm2.str()));
    }
    // Different delimiter
    {
        osstream strm1;
        formatter f = fmt::stream << fmt::named_scope(data::attr1(), keywords::delimiter = data::delimiter1().c_str());
        f(strm1, rec);
        osstream strm2;
        strm2 << data::scope1() << "|" << data::scope2();
        BOOST_CHECK(equal_strings(strm1.str(), strm2.str()));
    }
    // Different direction
    {
        osstream strm1;
        formatter f = fmt::stream << fmt::named_scope(data::attr1(), keywords::iteration = fmt::reverse);
        f(strm1, rec);
        osstream strm2;
        strm2 << data::scope2() << "<-" << data::scope1();
        BOOST_CHECK(equal_strings(strm1.str(), strm2.str()));
    }
    {
        osstream strm1;
        formatter f = fmt::stream << fmt::named_scope(data::attr1(),
            keywords::delimiter = data::delimiter1().c_str(),
            keywords::iteration = fmt::reverse);
        f(strm1, rec);
        osstream strm2;
        strm2 << data::scope2() << "|" << data::scope1();
        BOOST_CHECK(equal_strings(strm1.str(), strm2.str()));
    }
    // Limiting the number of scopes
    {
        osstream strm1;
        formatter f = fmt::stream << fmt::named_scope(data::attr1(), keywords::depth = 1);
        f(strm1, rec);
        osstream strm2;
        strm2 << "...->" << data::scope2();
        BOOST_CHECK(equal_strings(strm1.str(), strm2.str()));
    }
    {
        osstream strm1;
        formatter f = fmt::stream << fmt::named_scope(data::attr1(),
            keywords::depth = 1,
            keywords::iteration = fmt::reverse);
        f(strm1, rec);
        osstream strm2;
        strm2 << data::scope2() << "<-...";
        BOOST_CHECK(equal_strings(strm1.str(), strm2.str()));
    }
    {
        osstream strm1;
        formatter f = fmt::stream << fmt::named_scope(data::attr1(),
            keywords::delimiter = data::delimiter1().c_str(),
            keywords::depth = 1);
        f(strm1, rec);
        osstream strm2;
        strm2 << "...|" << data::scope2();
        BOOST_CHECK(equal_strings(strm1.str(), strm2.str()));
    }
    {
        osstream strm1;
        formatter f = fmt::stream << fmt::named_scope(data::attr1(),
            keywords::delimiter = data::delimiter1().c_str(),
            keywords::depth = 1,
            keywords::iteration = fmt::reverse);
        f(strm1, rec);
        osstream strm2;
        strm2 << data::scope2() << "|...";
        BOOST_CHECK(equal_strings(strm1.str(), strm2.str()));
    }
}
