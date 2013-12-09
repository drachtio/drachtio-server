/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   basic_sink_backend.hpp
 * \author Andrey Semashev
 * \date   04.11.2007
 *
 * The header contains implementation of base classes for sink backends.
 */

#if (defined(_MSC_VER) && _MSC_VER > 1000)
#pragma once
#endif // _MSC_VER > 1000

#ifndef BOOST_LOG_SINKS_BASIC_SINK_BACKEND_HPP_INCLUDED_
#define BOOST_LOG_SINKS_BASIC_SINK_BACKEND_HPP_INCLUDED_

#include <string>
#include <boost/type_traits/is_same.hpp>
#include <boost/log/detail/prologue.hpp>
#include <boost/log/sinks/frontend_requirements.hpp>
#include <boost/log/core/record.hpp>
#include <boost/log/attributes/attribute_values_view.hpp>

namespace boost {

namespace BOOST_LOG_NAMESPACE {

namespace sinks {

/*!
 * \brief Base class for a logging sink backend
 *
 * The \c basic_sink_backend class template defines a number of types that
 * all sink backends are required to define. All sink backends have to derive from the class.
 */
template< typename CharT, typename FrontendRequirementsT >
struct basic_sink_backend
{
    //! Character type
    typedef CharT char_type;
    //! String type to be used as a message text holder
    typedef std::basic_string< char_type > string_type;
    //! Attribute values view type
    typedef basic_attribute_values_view< char_type > values_view_type;
    //! Log record type
    typedef basic_record< char_type > record_type;

    //! Frontend requirements tag
    typedef FrontendRequirementsT frontend_requirements;

    BOOST_LOG_DEFAULTED_FUNCTION(basic_sink_backend(), {})

    BOOST_LOG_DELETED_FUNCTION(basic_sink_backend(basic_sink_backend const&))
    BOOST_LOG_DELETED_FUNCTION(basic_sink_backend& operator= (basic_sink_backend const&))
};

/*!
 * \brief A base class for a logging sink backend with message formatting support
 *
 * The \c basic_formatting_sink_backend class template indicates to the frontend that
 * the backend requires logging record formatting.
 *
 * The class allows to request encoding conversion in case if the sink backend
 * requires the formatted string in some particular encoding (e.g. if underlying API
 * supports only narrow or wide characters). In order to perform conversion one
 * should specify the desired final character type in the \c TargetCharT template
 * parameter.
 */
template<
    typename CharT,
    typename TargetCharT = CharT,
    typename FrontendRequirementsT = synchronized_feeding
>
struct basic_formatting_sink_backend :
    public basic_sink_backend<
        CharT,
        typename combine_requirements< FrontendRequirementsT, formatted_records >::type
    >
{
private:
    typedef basic_sink_backend<
        CharT,
        typename combine_requirements< FrontendRequirementsT, formatted_records >::type
    > base_type;

public:
    //  Type imports from the base class
    typedef typename base_type::char_type char_type;
    typedef typename base_type::string_type string_type;
    typedef typename base_type::values_view_type values_view_type;
    typedef typename base_type::frontend_requirements frontend_requirements;
    typedef typename base_type::record_type record_type;

    //! Target character type
    typedef TargetCharT target_char_type;
    //! Target string type
    typedef std::basic_string< target_char_type > target_string_type;
};

} // namespace sinks

} // namespace log

} // namespace boost

#endif // BOOST_LOG_SINKS_BASIC_SINK_BACKEND_HPP_INCLUDED_
