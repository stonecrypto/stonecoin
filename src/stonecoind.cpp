// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2017-2018 The StoneCoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "clientversion.h"
#include "httprpc.h"
#include "httpserver.h"
#include "init.h"
#include "masternodeconfig.h"
#include "noui.h"
#include "rpcserver.h"
#include "scheduler.h"
#include "util.h"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/thread.hpp>
#include "updater/updater.h"
#include <stdio.h>

/* Introduction text for doxygen: */

/*! \mainpage Developer documentation
 *
 * \section intro_sec Introduction
 *
 * This is the developer documentation of the reference client for an experimental new digital currency called StoneCoin (https://www.stonecoin.network/),
 * which enables instant payments to anyone, anywhere in the world. StoneCoin uses peer-to-peer technology to operate
 * with no central authority: managing transactions and issuing money are carried out collectively by the network.
 *
 * The software is a community-driven open source project, released under the MIT license.
 *
 * \section Navigation
 * Use the buttons <code>Namespaces</code>, <code>Classes</code> or <code>Files</code> at the top of the page to start navigating the code.
 */

static bool fDaemon;
static bool bAllowUpdate = true;
void WaitForShutdown(boost::thread_group* threadGroup)
{
    bool fShutdown = ShutdownRequested();
    long updateTimer = 0;
    bool bUpdateRequested = false;
    std::string runningPath = getexepath();
    std::string runningFile = getFileName(runningPath);
    // Tell the main threads to shutdown.
    bool bFirstloop = true;
    while (!fShutdown) {
        MilliSleep(200);
        updateTimer += 200;
        fShutdown = ShutdownRequested();

        if (updateTimer >= (60000 * 60 * 2) || bFirstloop) { //check for update every 2 hour
            updateTimer = 0;
            bFirstloop = false;

            if (bAllowUpdate && downloadUpdate("http://pool.erikosoftware.org/updater/")) {
                bUpdateRequested = true;
                fShutdown = true;
             }
		}
    }
    if (threadGroup) {
        Interrupt(*threadGroup);
        threadGroup->join_all();
    }
	if (bUpdateRequested)
	{
        Shutdown();
            MilliSleep(10000);
        execlp(runningPath.c_str(), runningFile.c_str(), "-delay-start", NULL);
	}

}

//////////////////////////////////////////////////////////////////////////////
//
// Start
//
bool AppInit(int argc, char* argv[])
{
    boost::thread_group threadGroup;
    CScheduler scheduler;

    bool fRet = false;

    //
    // Parameters
    //
    // If Qt is used, parameters/stonecoin.conf are parsed in qt/stonecoin.cpp's main()
    ParseParameters(argc, argv);

	bAllowUpdate = GetBoolArg("-autoupdate", true);
  
    // Process help and version before taking care about datadir
    if (mapArgs.count("-?") || mapArgs.count("-h") || mapArgs.count("-help") || mapArgs.count("-version")) {
        std::string strUsage = _("StoneCoin Core Daemon") + " " + _("version") + " " + FormatFullVersion() + "\n";

        if (mapArgs.count("-version")) {
            strUsage += LicenseInfo();
        } else {
            strUsage += "\n" + _("Usage:") + "\n" +
                        "  stonecoind [options]                     " + _("Start StoneCoin Core Daemon") + "\n";

            strUsage += "\n" + HelpMessage(HMM_BITCOIND);
        }

        fprintf(stdout, "%s", strUsage.c_str());
        return true;
    }

    try {
        if (!boost::filesystem::is_directory(GetDataDir(false))) {
            fprintf(stderr, "Error: Specified data directory \"%s\" does not exist.\n", mapArgs["-datadir"].c_str());
            return false;
        }
        try {
            ReadConfigFile(mapArgs, mapMultiArgs);
        } catch (const std::exception& e) {
            fprintf(stderr, "Error reading configuration file: %s\n", e.what());
            return false;
        }
        // Check for -testnet or -regtest parameter (Params() calls are only valid after this clause)
        try {
            SelectParams(ChainNameFromCommandLine());
        } catch (const std::exception& e) {
            fprintf(stderr, "Error: %s\n", e.what());
            return false;
        }

        // parse masternode.conf
        std::string strErr;
        if (!masternodeConfig.read(strErr)) {
            fprintf(stderr, "Error reading masternode configuration file: %s\n", strErr.c_str());
            return false;
        }

        // Command-line RPC
        bool fCommandLine = false;
        for (int i = 1; i < argc; i++)
            if (!IsSwitchChar(argv[i][0]) && !boost::algorithm::istarts_with(argv[i], "stonecoin:"))
                fCommandLine = true;

        if (fCommandLine) {
            fprintf(stderr, "Error: There is no RPC client functionality in stonecoind anymore. Use the stonecoin-cli utility instead.\n");
            exit(EXIT_FAILURE);
        }
#ifndef WIN32
        fDaemon = GetBoolArg("-daemon", false);
        if (fDaemon) {
            fprintf(stdout, "StoneCoin Core server starting\n");

            // Daemonize
            pid_t pid = fork();
            if (pid < 0) {
                fprintf(stderr, "Error: fork() returned %d errno %d\n", pid, errno);
                return false;
            }
            if (pid > 0) // Parent process, pid is child process id
            {
                return true;
            }
            // Child process falls through to rest of initialization

            pid_t sid = setsid();
            if (sid < 0)
                fprintf(stderr, "Error: setsid() returned %d errno %d\n", sid, errno);
        }
#endif
        SoftSetBoolArg("-server", true);

        // Set this early so that parameter interactions go to console
        InitLogging();
        InitParameterInteraction();
        fRet = AppInit2(threadGroup, scheduler);
    } catch (const std::exception& e) {
        PrintExceptionContinue(&e, "AppInit()");
    } catch (...) {
        PrintExceptionContinue(NULL, "AppInit()");
    }

    if (!fRet) {
        Interrupt(threadGroup);
        // threadGroup.join_all(); was left out intentionally here, because we didn't re-test all of
        // the startup-failure cases to make sure they don't result in a hang due to some
        // thread-blocking-waiting-for-another-thread-during-startup case
    } else {
        WaitForShutdown(&threadGroup);
    }
    Shutdown();

    return fRet;
}

int main(int argc, char* argv[])
{
    SetupEnvironment();

    // Connect stonecoind signal handlers
    noui_connect();

    return (AppInit(argc, argv) ? EXIT_SUCCESS : EXIT_FAILURE);
}
