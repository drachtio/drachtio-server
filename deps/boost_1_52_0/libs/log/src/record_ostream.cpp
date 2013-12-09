/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   record_ostream.cpp
 * \author Andrey Semashev
 * \date   17.04.2008
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/libs/log/doc/log.html.
 */

#include <memory>
#include <locale>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/detail/singleton.hpp>
#if !defined(BOOST_LOG_NO_THREADS)
#include <boost/thread/tss.hpp>
#endif

namespace boost {

namespace BOOST_LOG_NAMESPACE {

//! The function initializes the stream and the stream buffer
template< typename CharT, typename TraitsT >
BOOST_LOG_EXPORT void basic_record_ostream< CharT, TraitsT >::init_stream()
{
    if (!!m_Record)
    {
        ostream_buf_base_type::attach(m_Record.message());
        ostream_type::clear(ostream_type::goodbit);
        ostream_type::flags(
            ostream_type::dec |
            ostream_type::skipws |
            ostream_type::boolalpha // this differs from the default stream flags but makes logs look better
        );
        ostream_type::width(0);
        ostream_type::precision(6);
        ostream_type::fill(static_cast< char_type >(' '));
        ostream_type::imbue(std::locale());
    }
}
//! The function resets the stream into a detached (default initialized) state
template< typename CharT, typename TraitsT >
BOOST_LOG_EXPORT void basic_record_ostream< CharT, TraitsT >::detach_from_record()
{
    if (!!m_Record)
    {
        ostream_buf_base_type::detach();
        ostream_type::exceptions(ostream_type::goodbit);
        ostream_type::clear(ostream_type::badbit);
    }
}

namespace aux {

BOOST_LOG_ANONYMOUS_NAMESPACE {

//! The pool of stream compounds
template< typename CharT >
class stream_compound_pool :
    public log::aux::lazy_singleton<
        stream_compound_pool< CharT >,
#if !defined(BOOST_LOG_NO_THREADS)
        thread_specific_ptr< stream_compound_pool< CharT > >
#else
        std::auto_ptr< stream_compound_pool< CharT > >
#endif
    >
{
    //! Self type
    typedef stream_compound_pool< CharT > this_type;
#if !defined(BOOST_LOG_NO_THREADS)
    //! Thread-specific pointer type
    typedef thread_specific_ptr< this_type > tls_ptr_type;
#else
    //! Thread-specific pointer type
    typedef std::auto_ptr< this_type > tls_ptr_type;
#endif
    //! Singleton base type
    typedef log::aux::lazy_singleton<
        this_type,
        tls_ptr_type
    > base_type;
    //! Stream compound type
    typedef typename stream_provider< CharT >::stream_compound stream_compound_t;

public:
    //! Pooled stream compounds
    stream_compound_t* m_Top;

    ~stream_compound_pool()
    {
        register stream_compound_t* p = NULL;
        while ((p = m_Top) != NULL)
        {
            m_Top = p->next;
            delete p;
        }
    }

    //! The method returns pool instance
    static stream_compound_pool& get()
    {
        tls_ptr_type& ptr = base_type::get();
        register this_type* p = ptr.get();
        if (!p)
        {
            std::auto_ptr< this_type > pNew(new this_type());
            ptr.reset(pNew.get());
            p = pNew.release();
        }
        return *p;
    }

private:
    stream_compound_pool() : m_Top(NULL) {}
};

} // namespace

//! The method returns an allocated stream compound
template< typename CharT >
BOOST_LOG_EXPORT typename stream_provider< CharT >::stream_compound*
stream_provider< CharT >::allocate_compound(record_type const& rec)
{
    stream_compound_pool< char_type >& pool = stream_compound_pool< char_type >::get();
    if (pool.m_Top)
    {
        register stream_compound* p = pool.m_Top;
        pool.m_Top = p->next;
        p->next = NULL;
        p->stream.record(rec);
        return p;
    }
    else
        return new stream_compound(rec);
}

//! The method releases a compound
template< typename CharT >
BOOST_LOG_EXPORT void stream_provider< CharT >::release_compound(stream_compound* compound) /* throw() */
{
    stream_compound_pool< char_type >& pool = stream_compound_pool< char_type >::get();
    compound->next = pool.m_Top;
    pool.m_Top = compound;
    compound->stream.record(record_type());
}

//! Explicitly instantiate stream_provider implementation
#ifdef BOOST_LOG_USE_CHAR
template struct stream_provider< char >;
#endif
#ifdef BOOST_LOG_USE_WCHAR_T
template struct stream_provider< wchar_t >;
#endif

} // namespace aux

//! Explicitly instantiate basic_record_ostream implementation
#ifdef BOOST_LOG_USE_CHAR
template class basic_record_ostream< char, std::char_traits< char > >;
#endif
#ifdef BOOST_LOG_USE_WCHAR_T
template class basic_record_ostream< wchar_t, std::char_traits< wchar_t > >;
#endif

} // namespace log

} // namespace boost
