/* data_logger_ex.cc
   Rémi Attab and Jeremy Barnes, March 2011
   Copyright (c) 2012 Datacratic.  All rights reserved.

   A simple data logger which dumps the logging events from the various
   components of the RTBKit stack into files.
*/


#include "rtbkit/plugins/data_logger/data_logger.h"
#include "soa/logger/file_output.h"
#include "soa/logger/multi_output.h"
#include "soa/service/service_utils.h"

#include <boost/program_options/cmdline.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/positional_options.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/regex.hpp>
#include <vector>
#include <string>
#include <thread>
#include <chrono>


using namespace std;
using namespace Datacratic;
using namespace RTBKIT;


/******************************************************************************/
/* LOGGER SETUP                                                               */
/******************************************************************************/

/** Setup the various outputs of the data logger.

    A logger output is analogous to sinks in other logging frameworks and
    provides ways to funnel various events into files, callbacks, the console,
    etc. When added to a logger, each output can be associated with a filter
    which will determine what part of the log stream will make it to which
    output.
 */
void
setupOutputs(
        DataLogger& logger,
        const string& logDir,
        const string& rotationInterval)
{

    // Log the various error messages generated by our stack to the a log file.
    auto errorOutput = make_shared<RotatingFileOutput>();
    errorOutput->open(logDir + "/%F/errors-%F-%T.log", rotationInterval);
    logger.addOutput(errorOutput, boost::regex("ROUTERERROR|PAERROR"), boost::regex());


    // Output auction events (wins, impressions and clicks) into strategy
    // specific folders. MultiOutput allows the aggregation of multiple outputs
    // under a single outputs.
    auto strategyOutput = make_shared<MultiOutput>();

    auto createMatchedWinFile = [=] (const string & pattern)
        {
            auto result = make_shared<RotatingFileOutput>();
            result->open(pattern, rotationInterval);
            return result;
        };

    // The magic constants represent indexes in the received message.
    strategyOutput->logTo(
            "MATCHEDWIN",
            logDir + "/%F/$(17)/$(5)/$(0)-%T.log.gz",
            createMatchedWinFile);
    strategyOutput->logTo(
            "",
            logDir + "/%F/$(10)/$(11)/$(0)-%T.log.gz",
            createMatchedWinFile);

    logger.addOutput(
            strategyOutput,
            boost::regex("MATCHEDWIN|MATCHEDIMPRESSION|MATCHEDCLICK"));
}


/******************************************************************************/
/* MAIN                                                                       */
/******************************************************************************/

int main (int argc, char** argv)
{
    ServiceProxyArguments serviceArgs;
    string logDir = "data_logger";
    string rotationInterval = "1h";

    using namespace boost::program_options;

    options_description loggerOptions("Logger Options");
    loggerOptions.add_options()
        ("log-dir,d", value<string>(&logDir),
                "Directory where the folders should be stored.")
        ("rotation-interval,r", value<string>(&rotationInterval),
                "Interval between each log rotation.");

    options_description allOptions;
    allOptions.add(loggerOptions).add(serviceArgs.makeProgramOptions());
    allOptions.add_options() ("help,h", "Prints this message");

    variables_map vm;
    store(command_line_parser(argc, argv).options(allOptions).run(), vm);
    notify(vm);

    if (vm.count("help")) {
        cerr << allOptions << endl;
        exit(0);
    }

    auto serviceProxies = serviceArgs.makeServiceProxies();

    // Initialize the logger and it's outputs.
    DataLogger logger(serviceProxies);
    logger.init();
    setupOutputs(logger, logDir, rotationInterval);

    // Subscribe to the message stream coming from the adServer, the router and
    // the post auction loop.
    logger.connectAllServiceProviders("adServer", "logger");
    logger.connectAllServiceProviders("rtbRequestRouter", "logger");
    logger.connectAllServiceProviders("rtbPostAuctionService", "logger");

    // Start processing incoming events.
    logger.start();

    // Job done. Time to take a good long nap.
    while (true) this_thread::sleep_for(chrono::seconds(10));

    return 0;
}