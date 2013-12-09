/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   light_function.hpp
 * \author Andrey Semashev
 * \date   20.06.2010
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/libs/log/doc/log.html.
 *
 * The file contains a lightweight alternative of Boost.Function. It does not provide all
 * features of Boost.Function but doesn't introduce dependency on Boost.Bind.
 */

#ifndef BOOST_LOG_DETAIL_LIGHT_FUNCTION_HPP_INCLUDED_
#define BOOST_LOG_DETAIL_LIGHT_FUNCTION_HPP_INCLUDED_

#include <cstddef>
#include <boost/move/move.hpp>
#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/iteration/iterate.hpp>
#include <boost/preprocessor/repetition/enum_params.hpp>
#include <boost/preprocessor/repetition/enum_binary_params.hpp>
#include <boost/preprocessor/repetition/enum_trailing_params.hpp>
#include <boost/preprocessor/repetition/enum_trailing_binary_params.hpp>
#include <boost/log/detail/prologue.hpp>
#include <boost/log/utility/explicit_operator_bool.hpp>
#if defined(BOOST_NO_RVALUE_REFERENCES) || defined(BOOST_NO_CXX11_RVALUE_REFERENCES)
#include <boost/utility/enable_if.hpp>
#include <boost/type_traits/is_same.hpp>
#include <boost/type_traits/remove_cv.hpp>
#endif
#if defined(BOOST_NO_NULLPTR) || defined(BOOST_NO_CXX11_NULLPTR)
#include <boost/assert.hpp>
#endif

#ifndef BOOST_LOG_LIGHT_FUNCTION_LIMIT
#define BOOST_LOG_LIGHT_FUNCTION_LIMIT 2
#endif

namespace boost {

namespace BOOST_LOG_NAMESPACE {

namespace aux {

#define BOOST_PP_FILENAME_1 <boost/log/detail/light_function.hpp>
#define BOOST_PP_ITERATION_LIMITS (0, BOOST_LOG_LIGHT_FUNCTION_LIMIT)
#include BOOST_PP_ITERATE()

} // namespace aux

} // namespace log

} // namespace boost

#endif // BOOST_LOG_DETAIL_LIGHT_FUNCTION_HPP_INCLUDED_

#ifdef BOOST_PP_IS_ITERATING

#define BOOST_LOG_LWFUNCTION_NAME BOOST_PP_CAT(light_function, BOOST_PP_ITERATION())

template<
    typename ResultT
    BOOST_PP_ENUM_TRAILING_PARAMS(BOOST_PP_ITERATION(), typename ArgT)
>
class BOOST_LOG_LWFUNCTION_NAME
{
    typedef BOOST_LOG_LWFUNCTION_NAME this_type;
    BOOST_COPYABLE_AND_MOVABLE(this_type)

public:
    typedef ResultT result_type;

private:
    struct implementation_base
    {
        typedef result_type (*invoke_type)(implementation_base* BOOST_PP_ENUM_TRAILING_PARAMS(BOOST_PP_ITERATION(), ArgT));
        const invoke_type invoke;

        typedef implementation_base* (*clone_type)(const implementation_base*);
        const clone_type clone;

        typedef void (*destroy_type)(implementation_base*);
        const destroy_type destroy;

        implementation_base(invoke_type inv, clone_type cl, destroy_type dstr) : invoke(inv), clone(cl), destroy(dstr)
        {
        }
    };

#if !defined(BOOST_LOG_NO_MEMBER_TEMPLATE_FRIENDS)
    template< typename FunT >
    class implementation;
    template< typename FunT >
    friend class implementation;
#endif

    template< typename FunT >
    class implementation :
        public implementation_base
    {
        FunT m_Function;

    public:
        explicit implementation(FunT const& fun) :
            implementation_base(&implementation::invoke_impl, &implementation::clone_impl, &implementation::destroy_impl),
            m_Function(fun)
        {
        }

        static void destroy_impl(implementation_base* self)
        {
            delete static_cast< implementation* >(self);
        }
        static implementation_base* clone_impl(const implementation_base* self)
        {
            return new implementation(static_cast< const implementation* >(self)->m_Function);
        }
        static result_type invoke_impl(implementation_base* self BOOST_PP_ENUM_TRAILING_BINARY_PARAMS(BOOST_PP_ITERATION(), ArgT, arg))
        {
            return static_cast< implementation* >(self)->m_Function(BOOST_PP_ENUM_PARAMS(BOOST_PP_ITERATION(), arg));
        }
    };

private:
    implementation_base* m_pImpl;

public:
    BOOST_LOG_LWFUNCTION_NAME() : m_pImpl(NULL)
    {
    }
    BOOST_LOG_LWFUNCTION_NAME(this_type const& that)
    {
        if (that.m_pImpl)
            m_pImpl = that.m_pImpl->clone(that.m_pImpl);
        else
            m_pImpl = NULL;
    }
    BOOST_LOG_LWFUNCTION_NAME(BOOST_RV_REF(this_type) that)
    {
        m_pImpl = that.m_pImpl;
        that.m_pImpl = NULL;
    }
#if !defined(BOOST_NO_RVALUE_REFERENCES) && !defined(BOOST_NO_CXX11_RVALUE_REFERENCES)
    template< typename FunT >
    BOOST_LOG_LWFUNCTION_NAME(FunT const& fun)
#else
    template< typename FunT >
    BOOST_LOG_LWFUNCTION_NAME(FunT const& fun, typename disable_if< move_detail::is_rv< FunT >, int >::type = 0)
#endif
        : m_pImpl(new implementation< FunT >(fun))
    {
    }
    //! Constructor from NULL
#if !defined(BOOST_NO_NULLPTR) && !defined(BOOST_NO_CXX11_NULLPTR)
    BOOST_LOG_LWFUNCTION_NAME(std::nullptr_t)
#else
    BOOST_LOG_LWFUNCTION_NAME(int p)
#endif
        : m_pImpl(NULL)
    {
#if defined(BOOST_NO_NULLPTR) || defined(BOOST_NO_CXX11_NULLPTR)
        BOOST_ASSERT(p == 0);
#endif
    }
    ~BOOST_LOG_LWFUNCTION_NAME()
    {
        clear();
    }

    BOOST_LOG_LWFUNCTION_NAME& operator= (BOOST_RV_REF(this_type) that)
    {
        this->swap(that);
        return *this;
    }
    BOOST_LOG_LWFUNCTION_NAME& operator= (BOOST_COPY_ASSIGN_REF(this_type) that)
    {
        BOOST_LOG_LWFUNCTION_NAME tmp = that;
        this->swap(tmp);
        return *this;
    }
    //! Assignment of NULL
#if !defined(BOOST_NO_NULLPTR) && !defined(BOOST_NO_CXX11_NULLPTR)
    BOOST_LOG_LWFUNCTION_NAME& operator= (std::nullptr_t)
#else
    BOOST_LOG_LWFUNCTION_NAME& operator= (int p)
#endif
    {
#if defined(BOOST_NO_NULLPTR) || defined(BOOST_NO_CXX11_NULLPTR)
        BOOST_ASSERT(p == 0);
#endif
        clear();
        return *this;
    }
#if !defined(BOOST_NO_RVALUE_REFERENCES) && !defined(BOOST_NO_CXX11_RVALUE_REFERENCES)
    template< typename FunT >
    BOOST_LOG_LWFUNCTION_NAME& operator= (FunT const& fun)
#else
    template< typename FunT >
    typename disable_if< is_same< typename remove_cv< FunT >::type, this_type >, this_type& >::type
    operator= (FunT const& fun)
#endif
    {
        BOOST_LOG_LWFUNCTION_NAME tmp(fun);
        this->swap(tmp);
        return *this;
    }

    result_type operator() (BOOST_PP_ENUM_BINARY_PARAMS(BOOST_PP_ITERATION(), ArgT, arg)) const
    {
        return m_pImpl->invoke(m_pImpl BOOST_PP_ENUM_TRAILING_PARAMS(BOOST_PP_ITERATION(), arg));
    }

    BOOST_LOG_EXPLICIT_OPERATOR_BOOL()
    bool operator! () const { return (m_pImpl == NULL); }
    bool empty() const { return (m_pImpl == NULL); }
    void clear()
    {
        if (m_pImpl)
        {
            m_pImpl->destroy(m_pImpl);
            m_pImpl = NULL;
        }
    }

    void swap(this_type& that)
    {
        register implementation_base* p = m_pImpl;
        m_pImpl = that.m_pImpl;
        that.m_pImpl = p;
    }
};

template<
    BOOST_PP_ENUM_PARAMS(BOOST_PP_ITERATION(), typename ArgT)
>
class BOOST_LOG_LWFUNCTION_NAME<
    void
    BOOST_PP_ENUM_TRAILING_PARAMS(BOOST_PP_ITERATION(), ArgT)
>
{
    typedef BOOST_LOG_LWFUNCTION_NAME this_type;
    BOOST_COPYABLE_AND_MOVABLE(this_type)

public:
    typedef void result_type;

private:
    struct implementation_base
    {
        typedef void (*invoke_type)(implementation_base* BOOST_PP_ENUM_TRAILING_PARAMS(BOOST_PP_ITERATION(), ArgT));
        const invoke_type invoke;

        typedef implementation_base* (*clone_type)(const implementation_base*);
        const clone_type clone;

        typedef void (*destroy_type)(implementation_base*);
        const destroy_type destroy;

        implementation_base(invoke_type inv, clone_type cl, destroy_type dstr) : invoke(inv), clone(cl), destroy(dstr)
        {
        }
    };

#if !defined(BOOST_LOG_NO_MEMBER_TEMPLATE_FRIENDS)
    template< typename FunT >
    class implementation;
    template< typename FunT >
    friend class implementation;
#endif

    template< typename FunT >
    class implementation :
        public implementation_base
    {
        FunT m_Function;

    public:
        explicit implementation(FunT const& fun) :
            implementation_base(&implementation::invoke_impl, &implementation::clone_impl, &implementation::destroy_impl),
            m_Function(fun)
        {
        }

        static void destroy_impl(implementation_base* self)
        {
            delete static_cast< implementation* >(self);
        }
        static implementation_base* clone_impl(const implementation_base* self)
        {
            return new implementation(static_cast< const implementation* >(self)->m_Function);
        }
        static result_type invoke_impl(implementation_base* self BOOST_PP_ENUM_TRAILING_BINARY_PARAMS(BOOST_PP_ITERATION(), ArgT, arg))
        {
            static_cast< implementation* >(self)->m_Function(BOOST_PP_ENUM_PARAMS(BOOST_PP_ITERATION(), arg));
        }
    };

private:
    implementation_base* m_pImpl;

public:
    BOOST_LOG_LWFUNCTION_NAME() : m_pImpl(NULL)
    {
    }
    BOOST_LOG_LWFUNCTION_NAME(this_type const& that)
    {
        if (that.m_pImpl)
            m_pImpl = that.m_pImpl->clone(that.m_pImpl);
        else
            m_pImpl = NULL;
    }
    BOOST_LOG_LWFUNCTION_NAME(BOOST_RV_REF(this_type) that)
    {
        m_pImpl = that.m_pImpl;
        that.m_pImpl = NULL;
    }
#if !defined(BOOST_NO_RVALUE_REFERENCES) && !defined(BOOST_NO_CXX11_RVALUE_REFERENCES)
    template< typename FunT >
    BOOST_LOG_LWFUNCTION_NAME(FunT const& fun)
#else
    template< typename FunT >
    BOOST_LOG_LWFUNCTION_NAME(FunT const& fun, typename disable_if< move_detail::is_rv< FunT >, int >::type = 0)
#endif
        : m_pImpl(new implementation< FunT >(fun))
    {
    }
    //! Constructor from NULL
#if !defined(BOOST_NO_NULLPTR) && !defined(BOOST_NO_CXX11_NULLPTR)
    BOOST_LOG_LWFUNCTION_NAME(std::nullptr_t)
#else
    BOOST_LOG_LWFUNCTION_NAME(int p)
#endif
        : m_pImpl(NULL)
    {
#if defined(BOOST_NO_NULLPTR) || defined(BOOST_NO_CXX11_NULLPTR)
        BOOST_ASSERT(p == 0);
#endif
    }
    ~BOOST_LOG_LWFUNCTION_NAME()
    {
        clear();
    }

    BOOST_LOG_LWFUNCTION_NAME& operator= (BOOST_RV_REF(this_type) that)
    {
        this->swap(that);
        return *this;
    }
    BOOST_LOG_LWFUNCTION_NAME& operator= (BOOST_COPY_ASSIGN_REF(this_type) that)
    {
        BOOST_LOG_LWFUNCTION_NAME tmp = that;
        this->swap(tmp);
        return *this;
    }
    //! Assignment of NULL
#if !defined(BOOST_NO_NULLPTR) && !defined(BOOST_NO_CXX11_NULLPTR)
    BOOST_LOG_LWFUNCTION_NAME& operator= (std::nullptr_t)
#else
    BOOST_LOG_LWFUNCTION_NAME& operator= (int p)
#endif
    {
#if defined(BOOST_NO_NULLPTR) || defined(BOOST_NO_CXX11_NULLPTR)
        BOOST_ASSERT(p == 0);
#endif
        clear();
        return *this;
    }
#if !defined(BOOST_NO_RVALUE_REFERENCES) && !defined(BOOST_NO_CXX11_RVALUE_REFERENCES)
    template< typename FunT >
    BOOST_LOG_LWFUNCTION_NAME& operator= (FunT const& fun)
#else
    template< typename FunT >
    typename disable_if< is_same< typename remove_cv< FunT >::type, this_type >, this_type& >::type
    operator= (FunT const& fun)
#endif
    {
        BOOST_LOG_LWFUNCTION_NAME tmp(fun);
        this->swap(tmp);
        return *this;
    }

    result_type operator() (BOOST_PP_ENUM_BINARY_PARAMS(BOOST_PP_ITERATION(), ArgT, arg)) const
    {
        m_pImpl->invoke(m_pImpl BOOST_PP_ENUM_TRAILING_PARAMS(BOOST_PP_ITERATION(), arg));
    }

    BOOST_LOG_EXPLICIT_OPERATOR_BOOL()
    bool operator! () const { return (m_pImpl == NULL); }
    bool empty() const { return (m_pImpl == NULL); }
    void clear()
    {
        if (m_pImpl)
        {
            m_pImpl->destroy(m_pImpl);
            m_pImpl = NULL;
        }
    }

    void swap(this_type& that)
    {
        register implementation_base* p = m_pImpl;
        m_pImpl = that.m_pImpl;
        that.m_pImpl = p;
    }
};

template<
    typename ResultT
    BOOST_PP_ENUM_TRAILING_PARAMS(BOOST_PP_ITERATION(), typename ArgT)
>
inline void swap(
    BOOST_LOG_LWFUNCTION_NAME< ResultT BOOST_PP_ENUM_TRAILING_PARAMS(BOOST_PP_ITERATION(), ArgT) >& left,
    BOOST_LOG_LWFUNCTION_NAME< ResultT BOOST_PP_ENUM_TRAILING_PARAMS(BOOST_PP_ITERATION(), ArgT) >& right
)
{
    left.swap(right);
}

#undef BOOST_LOG_LWFUNCTION_NAME

#endif // BOOST_PP_IS_ITERATING
