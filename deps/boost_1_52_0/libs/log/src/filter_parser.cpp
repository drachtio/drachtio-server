/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   filter_parser.cpp
 * \author Andrey Semashev
 * \date   31.03.2008
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/libs/log/doc/log.html.
 */

#ifndef BOOST_LOG_NO_SETTINGS_PARSERS_SUPPORT

#include <map>
#include <stack>
#include <string>
#include <sstream>
#include <stdexcept>

#if !defined(BOOST_LOG_NO_THREADS) && !defined(BOOST_SPIRIT_THREADSAFE)
#define BOOST_SPIRIT_THREADSAFE
#endif // !defined(BOOST_LOG_NO_THREADS) && !defined(BOOST_SPIRIT_THREADSAFE)

#include <boost/bind.hpp>
#include <boost/none.hpp>
#include <boost/optional.hpp>
#include <boost/utility/addressof.hpp>
#include <boost/utility/in_place_factory.hpp>
#include <boost/spirit/include/classic_core.hpp>
#include <boost/spirit/include/classic_confix.hpp>
#include <boost/spirit/include/classic_escape_char.hpp>
#include <boost/log/core/core.hpp>
#include <boost/log/detail/singleton.hpp>
#include <boost/log/detail/functional.hpp>
#include <boost/log/exceptions.hpp>
#include <boost/log/utility/init/filter_parser.hpp>
#if !defined(BOOST_LOG_NO_THREADS)
#include <boost/log/detail/locks.hpp>
#include <boost/log/detail/light_rw_mutex.hpp>
#endif // !defined(BOOST_LOG_NO_THREADS)
#include "parser_utils.hpp"
#include "default_filter_factory.hpp"

namespace bsc = boost::spirit::classic;

namespace boost {

namespace BOOST_LOG_NAMESPACE {

BOOST_LOG_ANONYMOUS_NAMESPACE {

//! Filter factories repository
template< typename CharT >
struct filters_repository :
    public log::aux::lazy_singleton< filters_repository< CharT > >
{
    typedef CharT char_type;
    typedef log::aux::lazy_singleton< filters_repository< char_type > > base_type;
    typedef std::basic_string< char_type > string_type;
    typedef filter_factory< char_type > filter_factory_type;
    typedef std::map< string_type, shared_ptr< filter_factory_type > > factories_map;

#if !defined(BOOST_LOG_BROKEN_FRIEND_TEMPLATE_INSTANTIATIONS)
    friend class log::aux::lazy_singleton< filters_repository< char_type > >;
#else
    friend class base_type;
#endif

#if !defined(BOOST_LOG_NO_THREADS)
    //! Synchronization mutex
    mutable log::aux::light_rw_mutex m_Mutex;
#endif
    //! The map of filter factories
    factories_map m_Map;
    //! Default factory
    mutable aux::default_filter_factory< char_type > m_DefaultFactory;

    //! The method returns the filter factory for the specified attribute name
    filter_factory_type& get_factory(string_type const& name) const
    {
        typename factories_map::const_iterator it = m_Map.find(name);
        if (it != m_Map.end())
            return *it->second;
        else
            return m_DefaultFactory;
    }

private:
    filters_repository() {}
};

//! Filter parsing grammar
template< typename CharT >
struct filter_grammar :
    public bsc::grammar< filter_grammar< CharT > >
{
    typedef CharT char_type;
    typedef std::basic_string< char_type > string_type;
    typedef typename basic_core< char_type >::filter_type filter_type;
    typedef log::aux::char_constants< char_type > constants;
    typedef filter_grammar< char_type > filter_grammar_type;
    typedef filter_factory< char_type > filter_factory_type;

    typedef filter_type (filter_factory_type::*comparison_relation_handler_t)(string_type const&, string_type const&);

    template< typename ScannerT >
    struct definition;

    //! Parsed attribute name
    mutable optional< string_type > m_AttributeName;
    //! The second operand of a relation
    mutable optional< string_type > m_Operand;
    //! The custom relation string
    mutable string_type m_CustomRelation;

    //! Filter subexpressions as they are parsed
    mutable std::stack< filter_type > m_Subexpressions;

    //! Reference to the filter being constructed
    filter_type& m_Filter;

    //! Constructor
    explicit filter_grammar(filter_type& f) : m_Filter(f) {}

    //! The method finalizes filter construction by flushing its internal data that may not have been put into the filter
    void flush() const
    {
        if (!m_Subexpressions.empty())
            m_Filter.swap(m_Subexpressions.top());
    }

    //! The operand string handler
    void on_operand(const char_type* begin, const char_type* end) const
    {
        // An attribute name should have been parsed at this time
        if (!m_AttributeName)
            BOOST_LOG_THROW_DESCR(parse_error, "Invalid filter definition: operand is not expected");

        m_Operand = boost::in_place(begin, end);
    }

    //! The quoted string handler
    void on_quoted_string(const char_type* begin, const char_type* end) const
    {
        // An attribute name should have been parsed at this time
        if (!m_AttributeName)
            BOOST_LOG_THROW_DESCR(parse_error, "Invalid filter definition: quoted string operand is not expected");

        // Cut off the quotes
        string_type str(++begin, --end);

        // Translate escape sequences
        constants::translate_escape_sequences(str);
        m_Operand = str;
    }
    //! The attribute name handler
    void on_attribute(const char_type* begin, const char_type* end) const
    {
        // In case if previous subexpression consisted only
        // from attribute name, like in "%Attribute1% & %Attribute2% > 1"
        make_has_attr();

        // Cut off the '%'
        m_AttributeName = boost::in_place(++begin, --end);
    }

    //! The negation operation handler
    void on_negation(const char_type* begin, const char_type* end) const
    {
        make_has_attr();
        if (!m_Subexpressions.empty())
        {
            m_Subexpressions.top() = !log::filters::wrap< char_type >(m_Subexpressions.top());
        }
        else
        {
            // This would happen if a filter consists of a single '!'
            BOOST_LOG_THROW_DESCR(parse_error, "Filter parsing error:"
                " a negation operator applied to nothingness");
        }
    }
    //! The comparison relation handler
    void on_comparison_relation(const char_type* begin, const char_type* end, comparison_relation_handler_t method) const
    {
        if (!!m_AttributeName && !!m_Operand)
        {
            filters_repository< char_type > const& repo = filters_repository< char_type >::get();
            filter_factory_type& factory = repo.get_factory(m_AttributeName.get());

            m_Subexpressions.push((factory.*method)(m_AttributeName.get(), m_Operand.get()));

            m_AttributeName = none;
            m_Operand = none;
        }
        else
        {
            // This should never happen
            BOOST_LOG_THROW_DESCR(parse_error, "Filter parser internal error:"
                " the attribute name or subexpression operand is not set while trying to construct a subexpression");
        }
    }

    //! The method saves the relation word into an internal string
    void set_custom_relation(const char_type* begin, const char_type* end) const
    {
        m_CustomRelation.assign(begin, end);
    }

    //! The custom relation handler for string operands
    void on_custom_relation(const char_type* begin, const char_type* end) const
    {
        if (!!m_AttributeName && !!m_Operand && !m_CustomRelation.empty())
        {
            filters_repository< char_type > const& repo = filters_repository< char_type >::get();
            filter_factory_type& factory = repo.get_factory(m_AttributeName.get());

            m_Subexpressions.push(factory.on_custom_relation(m_AttributeName.get(), m_CustomRelation, m_Operand.get()));

            m_AttributeName = none;
            m_Operand = none;
            m_CustomRelation.clear();
        }
        else
        {
            // This should never happen
            BOOST_LOG_THROW_DESCR(parse_error, "Filter parser internal error:"
                " the attribute name or subexpression operand is not set while trying to construct a subexpression");
        }
    }

    //! The boolean operation handler
    template< template< typename, typename > class OperationT >
    void on_operation(const char_type* begin, const char_type* end) const
    {
        if (!m_Subexpressions.empty())
        {
            filter_type right = m_Subexpressions.top();
            m_Subexpressions.pop();
            if (!m_Subexpressions.empty())
            {
                filter_type const& left = m_Subexpressions.top();
                typedef log::filters::flt_wrap< char_type, filter_type > wrap_t;
                m_Subexpressions.top() = OperationT< wrap_t, wrap_t >(wrap_t(left), wrap_t(right));
                return;
            }
        }

        // This should never happen
        BOOST_LOG_THROW_DESCR(parse_error, "Filter parser internal error:"
            " the subexpression is not set while trying to construct a filter");
    }

    //! The function is called when a full expression have finished parsing
    void on_expression_finished(const char_type* begin, const char_type* end) const
    {
        make_has_attr();
    }

private:
    //  Assignment and copying are prohibited
    filter_grammar(filter_grammar const&);
    filter_grammar& operator= (filter_grammar const&);

    //! The function converts the parsed attribute name into a has_attr filter
    void make_has_attr() const
    {
        if (!!m_AttributeName)
        {
            filters_repository< char_type > const& repo = filters_repository< char_type >::get();
            filter_factory_type& factory = repo.get_factory(m_AttributeName.get());
            m_Subexpressions.push(factory.on_exists_test(m_AttributeName.get()));
            m_AttributeName = none;
        }
    }
};

//! Grammar definition
template< typename CharT >
template< typename ScannerT >
struct filter_grammar< CharT >::definition
{
    //! Boost.Spirit rule type
    typedef bsc::rule< ScannerT > rule_type;

    //! A simple mem_fn-like wrapper (a workaround for MSVC 7.1)
    struct handler
    {
        typedef void result_type;
        typedef void (filter_grammar_type::*fun_type)(const char_type*, const char_type*) const;
        handler(fun_type pf) : m_fun(pf) {}

        void operator() (filter_grammar_type const* p, const char_type* b, const char_type* e) const
        {
            (p->*m_fun)(b, e);
        }

    private:
        fun_type m_fun;
    };

    //! A parser for an attribute name in a single relation
    rule_type attr_name;
    //! A parser for an operand in a single relation
    rule_type operand;
    //! A parser for a single relation that consists of two operands and an operation between them
    rule_type relation;
    //! A parser for a custom relation word
    rule_type custom_relation;
    //! A parser for a term, which can be a relation, an expression in parenthesis or a negation thereof
    rule_type term;
    //! A parser for the complete filter expression that consists of one or several terms with boolean operations between them
    rule_type expression;

    //! Constructor
    definition(filter_grammar_type const& gram)
    {
        const filter_grammar_type* g = boost::addressof(gram);

        // MSVC 7.1 goes wild for some reason if we try to use bind or mem_fn directly
        // on some of these functions. The simple wrapper helps the compiler to deduce types correctly.
        handler on_string = &filter_grammar_type::on_quoted_string,
            on_oper = &filter_grammar_type::on_operand,
            on_attr = &filter_grammar_type::on_attribute;

        attr_name = bsc::lexeme_d[
            // An attribute name in form %name%
            bsc::confix_p(constants::char_percent, *bsc::print_p, constants::char_percent)
                [boost::bind(on_attr, g, _1, _2)]
        ];

        operand = bsc::lexeme_d[
            // A quoted string with C-style escape sequences support
            bsc::confix_p(constants::char_quote, *bsc::c_escape_ch_p, constants::char_quote)
                [boost::bind(on_string, g, _1, _2)] |
            // A single word, enclosed with white spaces. It cannot contain parenthesis, since is is used by the filter parser.
            (+(bsc::graph_p - bsc::ch_p(constants::char_paren_bracket_left) - constants::char_paren_bracket_right))
                [boost::bind(on_oper, g, _1, _2)]
        ];

        // Custom relation is a keyword that may contain either alphanumeric characters or an underscore
        custom_relation = bsc::lexeme_d[ +(bsc::alnum_p | bsc::ch_p(constants::char_underline)) ]
            [boost::bind(&filter_grammar_type::set_custom_relation, g, _1, _2)];

        relation = attr_name || // The relation may be as simple as a sole attribute name, in which case the filter checks for the attribute value presence
        (
            (bsc::str_p(constants::not_equal_keyword()) >> operand)
                [boost::bind(&filter_grammar_type::on_comparison_relation, g, _1, _2, &filter_factory_type::on_inequality_relation)] |
            (bsc::str_p(constants::greater_or_equal_keyword()) >> operand)
                [boost::bind(&filter_grammar_type::on_comparison_relation, g, _1, _2, &filter_factory_type::on_greater_or_equal_relation)] |
            (bsc::str_p(constants::less_or_equal_keyword()) >> operand)
                [boost::bind(&filter_grammar_type::on_comparison_relation, g, _1, _2, &filter_factory_type::on_less_or_equal_relation)] |
            (constants::char_equal >> operand)
                [boost::bind(&filter_grammar_type::on_comparison_relation, g, _1, _2, &filter_factory_type::on_equality_relation)] |
            (constants::char_greater >> operand)
                [boost::bind(&filter_grammar_type::on_comparison_relation, g, _1, _2, &filter_factory_type::on_greater_relation)] |
            (constants::char_less >> operand)
                [boost::bind(&filter_grammar_type::on_comparison_relation, g, _1, _2, &filter_factory_type::on_less_relation)] |
            (custom_relation >> operand)
                [boost::bind(&filter_grammar_type::on_custom_relation, g, _1, _2)]
        );

        handler on_neg = &filter_grammar_type::on_negation;

        term =
        (
            (bsc::ch_p(constants::char_paren_bracket_left) >> expression >> constants::char_paren_bracket_right) |
            ((bsc::str_p(constants::not_keyword()) | constants::char_exclamation) >> term)[boost::bind(on_neg, g, _1, _2)] |
            relation
        );

        handler on_and = &filter_grammar_type::BOOST_NESTED_TEMPLATE on_operation< log::filters::flt_and >,
            on_or = &filter_grammar_type::BOOST_NESTED_TEMPLATE on_operation< log::filters::flt_or >,
            on_finished = &filter_grammar_type::on_expression_finished;

        expression =
        (
            term >>
            *(
                ((bsc::str_p(constants::and_keyword()) | constants::char_and) >> term)
                    [boost::bind(on_and, g, _1, _2)] |
                ((bsc::str_p(constants::or_keyword()) | constants::char_or) >> term)
                    [boost::bind(on_or, g, _1, _2)]
            )
        )[boost::bind(on_finished, g, _1, _2)];
    }

    //! Accessor for the filter rule
    rule_type const& start() const { return expression; }
};

} // namespace

//! The function registers a filter factory object for the specified attribute name
template< typename CharT >
void register_filter_factory(const CharT* attr_name, shared_ptr< filter_factory< CharT > > const& factory)
{
    std::basic_string< CharT > name(attr_name);
    filters_repository< CharT >& repo = filters_repository< CharT >::get();

    BOOST_LOG_EXPR_IF_MT(log::aux::exclusive_lock_guard< log::aux::light_rw_mutex > _(repo.m_Mutex);)
    repo.m_Map[name] = factory;
}

//! The function parses a filter from the string
template< typename CharT >
#ifndef BOOST_LOG_BROKEN_TEMPLATE_DEFINITION_MATCHING
typename basic_core< CharT >::filter_type
#else
boost::log::aux::light_function1< bool, basic_attribute_values_view< CharT > const& >
#endif
parse_filter(const CharT* begin, const CharT* end)
{
    typedef CharT char_type;
    typedef typename basic_core< char_type >::filter_type filter_type;

    BOOST_LOG_EXPR_IF_MT(filters_repository< CharT >& repo = filters_repository< CharT >::get();)
    BOOST_LOG_EXPR_IF_MT(log::aux::shared_lock_guard< log::aux::light_rw_mutex > _(repo.m_Mutex);)

    filter_type filt;
    filter_grammar< char_type > gram(filt);
    bsc::parse_info< const char_type* > result = bsc::parse(begin, end, gram, bsc::space_p);
    if (!result.full)
    {
        std::ostringstream strm;
        strm << "Could not parse the filter, parsing stopped at position "
            << result.stop - begin;
        BOOST_LOG_THROW_DESCR(parse_error, strm.str());
    }
    gram.flush();

    return filt;
}

#ifdef BOOST_LOG_USE_CHAR

template BOOST_LOG_SETUP_EXPORT
void register_filter_factory(const char* name, shared_ptr< filter_factory< char > > const& factory);

template BOOST_LOG_SETUP_EXPORT
#ifndef BOOST_LOG_BROKEN_TEMPLATE_DEFINITION_MATCHING
basic_core< char >::filter_type
#else
boost::log::aux::light_function1< bool, basic_attribute_values_view< char > const& >
#endif // BOOST_LOG_BROKEN_TEMPLATE_DEFINITION_MATCHING
parse_filter< char >(const char* begin, const char* end);

#endif // BOOST_LOG_USE_CHAR

#ifdef BOOST_LOG_USE_WCHAR_T

template BOOST_LOG_SETUP_EXPORT
void register_filter_factory(const wchar_t* name, shared_ptr< filter_factory< wchar_t > > const& factory);

template BOOST_LOG_SETUP_EXPORT
#ifndef BOOST_LOG_BROKEN_TEMPLATE_DEFINITION_MATCHING
basic_core< wchar_t >::filter_type
#else
boost::log::aux::light_function1< bool, basic_attribute_values_view< wchar_t > const& >
#endif // BOOST_LOG_BROKEN_TEMPLATE_DEFINITION_MATCHING
parse_filter< wchar_t >(const wchar_t* begin, const wchar_t* end);

#endif // BOOST_LOG_USE_WCHAR_T

} // namespace log

} // namespace boost

#endif // BOOST_LOG_NO_SETTINGS_PARSERS_SUPPORT
