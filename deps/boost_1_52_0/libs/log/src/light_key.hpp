/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   light_key.hpp
 * \author Andrey Semashev
 * \date   17.11.2007
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/libs/log/doc/log.html.
 */

#ifndef BOOST_LOG_LIGHT_KEY_HPP_INCLUDED_
#define BOOST_LOG_LIGHT_KEY_HPP_INCLUDED_

namespace boost {

namespace BOOST_LOG_NAMESPACE {

namespace aux {

    //! A lightweight compound to pass around string keys
    template< typename CharT, typename SizeT >
    struct light_key
    {
        const CharT* pKey;
        SizeT KeyLen;
        light_key(const CharT* p, SizeT len) : pKey(p), KeyLen(len) {}
    };

} // namespace aux

} // namespace log

} // namespace boost

#endif // BOOST_LOG_LIGHT_KEY_HPP_INCLUDED_
