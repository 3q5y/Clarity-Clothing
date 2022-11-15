/**
* @file       tutorial.cpp
*
* @brief      Simple tutorial program to illustrate Zerocoin usage.
*
* @author     Ian Miers, Christina Garman and Matthew Green
* @date       June 2013
*
* @copyright  Copyright 2013 Ian Miers, Christina Garman and Matthew Green
* @license    This project is released under the MIT license.
**/
// Copyright (c) 2017-2018 The PIVX developers
// Copyright (c) 2018-2019 The GIANT developers

#include <boost/test/unit_test.hpp>
#include <string>
#include <iostream>
#include <fstream>
// #include <curses.h>
#include <exception>
#include "streams.h"
#include "libzerocoin/bignum.h"
#include "libzerocoin/ParamGeneration.h"
#include "libzerocoin/Denominations.h"
#include "libzerocoin/Coin.h"
#include "libzerocoin/CoinSpend.h"
#include "libzerocoin/Accumulator.h"

using namespace std;

#define COINS_TO_ACCUMULATE     5
#define DUMMY_TRANSACTION_HASH  0 // in real life these would be uint256 hashes
#define DUMMY_ACCUMULATOR_ID    0 // in real life these would be uint256 hashes

//
// We generated this for testing only. Don't use it in production!
//
#define TUTORIAL_TEST_MODULUS   "a8852ebf7c49f01cd196e35394f3b74dd86283a07f57e0a262928e7493d4a3961d93d93c90ea3369719641d626d28b9cddc6d9307b9aabdbffc40b6d6da2e329d079b4187ff784b2893d9f53e9ab913a04ff02668114695b07d8ce877c4c8cac1b12b9beff3c51294ebe349eca41c24cd32a6d09dd1579d3947e5c4dcc30b2090b0454edb98c6336e7571db09e0fdafbd68d8f0470223836e90666a5b143b73b9cd71547c917bf24c0efc86af2eba046ed781d9acb05c80f007ef5a0a5dfca23236f37e698e8728def12554bc80f294f71c040a88eff144d130b24211016a97ce0f5fe520f477e555c9997683d762aff8bd1402ae6938dd5c994780b1bf6aa7239e9d8101630ecfeaa730d2bbc97d39beb057f016db2e28bf12fab4989c0170c2593383fd04660b5229adcd8486ba78f6cc1b558bcd92f344100dff239a8c00dbc4c2825277f24bdd04475bcc9a8c39fd895eff97c1967e434effcb9bd394e0577f4cf98c30d9e6b54cd47d6e447dcf34d67e48e4421691dbe4a7d9bd503abb9"

//
// The following routine exercises most of the core functions of
// the library. We've commented it as well as possible to give
// you the flavor of how libzerocoin works.
//
// For details on Zerocoin integration, see the libzerocoin wiki
// at: https://github.com/Zerocoin/libzerocoin/wiki
//

bool
ZerocoinTutorial()
{
	// The following simple code illustrates the call flow for Zerocoin
	// applications. In a real currency network these operations would
	// be split between individual payers/payees, network nodes and miners.
	//
	// For each call we specify the participant who would use it.

	// Zerocoin uses exceptions (based on the runtime_error class)
	// to indicate all sorts of problems. Always remember to catch them!

	try {

		/********************************************************************/
		// What is it:      Parameter loading
		// Who does it:     ALL ZEROCOIN PARTICIPANTS
		// What it does:    Loads a trusted Zerocoin modulus "N" and
		//                  generates all associated parameters from it.
		//                  We use a hardcoded "N" that we generated using
		//                  the included 'paramgen' utility.
		/********************************************************************/

		// Load a test modulus from our hardcoded string (above)
		CBigNum testModulus;
		testModulus.SetHex(std::string(TUTORIAL_TEST_MODULUS));

		// Set up the Zerocoin Params object
		libzerocoin::ZerocoinParams* params = new libzerocoin::ZerocoinParams(testModulus);

		cout << "Successfully loaded parameters." << endl;

		/********************************************************************/
		// What is it:      Coin generation
		// Who does it:     ZEROCOIN CLIENTS
		// What it does:    Generates a new 'zerocoin' coin using the
		//                  public parameters. Once generated, the client
		//                  will transmit the public portion of this coin
		//                  in a ZEROCOIN_MINT transaction. The inputs
		//                  to this transaction must add up to the zerocoin
		//                  denomination plus any transaction fees.
		/********************************************************************/

		// The following constructor does all the work of minting a brand
		// new zerocoin. It stores all the private values i