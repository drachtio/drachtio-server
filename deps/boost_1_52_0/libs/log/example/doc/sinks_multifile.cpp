/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */

#include <string>
#include <iostream>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/log/core.hpp>
#include <boost/log/filters.hpp>
#include <boost/log/formatters.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/text_multifile_backend.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/attributes/scoped_attribute.hpp>

namespace logging = boost::log;
namespace src = boost::log::sources;
namespace flt = boost::log::filters;
namespace fmt = boost::log::formatters;
namespace sinks = boost::log::sinks;
namespace keywords = boost::log::keywords;

//[ example_sinks_multifile
void init_logging()
{
    boost::shared_ptr< logging::core > core = logging::core::get();

    boost::shared_ptr< sinks::text_multifile_backend > backend =
        boost::make_shared< sinks::text_multifile_backend >();

    // Set up the file naming pattern
    backend->set_file_name_composer
    (
        fmt::stream << "logs/" << fmt::attr< std::string >("RequestID") << ".log"
    );

    // Wrap it into the frontend and register in the core.
    // The backend requires synchronization in the frontend.
    typedef sinks::synchronous_sink< sinks::text_multifile_backend > sink_t;
    boost::shared_ptr< sink_t > sink(new sink_t(backend));

    // Set the formatter
    sink->set_formatter
    (
        fmt::stream
            << "[RequestID: " << fmt::attr< std::string >("RequestID")
            << "] " << fmt::message()
    );

    core->add_sink(sink);
}
//]

void logging_function()
{
    src::logger lg;
    BOOST_LOG(lg) << "Hello, world!";
}

int main(int, char*[])
{
    init_logging();

    {
        BOOST_LOG_SCOPED_THREAD_TAG("RequestID", std::string, "Request1");
        logging_function();
    }
    {
        BOOST_LOG_SCOPED_THREAD_TAG("RequestID", std::string, "Request2");
        logging_function();
    }

    return 0;
}
