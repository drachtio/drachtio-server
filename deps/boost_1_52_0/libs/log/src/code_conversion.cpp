/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   code_conversion.cpp
 * \author Andrey Semashev
 * \date   08.11.2008
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/libs/log/doc/log.html.
 */

#include <locale>
#include <string>
#include <stdexcept>
#include <algorithm>
#include <boost/log/exceptions.hpp>
#include <boost/log/detail/code_conversion.hpp>

namespace boost {

namespace BOOST_LOG_NAMESPACE {

namespace aux {

BOOST_LOG_ANONYMOUS_NAMESPACE {

    //! The function performs character conversion with the specified facet
    inline std::codecvt_base::result convert(
        std::codecvt< wchar_t, char, std::mbstate_t > const& fac,
        std::mbstate_t& state,
        const char*& pSrcBegin,
        const char* pSrcEnd,
        wchar_t*& pDstBegin,
        wchar_t* pDstEnd)
    {
        return fac.in(state, pSrcBegin, pSrcEnd, pSrcBegin, pDstBegin, pDstEnd, pDstBegin);
    }

    //! The function performs character conversion with the specified facet
    inline std::codecvt_base::result convert(
        std::codecvt< wchar_t, char, std::mbstate_t > const& fac,
        std::mbstate_t& state,
        const wchar_t*& pSrcBegin,
        const wchar_t* pSrcEnd,
        char*& pDstBegin,
        char* pDstEnd)
    {
        return fac.out(state, pSrcBegin, pSrcEnd, pSrcBegin, pDstBegin, pDstEnd, pDstBegin);
    }

} // namespace

//! Constructor
template< typename CharT, typename TraitsT >
converting_ostringstreambuf< CharT, TraitsT >::converting_ostringstreambuf(string_type& storage) :
    m_Storage(storage),
    m_ConversionState()
{
    // Announce the buffer size one character less to implement overflow gracefully
    base_type::setp(m_Buffer, m_Buffer + (sizeof(m_Buffer) / sizeof(*m_Buffer)) - 1);
}

//! Destructor
template< typename CharT, typename TraitsT >
converting_ostringstreambuf< CharT, TraitsT >::~converting_ostringstreambuf()
{
}

//! Clears the buffer to the initial state
template< typename CharT, typename TraitsT >
void converting_ostringstreambuf< CharT, TraitsT >::clear()
{
    const char_type* pBase = this->pbase();
    const char_type* pPtr = this->pptr();
    if (pBase != pPtr)
        this->pbump(static_cast< int >(pBase - pPtr));
    m_ConversionState = std::mbstate_t();
}

//! Puts all buffered data to the string
template< typename CharT, typename TraitsT >
int converting_ostringstreambuf< CharT, TraitsT >::sync()
{
    const char_type* pBase = this->pbase();
    const char_type* pPtr = this->pptr();
    if (pBase != pPtr)
    {
        write(pBase, pPtr);

        // After the data is written there could have been left
        // an incomplete character in the end of the buffer.
        // Move it to the beginning of the buffer.
        traits_type::move(m_Buffer, pBase, pPtr - pBase);
        this->pbump(static_cast< int >(this->pbase() - pBase));
    }

    return 0;
}

//! Puts an unbuffered character to the string
template< typename CharT, typename TraitsT >
typename converting_ostringstreambuf< CharT, TraitsT >::int_type
converting_ostringstreambuf< CharT, TraitsT >::overflow(int_type c)
{
    int_type res;
    const char_type* pBase = this->pbase();
    char_type* pPtr = this->pptr();

    if (!traits_type::eq_int_type(c, traits_type::eof()))
    {
        *(pPtr++) = traits_type::to_char_type(c); // safe, since we announced less buffer size than it actually is
        res = c;
    }
    else
        res = traits_type::not_eof(c);

    write(pBase, pPtr);

    // After the data is written there could have been left
    // an incomplete character in the end of the buffer.
    // Move it to the beginning of the buffer.
    traits_type::move(m_Buffer, pBase, pPtr - pBase);
    this->pbump(static_cast< int >(this->pbase() - pBase));

    return res;
}

//! Puts a character sequence to the string
template< typename CharT, typename TraitsT >
std::streamsize converting_ostringstreambuf< CharT, TraitsT >::xsputn(const char_type* s, std::streamsize n)
{
    const char_type* pend = s + n;

    // First, flush all buffered data
    converting_ostringstreambuf::sync();

    // Now, if we don't have any unfinished characters, we can go a bit faster
    if (this->pptr() == this->pbase())
    {
        write(s, pend);
        if (s != pend)
        {
            // An incomplete character is left at the end of the buffer
            traits_type::copy(this->pbase(), s, pend - s);
            this->pbump(static_cast< int >(pend - s));
        }
    }
    else
    {
        // We'll have to put the characters piece by piece
        while (s != pend)
        {
            std::size_t chunk_size = (std::min)(
                static_cast< std::size_t >(pend - s),
                static_cast< std::size_t >(this->epptr() - this->pptr()));
            traits_type::copy(this->pptr(), s, chunk_size);
            this->pbump(static_cast< int >(chunk_size));
            converting_ostringstreambuf::sync();
            s += chunk_size;
        }
    }

    return n;
}

//! The function writes the specified characters to the storage
template< typename CharT, typename TraitsT >
void converting_ostringstreambuf< CharT, TraitsT >::write(const char_type*& pBase, const char_type* pPtr)
{
    std::locale loc = this->getloc();
    facet_type const& fac = std::use_facet< facet_type >(loc);
    target_char_type converted_buffer[buffer_size];

    while (true)
    {
        target_char_type* pDest = converted_buffer;
        std::codecvt_base::result res = convert(
            fac,
            m_ConversionState,
            pBase,
            pPtr,
            pDest,
            pDest + sizeof(converted_buffer) / sizeof(*converted_buffer));

        switch (res)
        {
        case std::codecvt_base::ok:
            // All characters were successfully converted
            m_Storage.append(converted_buffer, pDest);
            return;

        case std::codecvt_base::partial:
            // Some characters were converted, some were not
            if (pDest != converted_buffer)
            {
                // Some conversion took place, so it seems like
                // the destination buffer might not have been long enough
                m_Storage.append(converted_buffer, pDest);

                // ...and go on for the next part
            }
            else
            {
                // Nothing was converted, looks like the tail of the
                // source buffer contains only part of the last character.
                // Leave it as it is.
                return;
            }

        case std::codecvt_base::noconv:
            // Not possible, unless wchar_t is actually char
            m_Storage.append(reinterpret_cast< const target_char_type* >(pBase), reinterpret_cast< const target_char_type* >(pPtr));
            pBase = pPtr;
            return;

        default: // std::codecvt_base::error
            BOOST_LOG_THROW_DESCR(conversion_error, "Could not convert character encoding");
        }
    }
}

// We'll instantiate the buffer for all character typess regardless of the configuration macros
// since code conversion is used in various places of code even if the library is built for only one character type.
template class BOOST_LOG_EXPORT converting_ostringstreambuf< char, std::char_traits< char > >;
template class BOOST_LOG_EXPORT converting_ostringstreambuf< wchar_t, std::char_traits< wchar_t > >;

} // namespace aux

} // namespace log

} // namespace boost
