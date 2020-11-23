/// \file       ParamGeneration.cpp
///
/// \brief      Parameter manipulation routines for the Zerocoin cryptographic
///             components.
///
/// \author     Ian Miers, Christina Garman and Matthew Green
/// \date       June 2013
///
/// \copyright  Copyright 2013 Ian Miers, Christina Garman and Matthew Green
/// \license    This project is released under the MIT license.
// Copyright (c) 2017 The PIVX developers
// Copyright (c) 2018-2019 The GIANT developers

#include "ParamGeneration.h"
#include <string>
#include <cmath>
#include "hash.h"
#include "uint256.h"

using namespace std;

namespace libzerocoin {

/// \brief Fill in a set of Zerocoin parameters from a modulus "N".
/// \param N                A trusted RSA modulus
/// \param aux              An optional auxiliary string used in derivation
/// \param securityLevel    A security level
///
/// \throws         std::runtime_error if the process fails
///
/// Fills in a ZC_Params data structure deterministically from
/// a trustworthy RSA modulus "N", which is provided as a CBigNum.
///
/// Note: this routine makes the fundamental assumption that "N"
/// encodes a valid RSA-style modulus of the form