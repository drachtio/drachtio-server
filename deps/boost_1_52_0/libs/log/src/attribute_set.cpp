/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   attribute_set.cpp
 * \author Andrey Semashev
 * \date   19.04.2007
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/libs/log/doc/log.html.
 */

#include <deque>
#include <boost/assert.hpp>
#include <boost/intrusive/options.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/link_mode.hpp>
#include <boost/intrusive/derivation_value_traits.hpp>
#include <boost/log/attributes/attribute_set.hpp>
#include "attribute_set_impl.hpp"

namespace boost {

namespace BOOST_LOG_NAMESPACE {

template< typename CharT >
inline basic_attribute_set< CharT >::node_base::node_base() :
    m_pPrev(NULL),
    m_pNext(NULL)
{
}

template< typename CharT >
inline basic_attribute_set< CharT >::node::node(key_type const& key, mapped_type const& data) :
    node_base(),
    m_Value(key, data)
{
}

//! Default constructor
template< typename CharT >
BOOST_LOG_EXPORT basic_attribute_set< CharT >::basic_attribute_set() :
    m_pImpl(new implementation())
{
}

//! Copy constructor
template< typename CharT >
BOOST_LOG_EXPORT basic_attribute_set< CharT >::basic_attribute_set(basic_attribute_set const& that) :
    m_pImpl(new implementation(*that.m_pImpl))
{
}

//! Destructor
template< typename CharT >
BOOST_LOG_EXPORT basic_attribute_set< CharT >::~basic_attribute_set()
{
    delete m_pImpl;
}

//! Assignment
template< typename CharT >
BOOST_LOG_EXPORT basic_attribute_set< CharT >& basic_attribute_set< CharT >::operator= (basic_attribute_set that)
{
    this->swap(that);
    return *this;
}

//  Iterator generators
template< typename CharT >
BOOST_LOG_EXPORT typename basic_attribute_set< CharT >::iterator basic_attribute_set< CharT >::begin()
{
    return m_pImpl->begin();
}
template< typename CharT >
BOOST_LOG_EXPORT typename basic_attribute_set< CharT >::iterator basic_attribute_set< CharT >::end()
{
    return m_pImpl->end();
}
template< typename CharT >
BOOST_LOG_EXPORT typename basic_attribute_set< CharT >::const_iterator basic_attribute_set< CharT >::begin() const
{
    return const_iterator(m_pImpl->begin());
}
template< typename CharT >
BOOST_LOG_EXPORT typename basic_attribute_set< CharT >::const_iterator basic_attribute_set< CharT >::end() const
{
    return const_iterator(m_pImpl->end());
}

//! The method returns number of elements in the container
template< typename CharT >
BOOST_LOG_EXPORT typename basic_attribute_set< CharT >::size_type basic_attribute_set< CharT >::size() const
{
    return m_pImpl->size();
}

//! Insertion method
template< typename CharT >
BOOST_LOG_EXPORT std::pair< typename basic_attribute_set< CharT >::iterator, bool >
basic_attribute_set< CharT >::insert(key_type key, mapped_type const& data)
{
    return m_pImpl->insert(key, data);
}

//! The method erases all attributes with the specified name
template< typename CharT >
BOOST_LOG_EXPORT typename basic_attribute_set< CharT >::size_type
basic_attribute_set< CharT >::erase(key_type key)
{
    iterator it = m_pImpl->find(key);
    if (it != end())
    {
        m_pImpl->erase(it);
        return 1;
    }
    else
        return 0;
}

//! The method erases the specified attribute
template< typename CharT >
BOOST_LOG_EXPORT void basic_attribute_set< CharT >::erase(iterator it)
{
    m_pImpl->erase(it);
}
//! The method erases all attributes within the specified range
template< typename CharT >
BOOST_LOG_EXPORT void basic_attribute_set< CharT >::erase(iterator begin, iterator end)
{
    while (begin != end)
    {
        m_pImpl->erase(begin++);
    }
}

//! The method clears the container
template< typename CharT >
BOOST_LOG_EXPORT void basic_attribute_set< CharT >::clear()
{
    m_pImpl->clear();
}

//! Internal lookup implementation
template< typename CharT >
BOOST_LOG_EXPORT typename basic_attribute_set< CharT >::iterator
basic_attribute_set< CharT >::find(key_type key)
{
    return m_pImpl->find(key);
}

#ifdef BOOST_LOG_USE_CHAR
template class basic_attribute_set< char >;
#endif
#ifdef BOOST_LOG_USE_WCHAR_T
template class basic_attribute_set< wchar_t >;
#endif

} // namespace log

} // namespace boost
