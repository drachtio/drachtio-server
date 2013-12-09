/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   process_id.hpp
 * \author Andrey Semashev
 * \date   12.09.2009
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/libs/log/doc/log.html.
 */

#if (defined(_MSC_VER) && _MSC_VER > 1000)
#pragma once
#endif // _MSC_VER > 1000

#ifndef BOOST_LOG_DETAIL_PROCESS_ID_HPP_INCLUDED_
#define BOOST_LOG_DETAIL_PROCESS_ID_HPP_INCLUDED_

#include <iosfwd>
#include <boost/log/detail/prologue.hpp>
#include <boost/log/detail/id.hpp>

namespace boost {

namespace BOOST_LOG_NAMESPACE {

namespace aux {

    //! The process id descriptor
    struct process
    {
        typedef unsigned long native_type;
        typedef boost::log::aux::id< process > id;
    };

    namespace this_process {

        //! The function returns current process identifier
        BOOST_LOG_EXPORT process::id get_id();

    } // namespace this_process

    template< typename CharT, typename TraitsT >
    BOOST_LOG_EXPORT std::basic_ostream< CharT, TraitsT >&
    operator<< (std::basic_ostream< CharT, TraitsT >& strm, process::id const& pid);

} // namespace aux

} // namespace log

} // namespace boost

#endif // BOOST_LOG_DETAIL_PROCESS_ID_HPP_INCLUDED_
