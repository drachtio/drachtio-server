/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   unbounded_fifo_queue.hpp
 * \author Andrey Semashev
 * \date   24.07.2011
 *
 * The header contains implementation of unbounded FIFO queueing strategy for
 * the asynchronous sink frontend.
 */

#if (defined(_MSC_VER) && _MSC_VER > 1000)
#pragma once
#endif // _MSC_VER > 1000

#ifndef BOOST_LOG_SINKS_UNBOUNDED_FIFO_QUEUE_HPP_INCLUDED_
#define BOOST_LOG_SINKS_UNBOUNDED_FIFO_QUEUE_HPP_INCLUDED_

#include <boost/log/detail/prologue.hpp>

#if defined(BOOST_LOG_NO_THREADS)
#error Boost.Log: This header content is only supported in multithreaded environment
#endif

#include <boost/log/detail/event.hpp>
#include <boost/log/detail/threadsafe_queue.hpp>

namespace boost {

namespace BOOST_LOG_NAMESPACE {

namespace sinks {

namespace aux {

//! Unbounded FIFO log record strategy implementation
template< typename RecordT >
class unbounded_fifo_queue
{
private:
    typedef RecordT record_type;
    typedef boost::log::aux::threadsafe_queue< record_type > queue_type;

private:
    //! Thread-safe queue
    queue_type m_queue;
    //! Event object to block on
    boost::log::aux::event m_event;
    //! Interruption flag
    volatile bool m_interruption_requested; // TODO: make it atomic

protected:
    //! Default constructor
    unbounded_fifo_queue() : m_interruption_requested(false)
    {
    }
    //! Initializing constructor
    template< typename ArgsT >
    explicit unbounded_fifo_queue(ArgsT const&) : m_interruption_requested(false)
    {
    }

    //! Enqueues log record to the queue
    void enqueue(record_type const& rec)
    {
        m_queue.push(rec);
        m_event.set_signalled();
    }

    //! Attempts to enqueue log record to the queue
    bool try_enqueue(record_type const& rec)
    {
        // Assume the call never blocks
        enqueue(rec);
        return true;
    }

    //! Attempts to dequeue a log record ready for processing from the queue, does not block if the queue is empty
    bool try_dequeue_ready(record_type& rec)
    {
        return m_queue.try_pop(rec);
    }

    //! Attempts to dequeue log record from the queue, does not block if the queue is empty
    bool try_dequeue(record_type& rec)
    {
        return m_queue.try_pop(rec);
    }

    //! Dequeues log record from the queue, blocks if the queue is empty
    bool dequeue_ready(record_type& rec)
    {
        // Try the fast way first
        if (m_queue.try_pop(rec))
            return true;

        // Ok, we probably have to wait for new records
        while (true)
        {
            m_event.wait();
            if (m_interruption_requested)
            {
                m_interruption_requested = false;
                return false;
            }
            if (m_queue.try_pop(rec))
                return true;
        }
    }

    //! Wakes a thread possibly blocked in the \c dequeue method
    void interrupt_dequeue()
    {
        m_interruption_requested = true;
        m_event.set_signalled();
    }
};

} // namespace aux

/*!
 * \brief Unbounded FIFO log record queueing strategy
 *
 * The \c unbounded_fifo_queue class is intended to be used with
 * the \c asynchronous_sink frontend as a log record queueing strategy.
 *
 * This strategy implements the simplest logic of log record buffering between
 * threads: the queue has no limits and imposes no ordering over the queued
 * elements aside from the order in which they are enqueued.
 * Because of this the queue provides decent performance and scalability,
 * however if sink backends can't consume log records fast enough the queue
 * may grow uncontrollably. When this is an issue, it is recommended to
 * use one of the bounded strategies.
 */
struct unbounded_fifo_queue
{
    template< typename RecordT >
    struct frontend_base
    {
        typedef sinks::aux::unbounded_fifo_queue< RecordT > type;
    };
};

} // namespace sinks

} // namespace log

} // namespace boost

#endif // BOOST_LOG_SINKS_UNBOUNDED_FIFO_QUEUE_HPP_INCLUDED_
