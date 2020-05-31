#!/usr/bin/env python
# Copyright (c) 2014 Wladimir J. van der Laan
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
'''
Run this script from the root of the repository to update all translations from
transifex.
It will do the following automatically:

- fetch all translations using the tx tool
- post-process them into valid and committable format
  - remove invalid control characters
  - remove location tags (makes diffs less noisy)

TODO:
- auto-add new translations to the build system according to the translation process
'''
from __future__ import division, print_function
import subprocess
import re
import sys
import os
import io
import xml.etree.ElementTree as ET

# Name of transifex tool
TX = 'tx'
# Name of source language file
SOURCE_LANG = 'giant_en.ts'
# Directory with locale files
LOCALE_DIR = 'src/qt/locale'
# Minimum number of messages for translation to be considered at all
MIN_NUM_MESSAGES = 10

def check_at_repository_root():
    if not os.path.exists('.git'):
        print('No .git directory found')
        print('Execute this script at the root of the repository', file=sys.stderr)
        exit(1)

def fetch_all_translations():
    if subprocess.call([TX, 'pull', '-f', '-a']):
        print('Error while fetching translations', file=sys.stderr)
        exit(1)

def find_format_specifiers(s):
    '''Find all format specifiers in a string.'''
    pos = 0
    specifiers = []
    while True:
        percent = s.find('%', pos)
        if percent < 0:
            break
        try:
            specifiers.append(s[percent+1])
        except:
            print('Failed to get specifier')
        pos = percent+2
    return specifiers

def split_format_specifiers(specifiers):
    '''Split format specifiers between numeric (Qt) and others (strprintf)'''
    numeric = []
    other = []
    for s in specifiers:
        if s in {'1','2','3','4','5','6','7','8','9'}:
            numeric.append(s)
        else:
            other.append(s)

    # If both numeric format specifiers and "others" are used, assume we're dealing
    # with a Qt-formatted message. In the case of Qt formatting (see https://doc.qt.io/qt-5/qstring.html#arg)
    # only numeric formats are replaced at all. This means "(percentage: %1%)" is valid, without needing
    # any kind of escaping that would be necessary for strprintf. Without this, this function
    # would wrongly detect '%)' as a printf format specifier.
    if numeric:
        other = []

    # numeric (Qt) can be present in any order, others (strprintf) must be in specified order
    return set(numeric),other

def san