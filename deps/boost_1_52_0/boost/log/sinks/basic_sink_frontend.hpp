/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   basic_sink_frontend.hpp
 * \author Andrey Semashev
 * \date   14.07.2009
 *
 * The header contains implementation of a base class for sink frontends.
 */

#if (defined(_MSC_VER) && _MSC_VER > 1000)
#pragma once
#endif // _MSC_VER > 1000

#ifndef BOOST_LOG_SINKS_BASIC_SINK_FRONTEND_HPP_INCLUDED_
#define BOOST_LOG_SINKS_BASIC_SINK_FRONTEND_HPP_INCLUDED_

#include <boost/mpl/bool.hpp>
#include <boost/log/detail/prologue.hpp>
#include <boost/log/detail/light_function.hpp>
#include <boost/log/detail/cleanup_scope_guard.hpp>
#include <boost/log/detail/code_conversion.hpp>
#include <boost/log/detail/attachable_sstream_buf.hpp>
#include <boost/log/detail/fake_mutex.hpp>
#include <boost/log/sinks/sink.hpp>
#include <boost/log/sinks/frontend_requirements.hpp>
#if !defined(BOOST_LOG_NO_THREADS)
#include <boost/thread/exceptions.hpp>
#include <boost/thread/tss.hpp>
#include <boost/thread/locks.hpp>
#include <boost/log/detail/locks.hpp>
#include <boost/log/detail/light_rw_mutex.hpp>
#include <boost/concept_check.hpp>
#endif // !defined(BOOST_LOG_NO_THREADS)

#ifdef _MSC_VER
#pragma warning(push)
// 'm_A' : class 'A' needs to have dll-interface to be used by clients of class 'B'
#pragma warning(disable: 4251)
// non dll-interface class 'A' used as base for dll-interface class 'B'
#pragma warning(disable: 4275)
#endif // _MSC_VER

namespace boost {

namespace BOOST_LOG_NAMESPACE {

namespace sinks {

//! A base class for a logging sink frontend
template< typename CharT >
class BOOST_LOG_NO_VTABLE basic_sink_frontend :
    public sink< CharT >
{
    //! Base type
    typedef sink< CharT > base_type;

public:
    //! Character type
    typedef typename base_type::char_type char_type;
    //! String type to be used as a message text holder
    typedef typename base_type::string_type string_type;
    //! Log record type
    typedef typename base_type::record_type record_type;
    //! Attribute values view type
    typedef typename base_type::values_view_type values_view_type;
    //! Filter function type
    typedef boost::log::aux::light_function1< bool, values_view_type const& > filter_type;
    //! An exception handler type
    typedef boost::log::aux::light_function0< void > exception_handler_type;

#if !defined(BOOST_LOG_NO_THREADS)
protected:
    //! Mutex type
    typedef boost::log::aux::light_rw_mutex mutex_type;

private:
    //! Synchronization mutex
    mutable mutex_type m_Mutex;
#endif

private:
    //! Filter
    filter_type m_Filter;
    //! Exception handler
    exception_handler_type m_ExceptionHandler;

public:
    /*!
     * The method sets sink-specific filter functional object
     */
    template< typename FunT >
    void set_filter(FunT const& filter)
    {
        BOOST_LOG_EXPR_IF_MT(boost::log::aux::exclusive_lock_guard< mutex_type > lock(m_Mutex);)
        m_Filter = filter;
    }
    /*!
     * The method resets the filter
     */
    void reset_filter()
    {
        BOOST_LOG_EXPR_IF_MT(boost::log::aux::exclusive_lock_guard< mutex_type > lock(m_Mutex);)
        m_Filter.clear();
    }

    /*!
     * The method sets an exception handler function
     */
    template< typename FunT >
    void set_exception_handler(FunT const& handler)
    {
        BOOST_LOG_EXPR_IF_MT(boost::log::aux::exclusive_lock_guard< mutex_type > lock(m_Mutex);)
        m_ExceptionHandler = handler;
    }

    /*!
     * The method resets the exception handler function
     */
    void reset_exception_handler()
    {
        BOOST_LOG_EXPR_IF_MT(boost::log::aux::exclusive_lock_guard< mutex_type > lock(m_Mutex);)
        m_ExceptionHandler.clear();
    }

    /*!
     * The method returns \c true if no filter is set or the attribute values pass the filter
     *
     * \param attributes A set of attribute values of a logging record
     */
    bool will_consume(values_view_type const& attributes)
    {
        BOOST_LOG_EXPR_IF_MT(boost::log::aux::exclusive_lock_guard< mutex_type > lock(m_Mutex);)
        try
        {
            return (m_Filter.empty() || m_Filter(attributes));
        }
#if !defined(BOOST_LOG_NO_THREADS)
        catch (thread_interrupted&)
        {
            throw;
        }
#endif
        catch (...)
        {
            if (m_ExceptionHandler.empty())
                throw;
            m_ExceptionHandler();
            return false;
        }
    }

protected:
#if !defined(BOOST_LOG_NO_THREADS)
    //! Returns reference to the frontend mutex
    mutex_type& frontend_mutex() const { return m_Mutex; }
#endif

    //! Returns reference to the exception handler
    exception_handler_type& exception_handler() { return m_ExceptionHandler; }
    //! Returns reference to the exception handler
    exception_handler_type const& exception_handler() const { return m_ExceptionHandler; }

    //! Feeds log record to the backend
    template< typename BackendMutexT, typename BackendT >
    void feed_record(record_type const& rec, BackendMutexT& backend_mutex, BackendT& backend)
    {
        try
        {
            BOOST_LOG_EXPR_IF_MT(boost::log::aux::exclusive_lock_guard< BackendMutexT > lock(backend_mutex);)
            backend.consume(rec);
        }
#if !defined(BOOST_LOG_NO_THREADS)
        catch (thread_interrupted&)
        {
            throw;
        }
#endif
        catch (...)
        {
            BOOST_LOG_EXPR_IF_MT(boost::log::aux::shared_lock_guard< mutex_type > lock(m_Mutex);)
            if (m_ExceptionHandler.empty())
                throw;
            m_ExceptionHandler();
        }
    }

    //! Attempts to feeds log record to the backend, does not block if \a backend_mutex is locked
    template< typename BackendMutexT, typename BackendT >
    bool try_feed_record(record_type const& rec, BackendMutexT& backend_mutex, BackendT& backend)
    {
#if !defined(BOOST_LOG_NO_THREADS)
        unique_lock< BackendMutexT > lock;
        try
        {
            unique_lock< BackendMutexT > tmp_lock(backend_mutex, try_to_lock);
            if (!tmp_lock.owns_lock())
                return false;
            lock.swap(tmp_lock);
        }
        catch (thread_interrupted&)
        {
            throw;
        }
        catch (...)
        {
            boost::log::aux::shared_lock_guard< mutex_type > frontend_lock(this->frontend_mutex());
            if (this->exception_handler().empty())
                throw;
            this->exception_handler()();
        }
#endif
        // No need to lock anything in the feed_record method
        boost::log::aux::fake_mutex m;
        feed_record(rec, m, backend);
        return true;
    }

    //! Flushes record buffers in the backend, if one supports it
    template< typename BackendMutexT, typename BackendT >
    void flush_backend(BackendMutexT& backend_mutex, BackendT& backend)
    {
        typedef typename BackendT::frontend_requirements frontend_requirements;
        flush_backend_impl(backend_mutex, backend,
            typename has_requirement< frontend_requirements, flushing >::type());
    }

private:
    //! Flushes record buffers in the backend (the actual implementation)
    template< typename BackendMutexT, typename BackendT >
    void flush_backend_impl(BackendMutexT& backend_mutex, BackendT& backend, mpl::true_)
    {
        try
        {
            BOOST_LOG_EXPR_IF_MT(boost::log::aux::exclusive_lock_guard< BackendMutexT > lock(backend_mutex);)
            backend.flush();
        }
#if !defined(BOOST_LOG_NO_THREADS)
        catch (thread_interrupted&)
        {
            throw;
        }
#endif
        catch (...)
        {
            BOOST_LOG_EXPR_IF_MT(boost::log::aux::shared_lock_guard< mutex_type > lock(m_Mutex);)
            if (m_ExceptionHandler.empty())
                throw;
            m_ExceptionHandler();
        }
    }
    //! Flushes record buffers in the backend (stub for backends that don't support flushing)
    template< typename BackendMutexT, typename BackendT >
    void flush_backend_impl(BackendMutexT&, BackendT&, mpl::false_)
    {
    }
};

//! A base class for a logging sink frontend with formatting support
template< typename CharT, typename TargetCharT = CharT >
class BOOST_LOG_NO_VTABLE basic_formatting_sink_frontend :
    public basic_sink_frontend< CharT >
{
    typedef basic_sink_frontend< CharT > base_type;

public:
    //  Type imports from the base class
    typedef typename base_type::char_type char_type;
    typedef typename base_type::string_type string_type;
    typedef typename base_type::values_view_type values_view_type;
    typedef typename base_type::record_type record_type;

    //! Output stream type
    typedef std::basic_ostream< char_type > stream_type;
    //! Target character type
    typedef TargetCharT target_char_type;
    //! Target string type
    typedef std::basic_string< target_char_type > target_string_type;
    //! Formatter function object type
    typedef boost::log::aux::light_function2<
        void,
        stream_type&,
        record_type const&
    > formatter_type;

#if !defined(BOOST_LOG_NO_THREADS)
protected:
    //! Mutex type
    typedef typename base_type::mutex_type mutex_type;
#endif

private:
    //! Stream buffer type
    typedef typename mpl::if_<
        is_same< char_type, target_char_type >,
        boost::log::aux::basic_ostringstreambuf< char_type >,
        boost::log::aux::converting_ostringstreambuf< char_type >
    >::type streambuf_type;

    struct formatting_context
    {
#if !defined(BOOST_LOG_NO_THREADS)
        //! Object version
        const unsigned int m_Version;
#endif
        //! Formatted log record storage
        target_string_type m_FormattedRecord;
        //! Stream buffer to fill the storage
        streambuf_type m_StreamBuf;
        //! Formatting stream
        stream_type m_FormattingStream;
        //! Formatter functor
        formatter_type m_Formatter;

        formatting_context() :
#if !defined(BOOST_LOG_NO_THREADS)
            m_Version(0),
#endif
            m_StreamBuf(m_FormattedRecord),
            m_FormattingStream(&m_StreamBuf)
        {
            m_FormattingStream.exceptions(std::ios_base::badbit | std::ios_base::failbit);
        }
#if !defined(BOOST_LOG_NO_THREADS)
        formatting_context(unsigned int version, std::locale const& loc, formatter_type const& formatter) :
            m_Version(version),
            m_StreamBuf(m_FormattedRecord),
            m_FormattingStream(&m_StreamBuf),
            m_Formatter(formatter)
        {
            m_FormattingStream.exceptions(std::ios_base::badbit | std::ios_base::failbit);
            m_FormattingStream.imbue(loc);
        }
#endif
    };

private:
#if !defined(BOOST_LOG_NO_THREADS)

    //! State version
    volatile unsigned int m_Version;

    //! Formatter functor
    formatter_type m_Formatter;
    //! Locale to perform formatting
    std::locale m_Locale;

    //! Formatting state
    thread_specific_ptr< formatting_context > m_pContext;

#else

    //! Formatting state
    formatting_context m_Context;

#endif // !defined(BOOST_LOG_NO_THREADS)

public:
#if !defined(BOOST_LOG_NO_THREADS)
    basic_formatting_sink_frontend() : m_Version(0)
    {
    }
#endif

    /*!
     * The method sets sink-specific formatter function object
     */
    template< typename FunT >
    void set_formatter(FunT const& formatter)
    {
#if !defined(BOOST_LOG_NO_THREADS)
        boost::log::aux::exclusive_lock_guard< mutex_type > lock(this->frontend_mutex());
        m_Formatter = formatter;
        ++m_Version;
#else
        m_Context.m_Formatter = formatter;
#endif
    }
    /*!
     * The method resets the formatter
     */
    void reset_formatter()
    {
#if !defined(BOOST_LOG_NO_THREADS)
        boost::log::aux::exclusive_lock_guard< mutex_type > lock(this->frontend_mutex());
        m_Formatter.clear();
        ++m_Version;
#else
        m_Context.m_Formatter.clear();
#endif
    }

    /*!
     * The method returns the current locale used for formatting
     */
    std::locale getloc() const
    {
#if !defined(BOOST_LOG_NO_THREADS)
        boost::log::aux::exclusive_lock_guard< mutex_type > lock(this->frontend_mutex());
        return m_Locale;
#else
        return m_Context.m_FormattingStream.getloc();
#endif
    }
    /*!
     * The method sets the locale used for formatting
     */
    void imbue(std::locale const& loc)
    {
#if !defined(BOOST_LOG_NO_THREADS)
        boost::log::aux::exclusive_lock_guard< mutex_type > lock(this->frontend_mutex());
        m_Locale = loc;
        ++m_Version;
#else
        m_Context.m_FormattingStream.imbue(loc);
#endif
    }

protected:
    //! Returns reference to the formatter
    formatter_type& formatter()
    {
#if !defined(BOOST_LOG_NO_THREADS)
        return m_Formatter;
#else
        return m_Context.m_Formatter;
#endif
    }

    //! Feeds log record to the backend
    template< typename BackendMutexT, typename BackendT >
    void feed_record(record_type const& rec, BackendMutexT& backend_mutex, BackendT& backend)
    {
        formatting_context* context;

#if !defined(BOOST_LOG_NO_THREADS)
        context = m_pContext.get();
        if (!context || context->m_Version != m_Version)
        {
            {
                boost::log::aux::shared_lock_guard< mutex_type > lock(this->frontend_mutex());
                context = new formatting_context(m_Version, m_Locale, m_Formatter);
            }
            m_pContext.reset(context);
        }
#else
        context = &m_Context;
#endif

        boost::log::aux::cleanup_guard< stream_type > cleanup1(context->m_FormattingStream);
        boost::log::aux::cleanup_guard< streambuf_type > cleanup2(context->m_StreamBuf);
        boost::log::aux::cleanup_guard< target_string_type > cleanup3(context->m_FormattedRecord);

        try
        {
            // Perform the formatting
            if (!context->m_Formatter.empty())
                context->m_Formatter(context->m_FormattingStream, rec);
            else
                context->m_FormattingStream << rec.message();

            context->m_FormattingStream.flush();

            // Feed the record
            BOOST_LOG_EXPR_IF_MT(boost::log::aux::exclusive_lock_guard< BackendMutexT > lock(backend_mutex);)
            backend.consume(rec, context->m_FormattedRecord);
        }
#if !defined(BOOST_LOG_NO_THREADS)
        catch (thread_interrupted&)
        {
            throw;
        }
#endif
        catch (...)
        {
            BOOST_LOG_EXPR_IF_MT(boost::log::aux::shared_lock_guard< mutex_type > lock(this->frontend_mutex());)
            if (this->exception_handler().empty())
                throw;
            this->exception_handler()();
        }
    }

    //! Attempts to feeds log record to the backend, does not block if \a backend_mutex is locked
    template< typename BackendMutexT, typename BackendT >
    bool try_feed_record(record_type const& rec, BackendMutexT& backend_mutex, BackendT& backend)
    {
#if !defined(BOOST_LOG_NO_THREADS)
        unique_lock< BackendMutexT > lock;
        try
        {
            unique_lock< BackendMutexT > tmp_lock(backend_mutex, try_to_lock);
            if (!tmp_lock.owns_lock())
                return false;
            lock.swap(tmp_lock);
        }
        catch (thread_interrupted&)
        {
            throw;
        }
        catch (...)
        {
            boost::log::aux::shared_lock_guard< mutex_type > frontend_lock(this->frontend_mutex());
            if (this->exception_handler().empty())
                throw;
            this->exception_handler()();
        }
#endif
        // No need to lock anything in the feed_record method
        boost::log::aux::fake_mutex m;
        feed_record(rec, m, backend);
        return true;
    }
};

namespace aux {

    template<
        typename BackendT,
        bool RequiresFormattingV = has_requirement<
            typename BackendT::frontend_requirements,
            formatted_records
        >::value
    >
    struct make_sink_frontend_base
    {
        typedef basic_sink_frontend< typename BackendT::char_type > type;
    };
    template< typename BackendT >
    struct make_sink_frontend_base< BackendT, true >
    {
        typedef basic_formatting_sink_frontend< typename BackendT::char_type, typename BackendT::target_char_type > type;
    };

} // namespace aux

} // namespace sinks

} // namespace log

} // namespace boost

#ifdef _MSC_VER
#pragma warning(pop)
#endif // _MSC_VER

#endif // BOOST_LOG_SINKS_BASIC_SINK_FRONTEND_HPP_INCLUDED_
