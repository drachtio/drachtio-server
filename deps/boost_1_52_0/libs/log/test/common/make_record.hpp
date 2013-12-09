/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   make_record.hpp
 * \author Andrey Semashev
 * \date   18.03.2009
 *
 * \brief  This header contains a helper function make_record that creates a log record with the specified attributes.
 */

#ifndef BOOST_LOG_TESTS_MAKE_RECORD_HPP_INCLUDED_
#define BOOST_LOG_TESTS_MAKE_RECORD_HPP_INCLUDED_

#include <boost/assert.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/log/core/core.hpp>
#include <boost/log/attributes/attribute_set.hpp>
#include <boost/log/detail/singleton.hpp>
#include "test_sink.hpp"

namespace aux {

// This singleton sink guarantees that all records will pass filtering
template< typename CharT >
class make_record_sink_holder :
    public boost::log::aux::singleton<
        make_record_sink_holder< CharT >,
        boost::shared_ptr< typename boost::log::basic_core< CharT >::sink_type >
    >
{
    typedef typename boost::log::basic_core< CharT > core_type;
    typedef typename core_type::sink_type sink_type;
    typedef boost::log::aux::singleton<
        make_record_sink_holder< CharT >,
        boost::shared_ptr< sink_type >
    > base_type;

public:
    //! Initializes the singleton instance
    static void init_instance()
    {
        boost::shared_ptr< sink_type > p(new test_sink< CharT >());
        core_type::get()->add_sink(p);
        base_type::get_instance() = p;
    }
};

} // namespace aux

template< typename CharT >
inline typename boost::log::basic_core< CharT >::record_type make_record(
    boost::log::basic_attribute_set< CharT > const& src_attrs)
{
    BOOST_VERIFY(!!aux::make_record_sink_holder< CharT >::instance); // to force sink registration

    typedef boost::log::basic_core< CharT > core_type;
    return core_type::get()->open_record(src_attrs);
}

#endif // BOOST_LOG_TESTS_MAKE_RECORD_HPP_INCLUDED_
