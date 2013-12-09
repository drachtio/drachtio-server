/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   core.cpp
 * \author Andrey Semashev
 * \date   19.04.2007
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/libs/log/doc/log.html.
 */

#include <memory>
#include <deque>
#include <vector>
#include <algorithm>
#include <boost/weak_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/compatibility/cpp_c_headers/cstddef>
#include <boost/log/core/core.hpp>
#include <boost/log/sinks/sink.hpp>
#include <boost/log/attributes/attribute_values_view.hpp>
#include <boost/log/detail/singleton.hpp>
#if !defined(BOOST_LOG_NO_THREADS)
#include <boost/thread/tss.hpp>
#include <boost/thread/exceptions.hpp>
#include <boost/log/detail/locks.hpp>
#include <boost/log/detail/light_rw_mutex.hpp>
#endif
#include "default_sink.hpp"

namespace boost {

namespace BOOST_LOG_NAMESPACE {

//! Private record data information, with core-specific structures
template< typename CharT >
struct basic_record< CharT >::private_data :
    public public_data
{
    //! Sink interface type
    typedef sinks::sink< CharT > sink_type;
    //! Sinks container type
    typedef std::deque< weak_ptr< sink_type > > sink_list;

    //! A list of sinks that will accept the record
    sink_list m_AcceptingSinks;

    template< typename SourceT >
    explicit private_data(SourceT const& values) : public_data(values)
    {
    }
};

//! Logging system implementation
template< typename CharT >
struct basic_core< CharT >::implementation :
    public log::aux::lazy_singleton<
        implementation,
        shared_ptr< basic_core< CharT > >
    >
{
public:
    //! Base type of singleton holder
    typedef log::aux::lazy_singleton<
        implementation,
        shared_ptr< basic_core< CharT > >
    > base_type;

    //! Front-end class type
    typedef basic_core< char_type > core_type;
#if !defined(BOOST_LOG_NO_THREADS)
    //! Read lock type
    typedef log::aux::shared_lock_guard< log::aux::light_rw_mutex > scoped_read_lock;
    //! Write lock type
    typedef log::aux::exclusive_lock_guard< log::aux::light_rw_mutex > scoped_write_lock;
#endif

    //! Sinks container type
    typedef std::vector< shared_ptr< sink_type > > sink_list;

    //! Thread-specific data
    struct thread_data
    {
        //! Thread-specific attribute set
        attribute_set_type ThreadAttributes;
    };

    //! Log record implementation type
    typedef typename record_type::private_data record_private_data;

public:
#if !defined(BOOST_LOG_NO_THREADS)
    //! Synchronization mutex
    log::aux::light_rw_mutex Mutex;
#endif

    //! List of sinks involved into output
    sink_list Sinks;
    //! Default sink
    const shared_ptr< sink_type > DefaultSink;

    //! Global attribute set
    attribute_set_type GlobalAttributes;
#if !defined(BOOST_LOG_NO_THREADS)
    //! Thread-specific data
    thread_specific_ptr< thread_data > pThreadData;

#if defined(BOOST_LOG_USE_COMPILER_TLS)
    //! Cached pointer to the thread-specific data
    static BOOST_LOG_TLS thread_data* pThreadDataCache;
#endif

#else
    //! Thread-specific data
    std::auto_ptr< thread_data > pThreadData;
#endif

    //! The global state of logging
    volatile bool Enabled;
    //! Global filter
    filter_type Filter;

    //! Exception handler
    exception_handler_type ExceptionHandler;

public:
    //! Constructor
    implementation() :
        DefaultSink(boost::make_shared< sinks::aux::basic_default_sink< char_type > >()),
        Enabled(true)
    {
    }

    //! Invokes sink-specific filter and adds the sink to the record if the filter passes the log record
    void apply_sink_filter(shared_ptr< sink_type > const& sink, record_type& rec, record_private_data*& rec_impl, values_view_type*& attr_values)
    {
        try
        {
            if (sink->will_consume(*attr_values))
            {
                // If at least one sink accepts the record, it's time to create it
                if (!rec_impl)
                {
                    attr_values->freeze();
                    rec.m_pData = rec_impl = new record_private_data(move(*attr_values));
                    attr_values = &rec.m_pData->m_AttributeValues;
                }
                rec_impl->m_AcceptingSinks.push_back(sink);
            }
        }
#if !defined(BOOST_LOG_NO_THREADS)
        catch (thread_interrupted&)
        {
            throw;
        }
#endif // !defined(BOOST_LOG_NO_THREADS)
        catch (...)
        {
            if (this->ExceptionHandler.empty())
                throw;

            // Assume that the sink is incapable to receive messages now
            this->ExceptionHandler();

            if (rec_impl && rec_impl->m_AcceptingSinks.empty())
            {
                rec_impl = NULL;
                rec.m_pData = NULL; // destroy record implementation
            }
        }
    }

    //! The method returns the current thread-specific data
    thread_data* get_thread_data()
    {
#if defined(BOOST_LOG_USE_COMPILER_TLS)
        thread_data* p = pThreadDataCache;
#else
        thread_data* p = pThreadData.get();
#endif
        if (!p)
        {
            init_thread_data();
#if defined(BOOST_LOG_USE_COMPILER_TLS)
            p = pThreadDataCache;
#else
            p = pThreadData.get();
#endif
        }
        return p;
    }

    //! The function initializes the logging system
    static void init_instance()
    {
        base_type::get_instance().reset(new core_type());
    }

private:
    //! The method initializes thread-specific data
    void init_thread_data()
    {
        BOOST_LOG_EXPR_IF_MT(scoped_write_lock lock(Mutex);)
        if (!pThreadData.get())
        {
            std::auto_ptr< thread_data > p(new thread_data());
            pThreadData.reset(p.get());
#if defined(BOOST_LOG_USE_COMPILER_TLS)
            pThreadDataCache = p.release();
#else
            p.release();
#endif
        }
    }
};

#if defined(BOOST_LOG_USE_COMPILER_TLS)
//! Cached pointer to the thread-specific data
template< typename CharT >
BOOST_LOG_TLS typename basic_core< CharT >::implementation::thread_data*
basic_core< CharT >::implementation::pThreadDataCache = NULL;
#endif // defined(BOOST_LOG_USE_COMPILER_TLS)

//! Logging system constructor
template< typename CharT >
basic_core< CharT >::basic_core() :
    pImpl(new implementation())
{
}

//! Logging system destructor
template< typename CharT >
basic_core< CharT >::~basic_core()
{
    delete pImpl;
}

//! The method returns a pointer to the logging system instance
template< typename CharT >
shared_ptr< basic_core< CharT > > basic_core< CharT >::get()
{
    return implementation::get();
}

//! The method enables or disables logging and returns the previous state of logging flag
template< typename CharT >
bool basic_core< CharT >::set_logging_enabled(bool enabled)
{
    BOOST_LOG_EXPR_IF_MT(typename implementation::scoped_write_lock lock(pImpl->Mutex);)
    const bool old_value = pImpl->Enabled;
    pImpl->Enabled = enabled;
    return old_value;
}

//! The method allows to detect if logging is enabled
template< typename CharT >
bool basic_core< CharT >::get_logging_enabled() const
{
    // Should have a read barrier here, but for performance reasons it is omitted.
    // The function should be used as a quick check and doesn't need to be reliable.
    return pImpl->Enabled;
}

//! The method adds a new sink
template< typename CharT >
void basic_core< CharT >::add_sink(shared_ptr< sink_type > const& s)
{
    BOOST_LOG_EXPR_IF_MT(typename implementation::scoped_write_lock lock(pImpl->Mutex);)
    typename implementation::sink_list::iterator it =
        std::find(pImpl->Sinks.begin(), pImpl->Sinks.end(), s);
    if (it == pImpl->Sinks.end())
        pImpl->Sinks.push_back(s);
}

//! The method removes the sink from the output
template< typename CharT >
void basic_core< CharT >::remove_sink(shared_ptr< sink_type > const& s)
{
    BOOST_LOG_EXPR_IF_MT(typename implementation::scoped_write_lock lock(pImpl->Mutex);)
    typename implementation::sink_list::iterator it =
        std::find(pImpl->Sinks.begin(), pImpl->Sinks.end(), s);
    if (it != pImpl->Sinks.end())
        pImpl->Sinks.erase(it);
}

//! The method removes all registered sinks from the output
template< typename CharT >
void basic_core< CharT >::remove_all_sinks()
{
    BOOST_LOG_EXPR_IF_MT(typename implementation::scoped_write_lock lock(pImpl->Mutex);)
    pImpl->Sinks.clear();
}


//! The method adds an attribute to the global attribute set
template< typename CharT >
std::pair< typename basic_core< CharT >::attribute_set_type::iterator, bool >
basic_core< CharT >::add_global_attribute(attribute_name_type const& name, attribute const& attr)
{
    BOOST_LOG_EXPR_IF_MT(typename implementation::scoped_write_lock lock(pImpl->Mutex);)
    return pImpl->GlobalAttributes.insert(name, attr);
}

//! The method removes an attribute from the global attribute set
template< typename CharT >
void basic_core< CharT >::remove_global_attribute(typename attribute_set_type::iterator it)
{
    BOOST_LOG_EXPR_IF_MT(typename implementation::scoped_write_lock lock(pImpl->Mutex);)
    pImpl->GlobalAttributes.erase(it);
}

//! The method returns the complete set of currently registered global attributes
template< typename CharT >
typename basic_core< CharT >::attribute_set_type basic_core< CharT >::get_global_attributes() const
{
    BOOST_LOG_EXPR_IF_MT(typename implementation::scoped_read_lock lock(pImpl->Mutex);)
    return pImpl->GlobalAttributes;
}
//! The method replaces the complete set of currently registered global attributes with the provided set
template< typename CharT >
void basic_core< CharT >::set_global_attributes(attribute_set_type const& attrs)
{
    BOOST_LOG_EXPR_IF_MT(typename implementation::scoped_write_lock lock(pImpl->Mutex);)
    pImpl->GlobalAttributes = attrs;
}

//! The method adds an attribute to the thread-specific attribute set
template< typename CharT >
std::pair< typename basic_core< CharT >::attribute_set_type::iterator, bool >
basic_core< CharT >::add_thread_attribute(attribute_name_type const& name, attribute const& attr)
{
    typename implementation::thread_data* p = pImpl->get_thread_data();
    return p->ThreadAttributes.insert(name, attr);
}

//! The method removes an attribute from the thread-specific attribute set
template< typename CharT >
void basic_core< CharT >::remove_thread_attribute(typename attribute_set_type::iterator it)
{
    typename implementation::thread_data* p = pImpl->pThreadData.get();
    if (p)
        p->ThreadAttributes.erase(it);
}

//! The method returns the complete set of currently registered thread-specific attributes
template< typename CharT >
typename basic_core< CharT >::attribute_set_type basic_core< CharT >::get_thread_attributes() const
{
    typename implementation::thread_data* p = pImpl->get_thread_data();
    return p->ThreadAttributes;
}
//! The method replaces the complete set of currently registered thread-specific attributes with the provided set
template< typename CharT >
void basic_core< CharT >::set_thread_attributes(attribute_set_type const& attrs)
{
    typename implementation::thread_data* p = pImpl->get_thread_data();
    p->ThreadAttributes = attrs;
}

//! An internal method to set the global filter
template< typename CharT >
void basic_core< CharT >::set_filter(filter_type const& filter)
{
    BOOST_LOG_EXPR_IF_MT(typename implementation::scoped_write_lock lock(pImpl->Mutex);)
    pImpl->Filter = filter;
}

//! The method removes the global logging filter
template< typename CharT >
void basic_core< CharT >::reset_filter()
{
    BOOST_LOG_EXPR_IF_MT(typename implementation::scoped_write_lock lock(pImpl->Mutex);)
    pImpl->Filter.clear();
}

//! The method sets exception handler function
template< typename CharT >
void basic_core< CharT >::set_exception_handler(exception_handler_type const& handler)
{
    BOOST_LOG_EXPR_IF_MT(typename implementation::scoped_write_lock lock(pImpl->Mutex);)
    pImpl->ExceptionHandler = handler;
}

//! The method performs flush on all registered sinks.
template< typename CharT >
void basic_core< CharT >::flush()
{
    // Acquire exclusive lock to prevent any logging attempts while flushing
    BOOST_LOG_EXPR_IF_MT(typename implementation::scoped_write_lock lock(pImpl->Mutex);)
    typename implementation::sink_list::iterator it = pImpl->Sinks.begin(), end = pImpl->Sinks.end();
    for (; it != end; ++it)
    {
        try
        {
            it->get()->flush();
        }
#if !defined(BOOST_LOG_NO_THREADS)
        catch (thread_interrupted&)
        {
            throw;
        }
#endif // !defined(BOOST_LOG_NO_THREADS)
        catch (...)
        {
            if (pImpl->ExceptionHandler.empty())
                throw;
            pImpl->ExceptionHandler();
        }
    }
}

//! The method opens a new record to be written and returns true if the record was opened
template< typename CharT >
typename basic_core< CharT >::record_type basic_core< CharT >::open_record(attribute_set_type const& source_attributes)
{
    record_type rec;

    // Try a quick win first
    if (pImpl->Enabled) try
    {
        typename implementation::thread_data* tsd = pImpl->get_thread_data();

        // Lock the core to be safe against any attribute or sink set modifications
        BOOST_LOG_EXPR_IF_MT(typename implementation::scoped_read_lock lock(pImpl->Mutex);)

        if (pImpl->Enabled)
        {
            // Compose a view of attribute values (unfrozen, yet)
            values_view_type temp_attr_values(source_attributes, tsd->ThreadAttributes, pImpl->GlobalAttributes);
            register values_view_type* attr_values = &temp_attr_values;

            if (pImpl->Filter.empty() || pImpl->Filter(*attr_values))
            {
                // The global filter passed, trying the sinks
                typename implementation::record_private_data* rec_impl = NULL;
                if (!pImpl->Sinks.empty())
                {
                    typename implementation::sink_list::iterator it = pImpl->Sinks.begin(), end = pImpl->Sinks.end();
                    for (; it != end; ++it)
                    {
                        pImpl->apply_sink_filter(*it, rec, rec_impl, attr_values);
                    }
                }
                else
                {
                    // Use the default sink
                    pImpl->apply_sink_filter(pImpl->DefaultSink, rec, rec_impl, attr_values);
                }
            }
        }
    }
#if !defined(BOOST_LOG_NO_THREADS)
    catch (thread_interrupted&)
    {
        throw;
    }
#endif // !defined(BOOST_LOG_NO_THREADS)
    catch (...)
    {
        // Lock the core to be safe against any attribute or sink set modifications
        BOOST_LOG_EXPR_IF_MT(typename implementation::scoped_read_lock lock(pImpl->Mutex);)
        if (pImpl->ExceptionHandler.empty())
            throw;

        pImpl->ExceptionHandler();
    }

    return rec;
}

//! The method pushes the record
template< typename CharT >
void basic_core< CharT >::push_record(record_type const& rec)
{
    try
    {
        typedef typename record_type::private_data record_private_data;
        record_private_data* pData = static_cast< record_private_data* >(rec.m_pData.get());

        typedef std::vector< shared_ptr< sink_type > > accepting_sinks_t;
        accepting_sinks_t accepting_sinks(pData->m_AcceptingSinks.size());
        shared_ptr< sink_type >* const begin = &*accepting_sinks.begin();
        register shared_ptr< sink_type >* end = begin;

        // Lock sinks that are willing to consume the record
        typename record_private_data::sink_list::iterator
            weak_it = pData->m_AcceptingSinks.begin(),
            weak_end = pData->m_AcceptingSinks.end();
        for (; weak_it != weak_end; ++weak_it)
        {
            shared_ptr< sink_type >& last = *end;
            weak_it->lock().swap(last);
            if (last)
                ++end;
        }

        bool shuffled = (end - begin) <= 1;
        register shared_ptr< sink_type >* it = begin;
        while (true) try
        {
            // First try to distribute load between different sinks
            register bool all_locked = true;
            while (it != end)
            {
                if (it->get()->try_consume(rec))
                {
                    --end;
                    end->swap(*it);
                    all_locked = false;
                }
                else
                    ++it;
            }

            it = begin;
            if (begin != end)
            {
                if (all_locked)
                {
                    // If all sinks are busy then block on any
                    if (!shuffled)
                    {
                        std::random_shuffle(begin, end);
                        shuffled = true;
                    }

                    it->get()->consume(rec);
                    --end;
                    end->swap(*it);
                }
            }
            else
                break;
        }
#if !defined(BOOST_LOG_NO_THREADS)
        catch (thread_interrupted&)
        {
            throw;
        }
#endif // !defined(BOOST_LOG_NO_THREADS)
        catch (...)
        {
            // Lock the core to be safe against any attribute or sink set modifications
            BOOST_LOG_EXPR_IF_MT(typename implementation::scoped_read_lock lock(pImpl->Mutex);)
            if (pImpl->ExceptionHandler.empty())
                throw;

            pImpl->ExceptionHandler();

            // Skip the sink that failed to consume the record
            --end;
            end->swap(*it);
        }
    }
#if !defined(BOOST_LOG_NO_THREADS)
    catch (thread_interrupted&)
    {
        throw;
    }
#endif // !defined(BOOST_LOG_NO_THREADS)
    catch (...)
    {
        // Lock the core to be safe against any attribute or sink set modifications
        BOOST_LOG_EXPR_IF_MT(typename implementation::scoped_read_lock lock(pImpl->Mutex);)
        if (pImpl->ExceptionHandler.empty())
            throw;

        pImpl->ExceptionHandler();
    }
}

//  Explicitly instantiate core implementation
#ifdef BOOST_LOG_USE_CHAR
template class BOOST_LOG_EXPORT basic_core< char >;
#endif
#ifdef BOOST_LOG_USE_WCHAR_T
template class BOOST_LOG_EXPORT basic_core< wchar_t >;
#endif

} // namespace log

} // namespace boost
