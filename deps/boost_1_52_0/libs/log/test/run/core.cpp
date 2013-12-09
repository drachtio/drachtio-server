/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   core.cpp
 * \author Andrey Semashev
 * \date   08.02.2009
 *
 * \brief  This header contains tests for the logging core.
 */

#define BOOST_TEST_MODULE core

#include <cstddef>
#include <map>
#include <string>
#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/test/included/unit_test.hpp>
#include <boost/log/core/core.hpp>
#include <boost/log/attributes/constant.hpp>
#include <boost/log/attributes/attribute_set.hpp>
#include <boost/log/attributes/attribute_values_view.hpp>
#include <boost/log/filters/has_attr.hpp>
#include <boost/log/sinks/sink.hpp>
#include <boost/log/core/record.hpp>
#ifndef BOOST_LOG_NO_THREADS
#include <boost/thread/thread.hpp>
#endif // BOOST_LOG_NO_THREADS
#include "char_definitions.hpp"
#include "test_sink.hpp"

namespace logging = boost::log;
namespace attrs = logging::attributes;
namespace sinks = logging::sinks;
namespace flt = logging::filters;

// The test checks that message filtering works
BOOST_AUTO_TEST_CASE_TEMPLATE(filtering, CharT, char_types)
{
    typedef logging::basic_attribute_set< CharT > attr_set;
    typedef logging::basic_core< CharT > core;
    typedef typename core::record_type record_type;
    typedef std::basic_string< CharT > string;
    typedef test_data< CharT > data;

    attrs::constant< int > attr1(10);
    attrs::constant< double > attr2(5.5);

    attr_set set1;
    set1[data::attr1()] = attr1;
    set1[data::attr2()] = attr2;

    boost::shared_ptr< core > pCore = core::get();
    boost::shared_ptr< test_sink< CharT > > pSink(new test_sink< CharT >());
    pCore->add_sink(pSink);

    // No filtering at all
    {
        record_type rec = pCore->open_record(set1);
        BOOST_REQUIRE(rec);
        pCore->push_record(rec);
        BOOST_CHECK_EQUAL(pSink->m_RecordCounter, 1UL);
        BOOST_CHECK_EQUAL(pSink->m_Consumed[data::attr1()], 1UL);
        BOOST_CHECK_EQUAL(pSink->m_Consumed[data::attr2()], 1UL);
        BOOST_CHECK_EQUAL(pSink->m_Consumed[data::attr3()], 0UL);
        BOOST_CHECK_EQUAL(pSink->m_Consumed[data::attr4()], 0UL);
        pSink->clear();
    }

    // Core-level filtering
    {
        pCore->set_filter(flt::has_attr(data::attr3()));
        record_type rec = pCore->open_record(set1);
        BOOST_CHECK(!rec);
        BOOST_CHECK_EQUAL(pSink->m_RecordCounter, 0UL);
        BOOST_CHECK_EQUAL(pSink->m_Consumed[data::attr1()], 0UL);
        BOOST_CHECK_EQUAL(pSink->m_Consumed[data::attr2()], 0UL);
        BOOST_CHECK_EQUAL(pSink->m_Consumed[data::attr3()], 0UL);
        BOOST_CHECK_EQUAL(pSink->m_Consumed[data::attr4()], 0UL);
        pSink->clear();
    }
    {
        pCore->set_filter(flt::has_attr(data::attr2()));
        record_type rec = pCore->open_record(set1);
        BOOST_REQUIRE(rec);
        pCore->push_record(rec);
        BOOST_CHECK_EQUAL(pSink->m_RecordCounter, 1UL);
        BOOST_CHECK_EQUAL(pSink->m_Consumed[data::attr1()], 1UL);
        BOOST_CHECK_EQUAL(pSink->m_Consumed[data::attr2()], 1UL);
        BOOST_CHECK_EQUAL(pSink->m_Consumed[data::attr3()], 0UL);
        BOOST_CHECK_EQUAL(pSink->m_Consumed[data::attr4()], 0UL);
        pSink->clear();
    }

    // Sink-level filtering
    {
        pCore->reset_filter();
        pSink->set_filter(flt::has_attr(data::attr2()));
        record_type rec = pCore->open_record(set1);
        BOOST_REQUIRE(rec);
        pCore->push_record(rec);
        BOOST_CHECK_EQUAL(pSink->m_RecordCounter, 1UL);
        BOOST_CHECK_EQUAL(pSink->m_Consumed[data::attr1()], 1UL);
        BOOST_CHECK_EQUAL(pSink->m_Consumed[data::attr2()], 1UL);
        BOOST_CHECK_EQUAL(pSink->m_Consumed[data::attr3()], 0UL);
        BOOST_CHECK_EQUAL(pSink->m_Consumed[data::attr4()], 0UL);
        pSink->clear();
    }
    {
        pSink->set_filter(flt::has_attr(data::attr3()));
        record_type rec = pCore->open_record(set1);
        BOOST_CHECK(!rec);
        BOOST_CHECK_EQUAL(pSink->m_RecordCounter, 0UL);
        BOOST_CHECK_EQUAL(pSink->m_Consumed[data::attr1()], 0UL);
        BOOST_CHECK_EQUAL(pSink->m_Consumed[data::attr2()], 0UL);
        BOOST_CHECK_EQUAL(pSink->m_Consumed[data::attr3()], 0UL);
        BOOST_CHECK_EQUAL(pSink->m_Consumed[data::attr4()], 0UL);
        pSink->clear();
        pSink->reset_filter();
    }
    // Only one sink of the two accepts the record
    {
        pSink->set_filter(flt::has_attr(data::attr2()));

        boost::shared_ptr< test_sink< CharT > > pSink2(new test_sink< CharT >());
        pCore->add_sink(pSink2);
        pSink2->set_filter(flt::has_attr(data::attr3()));

        record_type rec = pCore->open_record(set1);
        BOOST_REQUIRE(rec);
        pCore->push_record(rec);

        BOOST_CHECK_EQUAL(pSink->m_RecordCounter, 1UL);
        BOOST_CHECK_EQUAL(pSink->m_Consumed[data::attr1()], 1UL);
        BOOST_CHECK_EQUAL(pSink->m_Consumed[data::attr2()], 1UL);
        BOOST_CHECK_EQUAL(pSink->m_Consumed[data::attr3()], 0UL);
        BOOST_CHECK_EQUAL(pSink->m_Consumed[data::attr4()], 0UL);
        pSink->clear();

        BOOST_CHECK_EQUAL(pSink2->m_RecordCounter, 0UL);
        BOOST_CHECK_EQUAL(pSink2->m_Consumed[data::attr1()], 0UL);
        BOOST_CHECK_EQUAL(pSink2->m_Consumed[data::attr2()], 0UL);
        BOOST_CHECK_EQUAL(pSink2->m_Consumed[data::attr3()], 0UL);
        BOOST_CHECK_EQUAL(pSink2->m_Consumed[data::attr4()], 0UL);
        pCore->remove_sink(pSink2);
    }

    pCore->remove_sink(pSink);
    pCore->reset_filter();
}

#ifndef BOOST_LOG_NO_THREADS
namespace {

    //! A test routine that runs in a separate thread
    template< typename CharT >
    void thread_attributes_test()
    {
        typedef test_data< CharT > data;
        typedef logging::basic_core< CharT > core;
        typedef typename core::record_type record_type;
        typedef std::basic_string< CharT > string;
        typedef logging::basic_attribute_set< CharT > attr_set;
        attrs::constant< short > attr4(255);

        boost::shared_ptr< core > pCore = core::get();
        pCore->add_thread_attribute(data::attr4(), attr4);

        attr_set set1;
        record_type rec = pCore->open_record(set1);
        BOOST_CHECK(rec);
        if (rec)
            pCore->push_record(rec);
    }

} // namespace
#endif // BOOST_LOG_NO_THREADS

// The test checks that global and thread-specific attributes work
BOOST_AUTO_TEST_CASE_TEMPLATE(attributes, CharT, char_types)
{
    typedef logging::basic_attribute_set< CharT > attr_set;
    typedef logging::basic_core< CharT > core;
    typedef typename core::record_type record_type;
    typedef std::basic_string< CharT > string;
    typedef test_data< CharT > data;

    attrs::constant< int > attr1(10);
    attrs::constant< double > attr2(5.5);
    attrs::constant< std::string > attr3("Hello, world!");

    attr_set set1;
    set1[data::attr1()] = attr1;

    boost::shared_ptr< core > pCore = core::get();
    boost::shared_ptr< test_sink< CharT > > pSink(new test_sink< CharT >());
    pCore->add_sink(pSink);

    typename core::attribute_set_type::iterator itGlobal = pCore->add_global_attribute(data::attr2(), attr2).first;
    typename core::attribute_set_type::iterator itThread = pCore->add_thread_attribute(data::attr3(), attr3).first;

    {
        attr_set glob = pCore->get_global_attributes();
        BOOST_CHECK_EQUAL(glob.size(), 1UL);
        BOOST_CHECK_EQUAL(glob.count(data::attr2()), 1UL);

        attr_set thr = pCore->get_thread_attributes();
        BOOST_CHECK_EQUAL(thr.size(), 1UL);
        BOOST_CHECK_EQUAL(thr.count(data::attr3()), 1UL);
    }
    {
        record_type rec = pCore->open_record(set1);
        BOOST_REQUIRE(rec);
        pCore->push_record(rec);
        BOOST_CHECK_EQUAL(pSink->m_RecordCounter, 1UL);
        BOOST_CHECK_EQUAL(pSink->m_Consumed[data::attr1()], 1UL);
        BOOST_CHECK_EQUAL(pSink->m_Consumed[data::attr2()], 1UL);
        BOOST_CHECK_EQUAL(pSink->m_Consumed[data::attr3()], 1UL);
        BOOST_CHECK_EQUAL(pSink->m_Consumed[data::attr4()], 0UL);
        pSink->clear();
    }
#ifndef BOOST_LOG_NO_THREADS
    {
        boost::thread th(&thread_attributes_test< CharT >);
        th.join();
        BOOST_CHECK_EQUAL(pSink->m_RecordCounter, 1UL);
        BOOST_CHECK_EQUAL(pSink->m_Consumed[data::attr1()], 0UL);
        BOOST_CHECK_EQUAL(pSink->m_Consumed[data::attr2()], 1UL);
        BOOST_CHECK_EQUAL(pSink->m_Consumed[data::attr3()], 0UL);
        BOOST_CHECK_EQUAL(pSink->m_Consumed[data::attr4()], 1UL);
        pSink->clear();

        // Thread-specific attributes must not interfere
        attr_set thr = pCore->get_thread_attributes();
        BOOST_CHECK_EQUAL(thr.size(), 1UL);
        BOOST_CHECK_EQUAL(thr.count(data::attr3()), 1UL);
    }
#endif // BOOST_LOG_NO_THREADS

    pCore->remove_global_attribute(itGlobal);
    pCore->remove_thread_attribute(itThread);
    pCore->remove_sink(pSink);
}
