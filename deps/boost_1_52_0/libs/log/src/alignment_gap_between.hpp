/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   alignment_gap_between.hpp
 * \author Andrey Semashev
 * \date   20.11.2007
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/libs/log/doc/log.html.
 */

#ifndef BOOST_LOG_ALIGNMENT_GAP_BETWEEN_HPP_INCLUDED_
#define BOOST_LOG_ALIGNMENT_GAP_BETWEEN_HPP_INCLUDED_

namespace boost {

namespace BOOST_LOG_NAMESPACE {

namespace aux {

//! The metafunction computes the minimal gap between objects t1 and t2 of types T1 and T2
//! that would be needed to maintain the alignment of t2 if it's placed right after t1
template< typename T1, typename T2 >
struct alignment_gap_between
{
private:
    struct both
    {
        T1 t1;
        T2 t2;

        // To avoid warnings about inability to generate the destructor
        // if T1's or T2's destructor is inaccessible
        ~both();
    };

public:
    enum _ { value = sizeof(both) - (sizeof(T1) + sizeof(T2)) };
};

} // namespace aux

} // namespace log

} // namespace boost

#endif // BOOST_LOG_ALIGNMENT_GAP_BETWEEN_HPP_INCLUDED_
