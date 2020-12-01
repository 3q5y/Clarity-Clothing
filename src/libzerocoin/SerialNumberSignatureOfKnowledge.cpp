/**
* @file       SerialNumberSignatureOfKnowledge.cpp
*
* @brief      SerialNumberSignatureOfKnowledge class for the Zerocoin library.
*
* @author     Ian Miers, Christina Garman and Matthew Green
* @date       June 2013
*
* @copyright  Copyright 2013 Ian Miers, Christina Garman and Matthew Green
* @license    This project is released under the MIT license.
**/
// Copyright (c) 2017 The PIVX developers
// Copyright (c) 2018-2019 The GIANT developers

#include <streams.h>
#include "SerialNumberSignatureOfKnowledge.h"

namespace libzerocoin {

SerialNumberSignatureOfKnowledge::SerialNumberSignatureOfKnowledge(const ZerocoinParams* p): params(p) { }

// Use one 256 bit seed and concatenate 4 unique 256 bit hashes to make a 1024 bit hash
CBigNum SeedTo1024(uint256 hashSeed) {
    CHashWriter hasher(0,0);
    hasher << hashSeed;

    vector<unsigned char> vResult;
    vector<unsigned char> vHash 