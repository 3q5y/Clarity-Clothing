#!/usr/bin/env python3
# Copyright 2014 BitPay Inc.
# Copyright 2016-2017 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test framework for giant utils.

Runs automatically during `make check`.

Can also be run manually."""

from __future__ import division,print_function,unicode_literals

import argparse
import binascii
try:
    import configparser
except ImportError:
    import ConfigParser as configparser
import difflib
import json
import logging
import os
import pprint
import subprocess
import sys

def main():
    config = configparser.ConfigParser()
    config.optionxform = str
    config.read_file(open(os.path.join(os.path.dirname(__file__), "../config.ini"), encoding="utf8"))
    env_conf = dict(config.items('environment'))

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('-v', '--verbose', action='store_true')
    args = parser.parse_args()
    verbose = args.verbose

    if verbose:
        level = logging.DEBUG
    else:
        level = logging.ERROR
    formatter = '%(asctime)s - %(levelname)s - %(message)s'
    # Add the format/level to the logger
    logging.basicConfig(format=formatter, level=level)

    bctester(os.path.join(env_conf["SRCDIR"], "test", "util", "data"), "bitcoin-util-test.json", env_conf)

def bctester(testDir, input_basename, buildenv):
    """ Loads and parses the input file, runs all tests and reports results"""
    input_filename = os.path.join(testDir, input_basename)
    raw_data = open(input_filename, encoding="utf8").read()
    input_data = json.loads(raw_data)

    failed_testcases = []

    for testObj in input_data:
        try:
            bctest(testDir, testObj, buildenv)
            logging.info("PASSED: " + testObj["description"])
        except:
            logging.info("FAILED: " + testObj["description"])
            failed_testcases.append(testObj["description"])

    if failed_testcases:
        error_message = "FAILED_TESTCASES:\n"
        error_message += pprint.pformat(failed_testcases, width=400)
        logging.error(error_message)
        sys.exit(1)
    else:
        sys.exit(0)

def bctest(testDir, testObj, buildenv):
    """Runs a single test, comparing output and RC to expected output and RC.

    Raises an error if input can't be read, executable fails, or output/RC
    are not as expected. Error is caught by bctester() and reported.
    """
    # Get the exec names and arguments
    execprog = os.path.join(buildenv["BUILDDIR"], "src", testObj["exec"] + buildenv["EXEEXT"])
    execargs = testObj['args']
    execrun = [execprog] + execargs

    # Read the input data (if there is any)
    stdinCfg = None
    inputData = None
    if "input" in testObj:
        filename = os.path.join(testDir, testObj["input"])
        inputData = open(filename, encoding="utf8").read()
        stdinCfg = subprocess.PIPE

    # Read the expected output data (if there is any)
    outputFn = None
    outputData = None
    outputType = None
    if "output_cmp" in testObj:
        outputFn = testObj['output_cmp']
        outputType = os.path.splitext(outputFn)[1][1:]  # output type from file extension (determines how to compare)
        try:
            outputData = open(os.path.join(testDir, outputFn), encoding="utf8").read()
        except:
            logging.error("Output file " + outputFn + " can not be opened")
            raise
        if not outputData:
            logging.error("Output data missing for " + outputFn)
            raise Exception
        if not outputType:
            logging.error("Output file %s does no