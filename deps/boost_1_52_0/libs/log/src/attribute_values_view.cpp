/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   attribute_values_view.cpp
 * \author Andrey Semashev
 * \date   19.04.2007
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/libs/log/doc/log.html.
 */

#include <new>
#include <boost/array.hpp>
#include <boost/intrusive/options.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/link_mode.hpp>
#include <boost/intrusive/derivation_value_traits.hpp>
#include <boost/log/attributes/attribute_name.hpp>
#include <boost/log/attributes/attribute_value.hpp>
#include <boost/log/attributes/attribute_values_view.hpp>
#include "alignment_gap_between.hpp"
#include "attribute_set_impl.hpp"

namespace boost {

namespace BOOST_LOG_NAMESPACE {

template< typename CharT >
BOOST_LOG_FORCEINLINE basic_attribute_values_view< CharT >::node_base::node_base() :
    m_pPrev(NULL),
    m_pNext(NULL)
{
}

template< typename CharT >
BOOST_LOG_FORCEINLINE basic_attribute_values_view< CharT >::node::node(key_type const& key, mapped_type& data) :
    node_base(),
    m_Value(key, mapped_type())
{
    m_Value.second.swap(data);
}

//! Container implementation
template< typename CharT >
struct basic_attribute_values_view< CharT >::implementation
{
public:
    typedef typename key_type::id_type id_type;

private:
    typedef typename attribute_set_type::implementation attribute_set_impl_type;

    //! Node base class traits for the intrusive list
    struct node_traits
    {
        typedef node_base node;
        typedef node* node_ptr;
        typedef node const* const_node_ptr;
        static node* get_next(const node* n) { return n->m_pNext; }
        static void set_next(node* n, node* next) { n->m_pNext = next; }
        static node* get_previous(const node* n) { return n->m_pPrev; }
        static void set_previous(node* n, node* prev) { n->m_pPrev = prev; }
    };

    //! Contained node traits for the intrusive list
    typedef intrusive::derivation_value_traits<
        node,
        node_traits,
        intrusive::normal_link
    > value_traits;

    //! A container that provides iteration through elements of the container
    typedef intrusive::list<
        node,
        intrusive::value_traits< value_traits >,
        intrusive::constant_time_size< false >
    > node_list;

    //! A hash table bucket
    struct bucket
    {
        //! Points to the first element in the bucket
        node* first;
        //! Points to the last element in the bucket (not the one after the last!)
        node* last;

        bucket() : first(NULL), last(NULL) {}
    };

    //! A list of buckets
    typedef array< bucket, 1U << BOOST_LOG_HASH_TABLE_SIZE_LOG > buckets;

    //! Element disposer
    struct disposer
    {
        typedef void result_type;
        void operator() (node* p) const
        {
            p->~node();
        }
    };

private:
    //! Pointer to the source-specific attributes
    attribute_set_impl_type* m_pSourceAttributes;
    //! Pointer to the thread-specific attributes
    attribute_set_impl_type* m_pThreadAttributes;
    //! Pointer to the global attributes
    attribute_set_impl_type* m_pGlobalAttributes;

    //! The container with elements
    node_list m_Nodes;
    //! The pointer to the beginning of the storage of the elements
    node* m_pStorage;
    //! The pointer to the end of the allocated elements within the storage
    node* m_pEnd;
    //! The pointer to the end of storage
    node* m_pEOS;

    //! Hash table buckets
    buckets m_Buckets;

private:
    //! Constructor
    implementation(
        node* storage,
        node* eos,
        attribute_set_impl_type* source_attrs,
        attribute_set_impl_type* thread_attrs,
        attribute_set_impl_type* global_attrs
    ) :
        m_pSourceAttributes(source_attrs),
        m_pThreadAttributes(thread_attrs),
        m_pGlobalAttributes(global_attrs),
        m_pStorage(storage),
        m_pEnd(storage),
        m_pEOS(eos)
    {
    }

    //! Destructor
    ~implementation()
    {
        m_Nodes.clear_and_dispose(disposer());
    }

    //! The function allocates memory and creates the object
    static implementation* create(
        internal_allocator_type& alloc,
        size_type element_count,
        attribute_set_impl_type* source_attrs,
        attribute_set_impl_type* thread_attrs,
        attribute_set_impl_type* global_attrs)
    {
        // Calculate the buffer size
        const size_type header_size = sizeof(implementation) +
            aux::alignment_gap_between< implementation, node >::value;
        const size_type buffer_size = header_size + element_count * sizeof(node);

        implementation* p = reinterpret_cast< implementation* >(alloc.allocate(buffer_size));
        node* const storage = reinterpret_cast< node* >(reinterpret_cast< char* >(p) + header_size);
        new (p) implementation(storage, storage + element_count, source_attrs, thread_attrs, global_attrs);

        return p;
    }

public:
    //! The function allocates memory and creates the object
    static implementation* create(
        internal_allocator_type& alloc,
        attribute_set_type const& source_attrs,
        attribute_set_type const& thread_attrs,
        attribute_set_type const& global_attrs)
    {
        return create(
            alloc,
            source_attrs.m_pImpl->size() + thread_attrs.m_pImpl->size() + global_attrs.m_pImpl->size(),
            source_attrs.m_pImpl,
            thread_attrs.m_pImpl,
            global_attrs.m_pImpl);
    }

    //! Creates a copy of the object
    static implementation* copy(internal_allocator_type& alloc, implementation* that)
    {
        // Create new object
        implementation* p = create(alloc, that->size(), NULL, NULL, NULL);

        // Copy all elements
        typename node_list::iterator it = that->m_Nodes.begin(), end = that->m_Nodes.end();
        for (; it != end; ++it)
        {
            node* n = p->m_pEnd++;
            mapped_type data = it->m_Value.second;
            new (n) node(it->m_Value.first, data);
            p->m_Nodes.push_back(*n);

            // Since nodes within buckets are ordered, we can simply append the node to the end of the bucket
            bucket& b = p->get_bucket(n->m_Value.first.id());
            if (b.first == NULL)
                b.first = b.last = n;
            else
                b.last = n;
        }

        return p;
    }

    //! Destroys the object and releases the memory
    static void destroy(internal_allocator_type& alloc, implementation* p)
    {
        const size_type buffer_size = reinterpret_cast< char* >(p->m_pEOS) - reinterpret_cast< char* >(p);
        p->~implementation();
        alloc.deallocate(reinterpret_cast< typename internal_allocator_type::pointer >(p), buffer_size);
    }

    //! Returns the pointer to the first element
    node_base* begin()
    {
        freeze();
        return m_Nodes.begin().pointed_node();
    }
    //! Returns the pointer after the last element
    node_base* end()
    {
        return m_Nodes.end().pointed_node();
    }

    //! Returns the number of elements in the container
    size_type size()
    {
        freeze();
        return (m_pEnd - m_pStorage);
    }

    //! Looks for the element with an equivalent key
    node_base* find(key_type key)
    {
        // First try to find an acquired element
        bucket& b = get_bucket(key.id());
        register node* p = b.first;
        if (p)
        {
            // The bucket is not empty, search among the elements
            p = find_in_bucket(key, b);
            if (p->m_Value.first == key)
                return p;
        }

        // Element not found, try to acquire the value from attribute sets, if not frozen yet
        if (m_pSourceAttributes)
            return freeze_node(key, b, p);
        else
            return m_Nodes.end().pointed_node();
    }

    //! Freezes all elements of the container
    void freeze()
    {
        if (m_pSourceAttributes)
        {
            freeze_nodes_from(m_pSourceAttributes);
            freeze_nodes_from(m_pThreadAttributes);
            freeze_nodes_from(m_pGlobalAttributes);
            m_pSourceAttributes = m_pThreadAttributes = m_pGlobalAttributes = NULL;
        }
    }

private:
    //! The function returns a bucket for the specified element
    bucket& get_bucket(id_type id)
    {
        return m_Buckets[id & (buckets::static_size - 1)];
    }

    //! Attempts to find an element with the specified key in the bucket
    node* find_in_bucket(key_type key, bucket const& b)
    {
        typedef typename node_list::node_traits node_traits;
        typedef typename node_list::value_traits value_traits;

        // All elements within the bucket are sorted to speedup the search.
        register node* p = b.first;
        while (p != b.last && p->m_Value.first.id() < key.id())
        {
            p = value_traits::to_value_ptr(node_traits::get_next(p));
        }

        return p;
    }

    //! Acquires the attribute value from the attribute sets
    node_base* freeze_node(key_type key, bucket& b, node* where)
    {
        typename attribute_set_type::iterator it = m_pSourceAttributes->find(key);
        if (it == m_pSourceAttributes->end())
        {
            it = m_pThreadAttributes->find(key);
            if (it == m_pThreadAttributes->end())
            {
                it = m_pGlobalAttributes->find(key);
                if (it == m_pGlobalAttributes->end())
                {
                    // The attribute is not found
                    return m_Nodes.end().pointed_node();
                }
            }
        }

        // The attribute is found, acquiring the value
        return insert_node(key, b, where, it->second.get_value());
    }

    //! The function inserts a node into the container
    node* insert_node(key_type key, bucket& b, node* where, mapped_type data)
    {
        node* const p = m_pEnd++;
        new (p) node(key, data);

        if (b.first == NULL)
        {
            // The bucket is empty
            b.first = b.last = p;
            m_Nodes.push_back(*p);
        }
        else if (where == b.last && key.id() > where->m_Value.first.id())
        {
            // The new element should become the last element of the bucket
            typename node_list::iterator it = m_Nodes.iterator_to(*where);
            ++it;
            m_Nodes.insert(it, *p);
            b.last = p;
        }
        else
        {
            // The new element should be within the bucket
            typename node_list::iterator it = m_Nodes.iterator_to(*where);
            m_Nodes.insert(it, *p);
        }

        return p;
    }

    //! Acquires attribute values from the set of attributes
    void freeze_nodes_from(attribute_set_impl_type* attrs)
    {
        typename attribute_set_type::const_iterator
            it = attrs->begin(), end = attrs->end();
        for (; it != end; ++it)
        {
            key_type key = it->first;
            bucket& b = get_bucket(key.id());
            register node* p = b.first;
            if (p)
            {
                p = find_in_bucket(key, b);
                if (p->m_Value.first == key)
                    continue; // the element is already frozen
            }

            insert_node(key, b, p, it->second.get_value());
        }
    }
};

#ifdef _MSC_VER
#pragma warning(push)
// 'this' : used in base member initializer list
#pragma warning(disable: 4355)
#endif

//! The constructor adopts three attribute sets to the view
template< typename CharT >
BOOST_LOG_EXPORT basic_attribute_values_view< CharT >::basic_attribute_values_view(
    attribute_set_type const& source_attrs,
    attribute_set_type const& thread_attrs,
    attribute_set_type const& global_attrs
) :
    m_pImpl(implementation::create(
        static_cast< internal_allocator_type& >(*this), source_attrs, thread_attrs, global_attrs))
{
}

//! Copy constructor
template< typename CharT >
BOOST_LOG_EXPORT basic_attribute_values_view< CharT >::basic_attribute_values_view(basic_attribute_values_view const& that) :
    internal_allocator_type(static_cast< internal_allocator_type const& >(that))
{
    if (that.m_pImpl)
        m_pImpl = implementation::copy(static_cast< internal_allocator_type& >(*this), that.m_pImpl);
    else
        m_pImpl = NULL;
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif

//! Destructor
template< typename CharT >
BOOST_LOG_EXPORT basic_attribute_values_view< CharT >::~basic_attribute_values_view()
{
    if (m_pImpl)
        implementation::destroy(static_cast< internal_allocator_type& >(*this), m_pImpl);
}

//! Assignment
template< typename CharT >
BOOST_LOG_EXPORT basic_attribute_values_view< CharT >&
basic_attribute_values_view< CharT >::operator= (basic_attribute_values_view that)
{
    swap(that);
    return *this;
}

//  Iterator generators
template< typename CharT >
BOOST_LOG_EXPORT typename basic_attribute_values_view< CharT >::const_iterator
basic_attribute_values_view< CharT >::begin() const
{
    return const_iterator(m_pImpl->begin(), const_cast< basic_attribute_values_view* >(this));
}

template< typename CharT >
BOOST_LOG_EXPORT typename basic_attribute_values_view< CharT >::const_iterator
basic_attribute_values_view< CharT >::end() const
{
    return const_iterator(m_pImpl->end(), const_cast< basic_attribute_values_view* >(this));
}

//! The method returns number of elements in the container
template< typename CharT >
BOOST_LOG_EXPORT typename basic_attribute_values_view< CharT >::size_type
basic_attribute_values_view< CharT >::size() const
{
    return m_pImpl->size();
}

//! Internal lookup implementation
template< typename CharT >
BOOST_LOG_EXPORT typename basic_attribute_values_view< CharT >::const_iterator
basic_attribute_values_view< CharT >::find(key_type key) const
{
    return const_iterator(m_pImpl->find(key), const_cast< basic_attribute_values_view* >(this));
}

//! The method acquires values of all adopted attributes. Users don't need to call it, since will always get an already frozen view.
template< typename CharT >
BOOST_LOG_EXPORT void basic_attribute_values_view< CharT >::freeze()
{
    m_pImpl->freeze();
}

//! Explicitly instantiate container implementation
#ifdef BOOST_LOG_USE_CHAR
template class basic_attribute_values_view< char >;
#endif
#ifdef BOOST_LOG_USE_WCHAR_T
template class basic_attribute_values_view< wchar_t >;
#endif

} // namespace log

} // namespace boost
