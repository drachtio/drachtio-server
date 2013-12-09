/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   test_sink.hpp
 * \author Andrey Semashev
 * \date   18.03.2009
 *
 * \brief  This header contains a test sink frontend that is used through various tests.
 */

#ifndef BOOST_LOG_TESTS_TEST_SINK_HPP_INCLUDED_
#define BOOST_LOG_TESTS_TEST_SINK_HPP_INCLUDED_

#include <map>
#include <boost/compatibility/cpp_c_headers/cstddef>
#include <boost/log/sinks/sink.hpp>

//! A sink implementation for testing purpose
template< typename CharT >
struct test_sink :
    public boost::log::sinks::sink< CharT >
{
private:
    typedef boost::log::sinks::sink< CharT > base_type;

public:
    typedef typename base_type::char_type char_type;
    typedef typename base_type::string_type string_type;
    typedef typename base_type::values_view_type values_view_type;
    typedef typename base_type::record_type record_type;
    typedef typename base_type::filter_type filter_type;
    typedef typename values_view_type::key_type key_type;

    struct key_type_order
    {
        typedef bool result_type;

        result_type operator() (key_type const& left, key_type const& right) const
        {
            return left.id() < right.id();
        }
    };

    typedef std::map< key_type, std::size_t, key_type_order > attr_counters_map;

public:
    filter_type m_Filter;
    attr_counters_map m_Consumed;
    std::size_t m_RecordCounter;

public:
    test_sink() : m_RecordCounter(0) {}

    void set_filter(filter_type const& f)
    {
        m_Filter = f;
    }

    void reset_filter()
    {
        m_Filter.clear();
    }

    bool will_consume(values_view_type const& attributes)
    {
        return (m_Filter.empty() || m_Filter(attributes));
    }

    void consume(record_type const& record)
    {
        ++m_RecordCounter;
        typename values_view_type::const_iterator
            it = record.attribute_values().begin(),
            end = record.attribute_values().end();
        for (; it != end; ++it)
            ++m_Consumed[it->first];
    }

    void flush()
    {
    }

    void clear()
    {
        m_RecordCounter = 0;
        m_Consumed.clear();
    }
};

#endif // BOOST_LOG_TESTS_TEST_SINK_HPP_INCLUDED_
