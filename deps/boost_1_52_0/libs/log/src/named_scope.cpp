/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   named_scope.cpp
 * \author Andrey Semashev
 * \date   24.06.2007
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/libs/log/doc/log.html.
 */

#include <memory>
#include <algorithm>
#include <boost/optional.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/log/attributes/attribute.hpp>
#include <boost/log/attributes/attribute_value.hpp>
#include <boost/log/attributes/named_scope.hpp>
#include <boost/log/utility/type_dispatch/type_dispatcher.hpp>
#include <boost/log/detail/singleton.hpp>
#if !defined(BOOST_LOG_NO_THREADS)
#include <boost/thread/tss.hpp>
#endif

namespace boost {

namespace BOOST_LOG_NAMESPACE {

namespace attributes {

BOOST_LOG_ANONYMOUS_NAMESPACE {

    //! Actual implementation of the named scope list
    template< typename CharT >
    class basic_writeable_named_scope_list :
        public basic_named_scope_list< CharT >
    {
        //! Base type
        typedef basic_named_scope_list< CharT > base_type;

    public:
        //! Const reference type
        typedef typename base_type::const_reference const_reference;

    public:
        //! The method pushes the scope to the back of the list
        BOOST_LOG_FORCEINLINE void push_back(const_reference entry)
        {
            register aux::named_scope_list_node* top = this->m_RootNode._m_pPrev;
            entry._m_pPrev = top;
            entry._m_pNext = &this->m_RootNode;

            BOOST_LOG_ASSUME(&entry != 0);
            this->m_RootNode._m_pPrev = top->_m_pNext =
                const_cast< aux::named_scope_list_node* >(
                    static_cast< const aux::named_scope_list_node* >(&entry));

            ++this->m_Size;
        }
        //! The method removes the top scope entry from the list
        BOOST_LOG_FORCEINLINE void pop_back()
        {
            register aux::named_scope_list_node* top = this->m_RootNode._m_pPrev;
            top->_m_pPrev->_m_pNext = top->_m_pNext;
            top->_m_pNext->_m_pPrev = top->_m_pPrev;
            --this->m_Size;
        }
    };

    //! Named scope attribute value
    template< typename CharT >
    class basic_named_scope_value :
        public attribute_value::impl
    {
        //! Character type
        typedef CharT char_type;
        //! Scope names stack
        typedef basic_named_scope_list< char_type > scope_stack;

        //! Pointer to the actual scope value
        scope_stack* m_pValue;
        //! A thread-independent value
        optional< scope_stack > m_DetachedValue;

    public:
        //! Constructor
        explicit basic_named_scope_value(scope_stack* p) : m_pValue(p) {}

        //! The method dispatches the value to the given object. It returns true if the
        //! object was capable to consume the real attribute value type and false otherwise.
        bool dispatch(type_dispatcher& dispatcher)
        {
            type_dispatcher::callback< scope_stack > callback =
                dispatcher.get_callback< scope_stack >();
            if (callback)
            {
                callback(*m_pValue);
                return true;
            }
            else
                return false;
        }

        //! The method is called when the attribute value is passed to another thread (e.g.
        //! in case of asynchronous logging). The value should ensure it properly owns all thread-specific data.
        intrusive_ptr< attribute_value::impl > detach_from_thread()
        {
            if (!m_DetachedValue)
            {
                m_DetachedValue = *m_pValue;
                m_pValue = m_DetachedValue.get_ptr();
            }

            return this;
        }
    };

} // namespace

//! Named scope attribute implementation
template< typename CharT >
struct BOOST_LOG_VISIBLE basic_named_scope< CharT >::impl :
    public attribute::impl,
    public log::aux::singleton<
        impl,
        intrusive_ptr< impl >
    >
{
    //! Singleton base type
    typedef log::aux::singleton<
        impl,
        intrusive_ptr< impl >
    > singleton_base_type;

    //! Writable scope list type
    typedef basic_writeable_named_scope_list< char_type > scope_list;

#if !defined(BOOST_LOG_NO_THREADS)
    //! Pointer to the thread-specific scope stack
    thread_specific_ptr< scope_list > pScopes;

#if defined(BOOST_LOG_USE_COMPILER_TLS)
    //! Cached pointer to the thread-specific scope stack
    static BOOST_LOG_TLS scope_list* pScopesCache;
#endif

#else
    //! Pointer to the scope stack
    std::auto_ptr< scope_list > pScopes;
#endif

    //! The method returns current thread scope stack
    scope_list& get_scope_list()
    {
#if defined(BOOST_LOG_USE_COMPILER_TLS)
        register scope_list* p = pScopesCache;
#else
        register scope_list* p = pScopes.get();
#endif
        if (!p)
        {
            std::auto_ptr< scope_list > pNew(new scope_list());
            pScopes.reset(pNew.get());
#if defined(BOOST_LOG_USE_COMPILER_TLS)
            pScopesCache = p = pNew.release();
#else
            p = pNew.release();
#endif
        }

        return *p;
    }

    //! Instance initializer
    static void init_instance()
    {
        singleton_base_type::get_instance().reset(new impl());
    }

    //! The method returns the actual attribute value. It must not return NULL.
    attribute_value get_value()
    {
        return attribute_value(new basic_named_scope_value< char_type >(&get_scope_list()));
    }

private:
    impl() {}
};

#if defined(BOOST_LOG_USE_COMPILER_TLS)
//! Cached pointer to the thread-specific scope stack
template< typename CharT >
BOOST_LOG_TLS typename basic_named_scope< CharT >::implementation::scope_list*
basic_named_scope< CharT >::implementation::pScopesCache = NULL;
#endif // defined(BOOST_LOG_USE_COMPILER_TLS)


//! Copy constructor
template< typename CharT >
BOOST_LOG_EXPORT basic_named_scope_list< CharT >::basic_named_scope_list(basic_named_scope_list const& that) :
    allocator_type(static_cast< allocator_type const& >(that)),
    m_Size(that.size()),
    m_fNeedToDeallocate(!that.empty())
{
    if (m_Size > 0)
    {
        // Copy the container contents
        register pointer p = allocator_type::allocate(that.size());
        register aux::named_scope_list_node* prev = &m_RootNode;
        for (const_iterator src = that.begin(), end = that.end(); src != end; ++src, ++p)
        {
            allocator_type::construct(p, *src); // won't throw
            p->_m_pPrev = prev;
            prev->_m_pNext = p;
            prev = p;
        }
        m_RootNode._m_pPrev = prev;
        prev->_m_pNext = &m_RootNode;
    }
}

//! Destructor
template< typename CharT >
BOOST_LOG_EXPORT basic_named_scope_list< CharT >::~basic_named_scope_list()
{
    if (m_fNeedToDeallocate)
    {
        iterator it(m_RootNode._m_pNext);
        iterator end(&m_RootNode);
        while (it != end)
            allocator_type::destroy(&*(it++));
        allocator_type::deallocate(static_cast< pointer >(m_RootNode._m_pNext), m_Size);
    }
}

//! Swaps two instances of the container
template< typename CharT >
BOOST_LOG_EXPORT void basic_named_scope_list< CharT >::swap(basic_named_scope_list& that)
{
    using std::swap;

    unsigned int choice =
        static_cast< unsigned int >(this->empty()) | (static_cast< unsigned int >(that.empty()) << 1);
    switch (choice)
    {
    case 0: // both containers are not empty
        swap(m_RootNode._m_pNext->_m_pPrev, that.m_RootNode._m_pNext->_m_pPrev);
        swap(m_RootNode._m_pPrev->_m_pNext, that.m_RootNode._m_pPrev->_m_pNext);
        swap(m_RootNode, that.m_RootNode);
        swap(m_Size, that.m_Size);
        swap(m_fNeedToDeallocate, that.m_fNeedToDeallocate);
        break;

    case 1: // that is not empty
        that.m_RootNode._m_pNext->_m_pPrev = that.m_RootNode._m_pPrev->_m_pNext = &m_RootNode;
        m_RootNode = that.m_RootNode;
        that.m_RootNode._m_pNext = that.m_RootNode._m_pPrev = &that.m_RootNode;
        swap(m_Size, that.m_Size);
        swap(m_fNeedToDeallocate, that.m_fNeedToDeallocate);
        break;

    case 2: // this is not empty
        m_RootNode._m_pNext->_m_pPrev = m_RootNode._m_pPrev->_m_pNext = &that.m_RootNode;
        that.m_RootNode = m_RootNode;
        m_RootNode._m_pNext = m_RootNode._m_pPrev = &m_RootNode;
        swap(m_Size, that.m_Size);
        swap(m_fNeedToDeallocate, that.m_fNeedToDeallocate);
        break;

    default: // both containers are empty, nothing to do here
        ;
    }
}

//! Constructor
template< typename CharT >
basic_named_scope< CharT >::basic_named_scope() :
    attribute(impl::instance)
{
}

//! Constructor for casting support
template< typename CharT >
basic_named_scope< CharT >::basic_named_scope(cast_source const& source) :
    attribute(source.as< impl >())
{
}

//! The method pushes the scope to the stack
template< typename CharT >
void basic_named_scope< CharT >::push_scope(scope_entry const& entry)
{
    typename impl::scope_list& s = impl::instance->get_scope_list();
    s.push_back(entry);
}

//! The method pops the top scope
template< typename CharT >
void basic_named_scope< CharT >::pop_scope()
{
    typename impl::scope_list& s = impl::instance->get_scope_list();
    s.pop_back();
}

//! Returns the current thread's scope stack
template< typename CharT >
typename basic_named_scope< CharT >::value_type const& basic_named_scope< CharT >::get_scopes()
{
    return impl::instance->get_scope_list();
}

//! Explicitly instantiate named_scope implementation
#ifdef BOOST_LOG_USE_CHAR
template class BOOST_LOG_EXPORT basic_named_scope< char >;
template class basic_named_scope_list< char >;
#endif
#ifdef BOOST_LOG_USE_WCHAR_T
template class BOOST_LOG_EXPORT basic_named_scope< wchar_t >;
template class basic_named_scope_list< wchar_t >;
#endif

} // namespace attributes

} // namespace log

} // namespace boost
