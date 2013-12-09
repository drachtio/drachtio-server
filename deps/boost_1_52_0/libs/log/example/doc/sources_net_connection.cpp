/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */

#include <cstddef>
#include <string>
#include <fstream>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/log/core.hpp>
#include <boost/log/filters.hpp>
#include <boost/log/formatters.hpp>
#include <boost/log/attributes/constant.hpp>
#include <boost/log/attributes/scoped_attribute.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/utility/init/common_attributes.hpp>

namespace logging = boost::log;
namespace src = boost::log::sources;
namespace flt = boost::log::filters;
namespace fmt = boost::log::formatters;
namespace sinks = boost::log::sinks;
namespace attrs = boost::log::attributes;

//[ example_sources_network_connection
class network_connection
{
    src::logger m_logger;
    src::logger::attribute_set_type::iterator m_remote_addr;

public:
    void on_connected(std::string const& remote_addr)
    {
        // Put the remote address into the logger to automatically attach it
        // to every log record written through the logger
        m_remote_addr = m_logger.add_attribute("RemoteAddress",
            attrs::constant< std::string >(remote_addr)).first;

        // The straightforward way of logging
        if (logging::record rec = m_logger.open_record())
        {
            rec.message() = "Connection established";
            m_logger.push_record(rec);
        }
    }
    void on_disconnected()
    {
        // The simpler way of logging: the above "if" condition is wrapped into a neat macro
        BOOST_LOG(m_logger) << "Connection shut down";

        // Remove the attribute with the remote address
        m_logger.remove_attribute(m_remote_addr);
    }
    void on_data_received(std::size_t size)
    {
        // Put the size as an additional attribute
        // so it can be collected and accumulated later if needed.
        // The attribute will be attached to the only log record
        // that is made within the current scope.
        BOOST_LOG_SCOPED_LOGGER_TAG(m_logger, "ReceivedSize", std::size_t, size);
        BOOST_LOG(m_logger) << "Some data received";
    }
    void on_data_sent(std::size_t size)
    {
        BOOST_LOG_SCOPED_LOGGER_TAG(m_logger, "SentSize", std::size_t, size);
        BOOST_LOG(m_logger) << "Some data sent";
    }
};
//]

int main(int, char*[])
{
    // Construct the sink
    typedef sinks::synchronous_sink< sinks::text_ostream_backend > text_sink;
    boost::shared_ptr< text_sink > pSink = boost::make_shared< text_sink >();

    // Add a stream to write log to
    pSink->locked_backend()->add_stream(
        boost::make_shared< std::ofstream >("sample.log"));

    // Set the formatter
    pSink->set_formatter
    (
        fmt::stream
            << fmt::attr< unsigned int >("LineID")
            << ": [" << fmt::attr< std::string >("RemoteAddress") << "] "
            << fmt::if_(flt::has_attr("ReceivedSize"))
               [
                    fmt::stream << "[Received: " << fmt::attr< std::size_t >("ReceivedSize") << "] "
               ]
            << fmt::if_(flt::has_attr("SentSize"))
               [
                    fmt::stream << "[Sent: " << fmt::attr< std::size_t >("SentSize") << "] "
               ]
            << fmt::message()
    );

    // Register the sink in the logging core
    logging::core::get()->add_sink(pSink);

    // Register other common attributes, such as time stamp and record counter
    logging::add_common_attributes();

    // Emulate network activity
    network_connection conn;

    conn.on_connected("11.22.33.44");
    conn.on_data_received(123);
    conn.on_data_sent(321);
    conn.on_disconnected();

    return 0;
}
