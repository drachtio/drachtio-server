/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */

#include <fstream>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/formatters.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/utility/init/common_attributes.hpp>

namespace logging = boost::log;
namespace src = boost::log::sources;
namespace fmt = boost::log::formatters;
namespace sinks = boost::log::sinks;
namespace keywords = boost::log::keywords;

//[ example_tutorial_formatters_stream_manual
void init()
{
    typedef sinks::synchronous_sink< sinks::text_ostream_backend > text_sink;
    boost::shared_ptr< text_sink > pSink = boost::make_shared< text_sink >();

    pSink->locked_backend()->add_stream(
        boost::make_shared< std::ofstream >("sample.log"));

    pSink->set_formatter
    (
        fmt::stream
                // line id will be written in hex, 8-digits, zero-filled
            << fmt::attr< unsigned int >("LineID", keywords::format = "%08x")
            << ": <" << fmt::attr< logging::trivial::severity_level >("Severity")
            << "> " << fmt::message()
    );

    logging::core::get()->add_sink(pSink);
}
//]

int main(int, char*[])
{
    init();
    logging::add_common_attributes();

    using namespace logging::trivial;
    src::severity_logger< severity_level > lg;

    BOOST_LOG_SEV(lg, trace) << "A trace severity message";
    BOOST_LOG_SEV(lg, debug) << "A debug severity message";
    BOOST_LOG_SEV(lg, info) << "An informational severity message";
    BOOST_LOG_SEV(lg, warning) << "A warning severity message";
    BOOST_LOG_SEV(lg, error) << "An error severity message";
    BOOST_LOG_SEV(lg, fatal) << "A fatal severity message";

    return 0;
}
