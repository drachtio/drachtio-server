/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   init_from_stream.cpp
 * \author Andrey Semashev
 * \date   22.03.2008
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/libs/log/doc/log.html.
 */

#ifndef BOOST_LOG_NO_SETTINGS_PARSERS_SUPPORT

#include <string>
#include <iostream>
#include <locale>
#include <memory>
#include <stdexcept>
#include <iterator>

#if !defined(BOOST_LOG_NO_THREADS) && !defined(BOOST_SPIRIT_THREADSAFE)
#define BOOST_SPIRIT_THREADSAFE
#endif // !defined(BOOST_LOG_NO_THREADS) && !defined(BOOST_SPIRIT_THREADSAFE)

#include <boost/ref.hpp>
#include <boost/bind.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/spirit/include/classic_core.hpp>
#include <boost/spirit/include/classic_assign_actor.hpp>
#include <boost/spirit/include/classic_increment_actor.hpp>
#include <boost/spirit/include/classic_escape_char.hpp>
#include <boost/spirit/include/classic_confix.hpp>
#include <boost/spirit/include/classic_directives.hpp>
#include <boost/spirit/include/classic_multi_pass.hpp>
#include <boost/log/detail/prologue.hpp>
#include <boost/log/detail/code_conversion.hpp>
#include <boost/log/exceptions.hpp>
#include <boost/log/utility/init/from_settings.hpp>
#include "parser_utils.hpp"

namespace bsc = boost::spirit::classic;

namespace boost {

namespace BOOST_LOG_NAMESPACE {

BOOST_LOG_ANONYMOUS_NAMESPACE {

//! Settings file parsing grammar
template< typename CharT >
struct settings_file_grammar :
    public spirit::classic::grammar< settings_file_grammar< CharT > >
{
    typedef CharT char_type;
    typedef settings_file_grammar< char_type > settings_file_grammar_type;
    typedef std::basic_string< char_type > string_type;
    typedef log::aux::char_constants< char_type > constants;
    typedef basic_settings< char_type > settings_type;

    template< typename ScannerT >
    struct definition;

    //! Current section name
    string_type m_SectionName;
    //! Current parameter name
    string_type m_ParameterName;
    //! Settings instance
    settings_type& m_Settings;
    //! Locale from the source stream
    std::locale m_Locale;
    //! Current line number
    std::size_t m_LineCounter;

    //! Constructor
    explicit settings_file_grammar(settings_type& setts, std::locale const& loc) :
        m_Settings(setts),
        m_Locale(loc),
        m_LineCounter(1)
    {
    }

    //! The method sets the parsed section name
    template< typename IteratorT >
    void set_section_name(IteratorT begin, IteratorT end)
    {
        string_type sec(begin, end);
        // Cut off the square brackets
        m_SectionName = sec.substr(1, sec.size() - 2);
        algorithm::trim(m_SectionName, m_Locale);
        if (m_SectionName.empty())
        {
            // The section starter is broken
            BOOST_LOG_THROW_DESCR_PARAMS(parse_error, "The section header is invalid.", (m_LineCounter));
        }
    }

    //! The method sets the parsed parameter name
    template< typename IteratorT >
    void set_parameter_name(IteratorT begin, IteratorT end)
    {
        if (m_SectionName.empty())
        {
            // The parameter encountered before any section starter
            BOOST_LOG_THROW_DESCR_PARAMS(parse_error, "Parameters are only allowed within sections.", (m_LineCounter));
        }

        m_ParameterName.assign(begin, end);
    }

    //! The method sets the parsed parameter value (non-quoted)
    template< typename IteratorT >
    void set_parameter_value(IteratorT begin, IteratorT end)
    {
        string_type val(begin, end);
        m_Settings[m_SectionName][m_ParameterName] = val;
        m_ParameterName.clear();
    }

    //! The method sets the parsed parameter value (quoted)
    template< typename IteratorT >
    void set_parameter_quoted_value(IteratorT begin, IteratorT end)
    {
        string_type val(begin, end);
        // Cut off the quotes
        val = val.substr(1, val.size() - 2);
        constants::translate_escape_sequences(val);
        m_Settings[m_SectionName][m_ParameterName] = val;
        m_ParameterName.clear();
    }

private:
    //  Assignment and copying are prohibited
    settings_file_grammar(settings_file_grammar const&);
    settings_file_grammar& operator= (settings_file_grammar const&);
};

//! Grammar definition
template< typename CharT >
template< typename ScannerT >
struct settings_file_grammar< CharT >::definition
{
    //! Character iterator type
    typedef typename ScannerT::iterator_t iterator_type;

    //! Boost.Spirit rule type
    typedef spirit::classic::rule< ScannerT > rule_type;

    //! A parser for a comment
    rule_type comment;
    //! A parser for a section name
    rule_type section_name;
    //! A parser for a parameter name and value
    rule_type parameter;
    //! A parser for a single line
    rule_type line;
    //! A parser for the whole settings document
    rule_type document;

    //! Constructor
    definition(settings_file_grammar_type const& gram)
    {
        settings_file_grammar_type* g =
            const_cast< settings_file_grammar_type* >(boost::addressof(gram));

        comment = bsc::ch_p(constants::char_comment) >> *(bsc::anychar_p - bsc::eol_p);

        section_name =
            bsc::confix_p(
                constants::char_section_bracket_left,
                +bsc::graph_p,
                constants::char_section_bracket_right
            )[boost::bind(&settings_file_grammar_type::BOOST_NESTED_TEMPLATE set_section_name< iterator_type >, g, _1, _2)] >>
            !comment;

        parameter =
            // Parameter name
            (bsc::alpha_p >> *(bsc::graph_p - constants::char_equal))[boost::bind(&settings_file_grammar_type::BOOST_NESTED_TEMPLATE set_parameter_name< iterator_type >, g, _1, _2)] >>
            constants::char_equal >>
            // Parameter value
            (
                bsc::confix_p(
                    constants::char_quote,
                    *bsc::c_escape_ch_p,
                    constants::char_quote
                )[boost::bind(&settings_file_grammar_type::BOOST_NESTED_TEMPLATE set_parameter_quoted_value< iterator_type >, g, _1, _2)] |
                (+bsc::graph_p)[boost::bind(&settings_file_grammar_type::BOOST_NESTED_TEMPLATE set_parameter_value< iterator_type >, g, _1, _2)]
            ) >>
            !comment;

        line =
            (
                comment |
                section_name |
                parameter
            ) >>
            *(bsc::eol_p[bsc::increment_a(g->m_LineCounter)]);

        document = *(bsc::eol_p[bsc::increment_a(g->m_LineCounter)]) >> *line;
    }

    //! Accessor for the filter rule
    rule_type const& start() const { return document; }
};

} // namespace

//! The function initializes the logging library from a stream containing logging settings
template< typename CharT >
void init_from_stream(std::basic_istream< CharT >& strm)
{
    typedef CharT char_type;
    typedef std::basic_string< char_type > string_type;
    typedef settings_file_grammar< char_type > settings_file_grammar_type;
    typedef basic_settings< char_type > settings_type;

    // Engage parsing
    typedef std::istreambuf_iterator< char_type > stream_iterator_t;
    typedef bsc::multi_pass< stream_iterator_t > iterator_t;
    typedef bsc::skip_parser_iteration_policy< bsc::blank_parser > iter_policy_t;
    typedef bsc::scanner_policies< iter_policy_t > scanner_policies_t;
    typedef bsc::scanner< iterator_t, scanner_policies_t > scanner_t;

    iter_policy_t iter_policy = iter_policy_t(bsc::blank_p);
    scanner_policies_t policies = scanner_policies_t(iter_policy);
    iterator_t first = iterator_t(stream_iterator_t(strm));
    iterator_t last = iterator_t(stream_iterator_t());
    scanner_t scanner(first, last, policies);

    settings_type setts;
    settings_file_grammar_type gram(setts, strm.getloc());

    bsc::match< > m = gram.parse(scanner);
    if (!m || first != last)
    {
        BOOST_LOG_THROW_DESCR_PARAMS(parse_error, "Could not parse settings from stream.", (gram.m_LineCounter));
    }

    // Pass on the parsed settings to the initialization routine
    init_from_settings(setts);
}


#ifdef BOOST_LOG_USE_CHAR
template BOOST_LOG_SETUP_EXPORT void init_from_stream< char >(std::basic_istream< char >& strm);
#endif
#ifdef BOOST_LOG_USE_WCHAR_T
template BOOST_LOG_SETUP_EXPORT void init_from_stream< wchar_t >(std::basic_istream< wchar_t >& strm);
#endif

} // namespace log

} // namespace boost

#endif // BOOST_LOG_NO_SETTINGS_PARSERS_SUPPORT
