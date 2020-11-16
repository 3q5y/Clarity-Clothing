/**
 * @file       Accumulator.cpp
 *
 * @brief      Accumulator and AccumulatorWitness classes for the Zerocoin library.
 *
 * @author     Ian Miers, Christina Garman and Matthew Green
 * @date       June 2013
 *
 * @copyright  Copyright 2013 Ian Miers, Christina Garman and Matthew Green
 * @license    This project is released under the MIT license.
 **/
// Copyright (c) 2017-2018 The PIVX developers
// Copyright (c) 2018-2019 The GIANT developers

#include <sstream>
#include <iostream>
#include "Accumulator.h"
#include "ZerocoinDefines.h"

namespace libzerocoin {

//Accumulator class
Accumulator::Accumulator(const AccumulatorAndProofParams* p, const CoinDenomination d): params(p) {
    if (!(params->initialized)) {
        throw std::runtime_error("Invalid parameters for accumulator");
    }
    denomination = d;
    this->value = this->params->accumulatorBase;
}

Accumulator::Accumulator(const ZerocoinParams* p, const CoinDenomination d, const CBigNum bnValue) {
    this->params = &(p->accumulatorParams);
    denomination = d;

    if (!(params->initialized)) {
        throw std::runtime_error("Invalid parameters for accumulator");
    }

    if(bnValue != 0)
        this->value = bnValue;
    else
        this->value = this->params->accumulatorBase;
}

void Accumulator::increment(const CBigNum& bnValue) {
    // Compute new accumulator = "old accumul