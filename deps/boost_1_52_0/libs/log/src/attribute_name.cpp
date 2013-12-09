/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   attribute_name.cpp
 * \author Andrey Semashev
 * \date   28.06.2010
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/libs/log/doc/log.html.
 */

#include <deque>
#include <ostream>
#include <boost/assert.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/intrusive/set.hpp>
#include <boost/intrusive/set_hook.hpp>
#include <boost/intrusive/options.hpp>
#include <boost/log/detail/singleton.hpp>
#include <boost/log/attributes/attribute_name.hpp>
#if !defined(BOOST_LOG_NO_THREADS)
#include <boost/log/detail/locks.hpp>
#include <boost/log/detail/light_rw_mutex.hpp>
#endif

namespace boost {

namespace BOOST_LOG_NAMESPACE {

BOOST_LOG_ANONYMOUS_NAMESPACE {

//! A global container of all known attribute names
template< typename CharT >
class attribute_name_repository :
    public log::aux::lazy_singleton<
        attribute_name_repository< CharT >,
        shared_ptr< attribute_name_repository< CharT > >
    >
{
    typedef log::aux::lazy_singleton<
        attribute_name_repository< CharT >,
        shared_ptr< attribute_name_repository< CharT > >
    > base_type;

#if !defined(BOOST_LOG_BROKEN_FRIEND_TEMPLATE_INSTANTIATIONS)
    friend class log::aux::lazy_singleton<
        attribute_name_repository< CharT >,
        shared_ptr< attribute_name_repository< CharT > >
    >;
#else
    friend class base_type;
#endif

public:
    //  Import types from the basic_attribute_name template
    typedef CharT char_type;
    typedef typename basic_attribute_name< char_type >::id_type id_type;
    typedef typename basic_attribute_name< char_type >::string_type string_type;

    //! A base hook for arranging the attribute names into a set
    typedef intrusive::set_base_hook<
        intrusive::link_mode< intrusive::safe_link >,
        intrusive::optimize_size< true >
    > node_by_name_hook;

private:
    //! An element of the attribute names repository
    struct node :
        public node_by_name_hook
    {
        typedef node_by_name_hook base_type;

    public:
        //! A predicate for name-based ordering
        struct order_by_name
        {
            typedef bool result_type;
            typedef typename string_type::traits_type traits_type;

            bool operator() (node const& left, node const& right) const
            {
                // Include terminating 0 into comparison to also check the length match
                return traits_type::compare(
                    left.m_name.c_str(), right.m_name.c_str(), left.m_name.size() + 1) < 0;
            }
            bool operator() (node const& left, const char_type* right) const
            {
                // Include terminating 0 into comparison to also check the length match
                return traits_type::compare(left.m_name.c_str(), right, left.m_name.size() + 1) < 0;
            }
            bool operator() (const char_type* left, node const& right) const
            {
                // Include terminating 0 into comparison to also check the length match
                return traits_type::compare(left, right.m_name.c_str(), right.m_name.size() + 1) < 0;
            }
        };

    public:
        id_type m_id;
        string_type m_name;

    public:
        node() : m_id(0), m_name() {}
        node(id_type i, string_type const& n) :
            base_type(),
            m_id(i),
            m_name(n)
        {
        }
        node(node const& that) :
            base_type(),
            m_id(that.m_id),
            m_name(that.m_name)
        {
        }
    };

    //! The container that provides storage for nodes
    typedef std::deque< node > node_list;
    //! The conainer that provides name-based lookup
    typedef intrusive::set<
        node,
        intrusive::base_hook< node_by_name_hook >,
        intrusive::constant_time_size< false >,
        intrusive::compare< typename node::order_by_name >
    > node_set;

private:
#if !defined(BOOST_LOG_NO_THREADS)
    typedef log::aux::light_rw_mutex mutex_type;
    log::aux::light_rw_mutex m_Mutex;
#endif
    node_list m_NodeList;
    node_set m_NodeSet;

public:
    //! Converts attribute name string to id
    id_type get_id_from_string(const char_type* name)
    {
        BOOST_ASSERT(name != NULL);

#if !defined(BOOST_LOG_NO_THREADS)
        {
            // Do a non-blocking lookup first
            log::aux::shared_lock_guard< mutex_type > _(m_Mutex);
            typename node_set::const_iterator it =
                m_NodeSet.find(name, typename node::order_by_name());
            if (it != m_NodeSet.end())
                return it->m_id;
        }
#endif // !defined(BOOST_LOG_NO_THREADS)

        BOOST_LOG_EXPR_IF_MT(log::aux::exclusive_lock_guard< mutex_type > _(m_Mutex);)
        typename node_set::iterator it =
            m_NodeSet.lower_bound(name, typename node::order_by_name());
        if (it == m_NodeSet.end() || it->m_name != name)
        {
            m_NodeList.push_back(node(static_cast< id_type >(m_NodeList.size()), name));
            it = m_NodeSet.insert(it, m_NodeList.back());
        }
        return it->m_id;
    }

    //! Converts id to the attribute name string
    string_type const& get_string_from_id(id_type id)
    {
        BOOST_LOG_EXPR_IF_MT(log::aux::shared_lock_guard< mutex_type > _(m_Mutex);)
        BOOST_ASSERT(id < m_NodeList.size());
        return m_NodeList[id].m_name;
    }

private:
    //! Initializes the singleton instance
    static void init_instance()
    {
        base_type::get_instance() = boost::make_shared< attribute_name_repository >();
    }
};

} // namespace

template< typename CharT >
BOOST_LOG_EXPORT typename basic_attribute_name< CharT >::id_type
basic_attribute_name< CharT >::get_id_from_string(const char_type* name)
{
    typedef attribute_name_repository< char_type > repository;
    return repository::get()->get_id_from_string(name);
}

template< typename CharT >
BOOST_LOG_EXPORT typename basic_attribute_name< CharT >::string_type const&
basic_attribute_name< CharT >::get_string_from_id(id_type id)
{
    typedef attribute_name_repository< char_type > repository;
    return repository::get()->get_string_from_id(id);
}

template< typename CharT, typename TraitsT >
std::basic_ostream< CharT, TraitsT >& operator<< (
    std::basic_ostream< CharT, TraitsT >& strm,
    basic_attribute_name< CharT > const& name)
{
    if (!!name)
        strm << name.string();
    else
        strm << "[uninitialized]";
    return strm;
}

//  Explicitly instantiate attribute name implementation
#ifdef BOOST_LOG_USE_CHAR
template class basic_attribute_name< char >;
template BOOST_LOG_EXPORT std::basic_ostream< char, std::char_traits< char > >&
    operator<< < char, std::char_traits< char > >(
        std::basic_ostream< char, std::char_traits< char > >& strm,
        basic_attribute_name< char > const& name);
#endif
#ifdef BOOST_LOG_USE_WCHAR_T
template class basic_attribute_name< wchar_t >;
template BOOST_LOG_EXPORT std::basic_ostream< wchar_t, std::char_traits< wchar_t > >&
    operator<< < wchar_t, std::char_traits< wchar_t > >(
        std::basic_ostream< wchar_t, std::char_traits< wchar_t > >& strm,
        basic_attribute_name< wchar_t > const& name);
#endif

} // namespace log

} // namespace boost

