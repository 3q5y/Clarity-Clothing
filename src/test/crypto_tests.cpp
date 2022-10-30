// Copyright (c) 2014 The Bitcoin Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "crypto/rfc6979_hmac_sha256.h"
#include "crypto/ripemd160.h"
#include "crypto/sha1.h"
#include "crypto/sha256.h"
#include "crypto/sha512.h"
#include "crypto/hmac_sha256.h"
#include "crypto/hmac_sha512.h"
#include "random.h"
#include "utilstrencodings.h"

#include <vector>

#include <boost/assign/list_of.hpp>
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(crypto_tests)

template<typename Hasher, typename In, typename Out>
void TestVector(const Hasher &h, const In &in, const Out &out) {
    Out hash;
    BOOST_CHECK(out.size() == h.OUTPUT_SIZE);
    hash.resize(out.size());
    {
        // Test that writing the whole input string at once works.
        Hasher(h).Write((unsigned char*)&in[0], in.size()).Finalize(&hash[0]);
        BOOST_CHECK(hash == out);
    }
    for (int i=0; i<32; i++) {
        // Test that writing the string broken up in random pieces works.
        Hasher hasher(h);
        size_t pos = 0;
        while (pos < in.size()) {
            size_t len = insecure_rand() % ((in.size() - pos + 1) / 2 + 1);
            hasher.Write((unsigned char*)&in[pos], len);
            pos += len;
            if (pos > 0 && pos + 2 * out.size() > in.size() && pos < in.size()) {
                // Test that writing the rest at once to a copy of a hasher works.
                Hasher(hasher).Write((unsigned char*)&in[pos], in.size() - pos).Finalize(&hash[0]);
                BOOST_CHECK(hash == out);
            }
        }
        hasher.Finalize(&hash[0]);
        BOOST_CHECK(hash == out);
    }
}

void TestSHA1(const std::string &in, const std::string &hexout) { TestVector(CSHA1(), in, ParseHex(hexout));}
void TestSHA256(const std::string &in, const std::string &hexout) { TestVector(CSHA256(), in, ParseHex(hexout));}
void TestSHA512(const std::string &in, const std::string &hexout) { TestVector(CSHA512(), in, ParseHex(hexout));}
void TestRIPEMD160(const std::string &in, const std::string &hexout) { TestVector(CRIPEMD160(), in, ParseHex(hexout));}

void TestHMACSHA256(const std::string &hexkey, const std::string &hexin, const std::string &hexout) {
    std::vector<unsigned char> key = ParseHex(hexkey);
    TestVector(CHMAC_SHA256(&key[0], key.size()), ParseHex(hexin), ParseHex(hexout));
}

void TestHMACSHA512(const std::string &hexkey, const std::string &hexin, const std::string &hexout) {
    std::vector<unsigned char> key = ParseHex(hexkey);
    TestVector(CHMAC_SHA512(&key[0], key.size()), ParseHex(hexin), ParseHex(hexout));
}

std::string LongTestString(void) {
    std::string ret;
    for (int i=0; i<200000; i++) {
        ret += (unsigned char)(i);
        ret += (unsigned char)(i >> 4);
        ret += (unsigned char)(i >> 8);
        ret += (unsigned char)(i >> 12);
        ret += (unsigned char)(i >> 16);
    }
    return ret;
}

const std::string test1 = LongTestString();

BOOST_AUTO_TEST_CASE(ripemd160_testvectors) {
    TestRIPEMD160("", "9c1185a5c5e9fc54612808977ee8f548b2258d31");
    TestRIPEMD160("abc", "8eb208f7e05d987a9b044a8e98c6b087f15a0bfc");
    TestRIPE