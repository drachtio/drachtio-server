/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   attribute_mapping.hpp
 * \author Andrey Semashev
 * \date   07.11.2008
 *
 * The header contains facilities that are used in different sinks to map attribute values
 * used throughout the application to values used with the specific native logging API.
 * These tools are mostly needed to map application severity levels on native levels,
 * required by OS-specific sink backends.
 */

#if (defined(_MSC_VER) && _MSC_VER > 1000)
#pragma once
#endif // _MSC_VER > 1000

#ifndef BOOST_LOG_SINKS_ATTRIBUTE_MAPPING_HPP_INCLUDED_
#define BOOST_LOG_SINKS_ATTRIBUTE_MAPPING_HPP_INCLUDED_

#include <map>
#include <functional>
#include <boost/log/detail/prologue.hpp>
#include <boost/log/detail/tagged_integer.hpp>
#include <boost/log/core/record.hpp>
#include <boost/log/attributes/attribute_name.hpp>
#include <boost/log/attributes/attribute_values_view.hpp>
#include <boost/log/attributes/value_visitation.hpp>

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

//! Base class for attribute mapping function objects
template< typename CharT, typename MappedT >
struct basic_mapping :
    public std::unary_function< basic_attribute_values_view< CharT >, MappedT >
{
    //! Char type
    typedef CharT char_type;
    //! Attribute name type
    typedef basic_attribute_name< char_type > attribute_name_type;
    //! Attribute values view type
    typedef basic_attribute_values_view< char_type > values_view_type;
    //! Log record type
    typedef basic_record< char_type > record_type;
    //! Mapped value type
    typedef MappedT mapped_type;
};

namespace aux {

    //! Attribute value visitor
    template< typename MappedT >
    struct direct_mapping_visitor
    {
        typedef void result_type;
        typedef MappedT mapped_type;

        explicit direct_mapping_visitor(mapped_type& extracted) :
            m_Extracted(extracted)
        {
        }
        template< typename T >
        void operator() (T const& val) const
        {
            m_Extracted = mapped_type(val);
        }

    private:
        mapped_type& m_Extracted;
    };
    //  Specialization for the tagged integer
    template< typename IntT, typename TagT >
    struct direct_mapping_visitor< boost::log::aux::tagged_integer< IntT, TagT > >
    {
        typedef void result_type;
        typedef boost::log::aux::tagged_integer< IntT, TagT > mapped_type;

        explicit direct_mapping_visitor(mapped_type& extracted) :
            m_Extracted(extracted)
        {
        }
        template< typename T >
        void operator() (T const& val) const
        {
            mapped_type v = { val };
            m_Extracted = v;
        }

    private:
        mapped_type& m_Extracted;
    };

} // namespace aux

/*!
 * \brief Straightforward mapping
 *
 * This type of mapping assumes that attribute with a particular name always
 * provides values that map directly onto the native values. The mapping
 * simply returns the extracted attribute value converted to the native value.
 */
template< typename CharT, typename MappedT, typename AttributeValueT = int >
class basic_direct_mapping :
    public basic_mapping< CharT, MappedT >
{
    //! Base type
    typedef basic_direct_mapping< CharT, MappedT > base_type;

public:
    //! Attribute contained value type
    typedef AttributeValueT attribute_value_type;
    //! Char type
    typedef typename base_type::char_type char_type;
    //! Attribute name type
    typedef typename base_type::attribute_name_type attribute_name_type;
    //! Attribute values view type
    typedef typename base_type::values_view_type values_view_type;
    //! Log record type
    typedef typename base_type::record_type record_type;
    //! Mapped value type
    typedef typename base_type::mapped_type mapped_type;

private:
    //! Visitor invoker for the attribute value
    value_visitor_invoker< char_type, attribute_value_type > m_Invoker;
    //! Default native value
    mapped_type m_DefaultValue;

public:
    /*!
     * Constructor
     *
     * \param name Attribute name
     * \param default_value The default native value that is returned if the attribute value is not found
     */
    explicit basic_direct_mapping(attribute_name_type const& name, mapped_type const& default_value) :
        m_Invoker(name),
        m_DefaultValue(default_value)
    {
    }

    /*!
     * Extraction operator
     *
     * \param rec A log record to extract value from
     * \return An extracted attribute value
     */
    mapped_type operator() (record_type const& rec) const
    {
        mapped_type res = m_DefaultValue;
        aux::direct_mapping_visitor< mapped_type > vis(res);
        m_Invoker(rec.attribute_values(), vis);
        return res;
    }
};

/*!
 * \brief Customizable mapping
 *
 * The class allows to setup a custom mapping between an attribute and native values.
 * The mapping should be initialized similarly to the standard \c map container, by using
 * indexing operator and assignment.
 *
 * \note Unlike many other components of the library, exact type of the attribute value
 *       must be specified in the template parameter \c AttributeValueT. Type sequences
 *       are not supported.
 */
template< typename CharT, typename MappedT, typename AttributeValueT = int >
class basic_custom_mapping :
    public basic_mapping< CharT, MappedT >
{
    //! Base type
    typedef basic_mapping< CharT, MappedT > base_type;

public:
    //! Attribute contained value type
    typedef AttributeValueT attribute_value_type;
    //! Char type
    typedef typename base_type::char_type char_type;
    //! Attribute name type
    typedef typename base_type::attribute_name_type attribute_name_type;
    //! Attribute values view type
    typedef typename base_type::values_view_type values_view_type;
    //! Log record type
    typedef typename base_type::record_type record_type;
    //! Mapped value type
    typedef typename base_type::mapped_type mapped_type;

private:
    //! \cond

    //! Mapping type
    typedef std::map< attribute_value_type, mapped_type > mapping_type;
    //! Smart reference class for implementing insertion into the map
    class reference_proxy;
    friend class reference_proxy;
    class reference_proxy
    {
        mapping_type& m_Mapping;
        attribute_value_type m_Key;

    public:
        //! Constructor
        reference_proxy(mapping_type& mapping, attribute_value_type const& key) : m_Mapping(mapping), m_Key(key) {}
        //! Insertion
        reference_proxy const& operator= (mapped_type const& val) const
        {
            m_Mapping[m_Key] = val;
            return *this;
        }
    };

    //! Attribute value visitor
    struct visitor;
    friend struct visitor;
    struct visitor
    {
        typedef void result_type;

        visitor(mapping_type const& mapping, mapped_type& extracted) :
            m_Mapping(mapping),
            m_Extracted(extracted)
        {
        }
        template< typename T >
        void operator() (T const& val) const
        {
            typename mapping_type::const_iterator it = m_Mapping.find(val);
            if (it != m_Mapping.end())
                m_Extracted = it->second;
        }

    private:
        mapping_type const& m_Mapping;
        mapped_type& m_Extracted;
    };

    //! \endcond

private:
    //! Visitor invoker for the attribute value
    value_visitor_invoker< char_type, attribute_value_type > m_Invoker;
    //! Default native value
    mapped_type m_DefaultValue;
    //! Conversion mapping
    mapping_type m_Mapping;

public:
    /*!
     * Constructor
     *
     * \param name Attribute name
     * \param default_value The default native value that is returned if the conversion cannot be performed
     */
    explicit basic_custom_mapping(attribute_name_type const& name, mapped_type const& default_value) :
        m_Invoker(name),
        m_DefaultValue(default_value)
    {
    }
    /*!
     * Extraction operator. Extracts the attribute value and attempts to map it onto
     * the native value.
     *
     * \param rec A log record to extract value from
     * \return A mapped value, if mapping was successfull, or the default value if
     *         mapping did not succeed.
     */
    mapped_type operator() (record_type const& rec) const
    {
        mapped_type res = m_DefaultValue;
        visitor vis(m_Mapping, res);
        m_Invoker(rec.attribute_values(), vis);
        return res;
    }
    /*!
     * Insertion operator
     *
     * \param key Attribute value to be mapped
     * \return An object of unspecified type that allows to insert a new mapping through assignment.
     *         The \a key argument becomes the key attribute value, and the assigned value becomes the
     *         mapped native value.
     */
#ifndef BOOST_LOG_DOXYGEN_PASS
    reference_proxy operator[] (attribute_value_type const& key)
#else
    implementation_defined operator[] (attribute_value_type const& key)
#endif
    {
        return reference_proxy(m_Mapping, key);
    }
};

} // namespace sinks

} // namespace log

} // namespace boost

#ifdef _MSC_VER
#pragma warning(pop)
#endif // _MSC_VER

#endif // BOOST_LOG_SINKS_ATTRIBUTE_MAPPING_HPP_INCLUDED_
