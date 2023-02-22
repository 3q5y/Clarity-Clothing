#!/usr/bin/env python3
# Copyright (c) 2014-2017 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Run regression test suite.

This module calls down into individual test cases via subprocess. It will
forward all unrecognized arguments onto the individual test scripts.

Functional tests are disabled on Windows by default. Use --force to run them anyway.

For a description of arguments recognized by test scripts, see
`test/functional/test_framework/test_framework.py:BitcoinTestFramework.main`.

"""

import argparse
from collections import deque
import configparser
import datetime
import os
import time
import shutil
import signal
import sys
import subprocess
import tempfile
import re
import logging

# Formatting. Default colors to empty strings.
BOLD, BLUE, RED, GREY = ("", ""), ("", ""), ("", ""), ("", "")
try:
    # Make sure python thinks it can write unicode to its stdout
    "\u2713".encode("utf_8").decode(sys.stdout.encoding)
    TICK = "✓ "
    CROSS = "✖ "
    CIRCLE = "○ "
except UnicodeDecodeError:
    TICK = "P "
    CROSS = "x "
    CIRCLE = "o "

if os.name == 'posix':
    # primitive formatting on supported
    # terminal via ANSI escape sequences:
    BOLD = ('\033[0m', '\033[1m')
    BLUE = ('\033[0m', '\033[0;34m')
    RED = ('\033[0m', '\033[0;31m')
    GREY = ('\033[0m', '\033[1;30m')

TEST_EXIT_PASSED = 0
TEST_EXIT_SKIPPED = 77

BASE_SCRIPTS= [
    # Scripts that are run by the travis build process.
    # Longest test should go first, to favor running tests in parallel
    'wallet_backup.py',
    'p2p_pos_fakestake.py',
    'p2p_pos_fakestake_accepted.py',
    'p2p_zpos_fakestake.py',
    'p2p_zpos_fakestake_accepted.py',
    'zerocoin_wrapped_serials.py',
    # vv Tests less than 5m vv
    'feature_block.py',
    'rpc_fundrawtransaction.py',
    # vv Tests less than 2m vv
    'p2p_pos_doublespend.py',
    'wallet_basic.py',
    'wallet_accounts.py',
    'wallet_dump.py',
    'rpc_listtransactions.py',
    # vv Tests less than 60s vv
    'wallet_zapwallettxes.py',
    'wallet_importmulti.py',
    #'mempool_limit.py', # We currently don't limit our mempool
    'wallet_listreceivedby.py',
    'wallet_abandonconflict.py',
    'rpc_rawtransaction.py',
    'feature_reindex.py',
    'rpc_bip38.py',
    # vv Tests less than 30s vv
    'wallet_keypool_topup.py',
    'interface_zmq.py',
    'interface_bitcoin_cli.py',
    'mempool_resurrect.py',
    'wallet_txn_doublespend.py --mineblock',
    'wallet_txn_clone.py --mineblock',
    'rpc_getchaintips.py',
    'interface_rest.py',
    'mempool_spend_coinbase.py',
    'mempool_reorg.py',
    #'mempool_persist.py', # Not yet implemented
    'interface_http.py',
    'rpc_users.py',
    'feature_proxy.py',
    'rpc_signrawtransaction.py',
    'p2p_disconnect_ban.py',
    'rpc_decodescript.py',
    'rpc_blockchain.py',
    'rpc_deprecated.py',
    'wallet_disable.py',
    'rpc_net.py',
    'wallet_keypool.py',
    'p2p_mempool.py',
    'mining_prioritisetransaction.py',
    'p2p_invalid_block.py',
    'p2p_invalid_tx.py',
    'rpc_signmessage.py',
    'wallet_import_rescan.py',
    'mining_basic.py',
    'wallet_bumpfee.py',
    'wallet_listsinceblock.py',
    'p2p_leak.py',
    'wallet_encryption.py',
    'feature_cltv.py',
    'wallet_resendwallettransactions.py',
    'feature_minchainwork.py',
    'p2p_fingerprint.py',
    'feature_uacomment.py',
    'p2p_unrequested_blocks.py',
    'feature_config_args.py',
    'feature_help.py',
    # Don't append tests at the end to avoid merge conflicts
    # Put them in a random line within the section that fits their approximate run-time
]

EXTENDED_SCRIPTS = [
    # These tests are not run by the travis build process.
    # Longest test should go first, to favor running tests in parallel
    # vv Tests less than 20m vv
    'feature_fee_estimation.py',
    # vv Tests less than 5m vv
    # vv Tests less than 2m vv
    'p2p_timeouts.py',
    # vv Tests less than 60s vv
    'p2p_feefilter.py',
    'rpc_bind.py',
    # vv Tests less than 30s vv
    'example_test.py',
    'feature_notifications.py',
    'rpc_invalidateblock.py',
]

# Place EXTENDED_SCRIPTS first since it has the 3 longest running tests
ALL_SCRIPTS = EXTENDED_SCRIPTS + BASE_SCRIPTS

NON_SCRIPTS = [
    # These are python files that live in the functional tests directory, but are not test scripts.
    "combine_logs.py",
    "create_cache.py",
    "test_runner.py",
]

def main():
    # Parse arguments and pass through unrecognised args
    parser = argparse.ArgumentParser(add_help=False,
                                     usage='%(prog)s [test_runner.py options] [script options] [scripts]',
                                     description=__doc__,
                                     epilog='''
    Help text and arguments for individual test script:''',
                                     formatter_class=argparse.RawTextHelpFormatter)
    parser.add_argument('--combinedlogslen', '-c', type=int, default=0, help='print a combined log (of length n lines) from all test nodes and test framework to the console on failure.')
    parser.add_argument('--coverage', action='store_true', help='generate a basic coverage report for the RPC interface')
    parser.add_argument('--exclude', '-x', help='specify a comma-separated-list of scripts to exclude.')
    parser.add_argument('--extended', action='store_true', help='run the extended test suite in addition to the basic tests')
    parser.add_argument('--force', '-f', action='store_true', help='run tests even on platforms where they are disabled by default (e.g. windows).')
    parser.add_argument('--help', '-h', '-?', action='store_true', help='print help text and exit')
    parser.add_argument('--jobs', '-j', type=int, default=4, help='how many test scripts to run in parallel. Default=4.')
    parser.add_argument('--keepcache', '-k', action='store_true', help='the default behavior is to flush the cache directory on startup. --keepcache retains the cache from the previous testrun.')
    parser.add_argument('--quiet', '-q', action='store_true', help='only print results summary and failure logs')
    parser.add_argument('--tmpdirprefix', '-t', default=tempfile.gettempdir(), help="Root directory for datadirs")
    args, unknown_args = parser.parse_known_args()

    # args to be passed on always start with two dashes; tests are the remaining unknown args
    tests = [arg for arg in unknown_args if arg[:2] != "--"]
    passon_args = [arg for arg in unknown_args if arg[:2] == "--"]

    # Read config generated by configure.
    config = configparser.ConfigParser()
    configfile = os.path.abspath(os.path.dirname(__file__)) + "/../config.ini"
    config.read_file(open(configfile))

    passon_args.append("--configfile=%s" % configfile)

    # Set up logging
    logging_level = logging.INFO if args.quiet else logging.DEBUG
    logging.basicConfig(format='%(message)s', level=logging_level)

    # Create base test directory
    tmpdir = "%s/giant_test_runner_%s" % (args.tmpdirprefix, datetime.datetime.now().strftime("%Y%m%d_%H%M%S"))
    os.makedirs(tmpdir)

    logging.debug("Temporary test directory at %s" % tmpdir)

    enable_wallet = config["components"].getboolean("ENABLE_WALLET")
    enable_utils = config["components"].getboolean("ENABLE_UTILS")
    enable_bitcoind = config["components"].getboolean("ENABLE_BITCOIND")

    if config["environment"]["EXEEXT"] == ".exe" and not args.force:
        # https://github.com/bitcoin/bitcoin/commit/d52802551752140cf41f0d9a225a43e84404d3e9
        # https://github.com/bitcoin/bitcoin/pull/5677#issuecomment-136646964
        print("Tests currently disabled on Windows by default. Use --force option to enable")
        sys.exit(0)

    if not (enable_wallet and enable_utils and enable_bitcoind):
        print("No functional tests to run. Wallet, utils, and giantd must all be enabled")
        print("Rerun `configure` with -enable-wallet, -with-utils and -with-daemon and rerun make")
        sys.exit(0)

    # Build list of tests
    if tests:
        # Individual tests have been specified. Run specified tests that exist
        # in the ALL_SCRIPTS list. Accept the name with or without .py extension.
        tests = [re.sub("\.py$", "", t) + ".py" for t in tests]
        test_list = []
        for t in tests:
            if t in ALL_SCRIPTS:
                test_list.append(t)
            else:
                print("{}WARNING!{} Test '{}' not found in full test list.".format(BOLD[1], BOLD[0], t))
    else:
        # No individual tests have been specified.
        # Run all base tests, and optionally run extended tests.
        test_list = BASE_SCRIPTS
        if args.extended:
            # place the EXTENDED_SCRIPTS first since the three longest ones
            # are there and the list is shorter
            test_list = EXTENDED_SCRIPTS + test_list

    # Remove the test cases that the user has explicitly asked to exclude.
    if args.exclude:
        tests_excl = [re.sub("\.py$", "", t) + ".py" for t in args.exclude.split(',')]
        for exclude_test in tests_excl:
            if exclude_test in test_list:
                test_list.remove(exclude_test)
            else:
                print("{}WARNING!{} Test '{}' not found in current test list.".format(BOLD[1], BOLD[0], exclude_test))

    if not test_list:
        print("No valid test scripts specified. Check that your test is in one "
              "of the test lists in test_runner.py, or run test_runner.py with no arguments to run all tests")
        sys.exit(0)

    if args.help:
        # Print help for test_runner.py, then print help of the first script (with args removed) and exit.
        parser.print_help()
        subprocess.check_call([(config["environment"]["SRCDIR"] + '/test/functional/' + test_list[0].split()[0])] + ['-h'])
        sys.exit(0)

    check_script_list(config["environment"]["SRCDIR"])
    check_script_prefixes()

    if not args.keepcache:
        shutil.rmtree("%s/test/cache" % config["environment"]["BUILDDIR"], ignore_errors=True)

    run_tests(test_list, config["environment"]["SRCDIR"], config["environment"]["BUILDDIR"], config["environment"]["EXEEXT"], tmpdir, args.jobs, args.coverage, passon_args, args.combinedlogslen)

def run_tests(test_list, src_dir, build_dir, exeext, tmpdir, jobs=1, enable_coverage=False, args=[], combined_logs_len=0):
    # Warn if bitcoind is already running (unix only)
    try:
        if subprocess.check_output(["pidof", "giantd"]) is not None:
            print("%sWARNING!%s There is already a giantd process running on this system. Tests may fail unexpectedly due to resource contention!" % (BOLD[1], BOLD[0]))
    except (OSError, subprocess.SubprocessError):
        pass

    # Warn if there is a cache directory
    cache_dir = "%s/test/cache" % build_dir
    if os.path.isdir(cache_dir):
        print("%sWARNING!%s There is a cache directory here: %s. If tests fail u