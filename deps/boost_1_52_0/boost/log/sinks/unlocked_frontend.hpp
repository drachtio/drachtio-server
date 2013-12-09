/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   unlocked_frontend.hpp
 * \author Andrey Semashev
 * \date   14.07.2009
 *
 * The header contains declaration of an unlocked sink frontend.
 */

#if (defined(_MSC_VER) && _MSC_VER > 1000)
#pragma once
#endif // _MSC_VER > 1000

#ifndef BOOST_LOG_SINKS_UNLOCKED_FRONTEND_HPP_INCLUDED_
#define BOOST_LOG_SINKS_UNLOCKED_FRONTEND_HPP_INCLUDED_

#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/mpl/assert.hpp>
#include <boost/log/detail/prologue.hpp>
#include <boost/log/detail/parameter_tools.hpp>
#include <boost/log/detail/fake_mutex.hpp>
#include <boost/log/sinks/basic_sink_frontend.hpp>
#include <boost/log/sinks/frontend_requirements.hpp>

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

//! \cond
#define BOOST_LOG_SINK_CTOR_FORWARD_INTERNAL(z, n, types)\
    template< BOOST_PP_ENUM_PARAMS(n, typename T) >\
    explicit unlocked_sink(BOOST_PP_ENUM_BINARY_PARAMS(n, T, const& arg)) :\
        m_pBackend(boost::make_shared< sink_backend_type >(BOOST_PP_ENUM_PARAMS(n, arg))) {}
//! \endcond

/*!
 * \brief Non-blocking logging sink frontend
 *
 * The sink frontend does not perform thread synchronization and
 * simply passes logging records to the sink backend.
 */
template< typename SinkBackendT >
class unlocked_sink :
    public aux::make_sink_frontend_base< SinkBackendT >::type
{
    typedef typename aux::make_sink_frontend_base< SinkBackendT >::type base_type;

public:
    //! Sink implementation type
    typedef SinkBackendT sink_backend_type;
    //! \cond
    BOOST_MPL_ASSERT((has_requirement< typename sink_backend_type::frontend_requirements, concurrent_feeding >));
    //! \endcond

    typedef typename base_type::record_type record_type;
    typedef typename base_type::string_type string_type;

    //! Type of pointer to the backend
    typedef shared_ptr< sink_backend_type > locked_backend_ptr;

private:
    //! Pointer to the backend
    const shared_ptr< sink_backend_type > m_pBackend;

public:
    /*!
     * Default constructor. Constructs the sink backend instance.
     * Requires the backend to be default-constructible.
     */
    unlocked_sink() :
        m_pBackend(boost::make_shared< sink_backend_type >())
    {
    }
    /*!
     * Constructor attaches user-constructed backend instance
     *
     * \param backend Pointer to the backend instance
     *
     * \pre \a backend is not \c NULL.
     */
    explicit unlocked_sink(shared_ptr< sink_backend_type > const& backend) :
        m_pBackend(backend)
    {
    }

    // Constructors that pass arbitrary parameters to the backend constructor
    BOOST_LOG_PARAMETRIZED_CONSTRUCTORS_GEN(BOOST_LOG_SINK_CTOR_FORWARD_INTERNAL, ~)

    /*!
     * Locking accessor to the attached backend.
     *
     * \note Does not do any actual locking, provided only for intarface consistency
     *       with other frontends.
     */
    locked_backend_ptr locked_backend()
    {
        return m_pBackend;
    }

    /*!
     * Passes the log record to the backend
     */
    void consume(record_type const& record)
    {
        boost::log::aux::fake_mutex m;
        base_type::feed_record(record, m, *m_pBackend);
    }

    /*!
     * The method performs flushing of any internal buffers that may hold log records. The method
     * may take considerable time to complete and may block both the calling thread and threads
     * attempting to put new records into the sink while this call is in progress.
     */
    void flush()
    {
        boost::log::aux::fake_mutex m;
        base_type::flush_backend(m, *m_pBackend);
    }
};

#undef BOOST_LOG_SINK_CTOR_FORWARD_INTERNAL

} // namespace sinks

} // namespace log

} // namespace boost

#ifdef _MSC_VER
#pragma warning(pop)
#endif // _MSC_VER

#endif // BOOST_LOG_SINKS_UNLOCKED_FRONTEND_HPP_INCLUDED_
