// Copyright (c) 2017-2018 The PIVX developers
// Copyright (c) 2018-2019 The GIANT developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef GIANT_ZEROCOIN_H
#define GIANT_ZEROCOIN_H

#include <amount.h>
#include <limits.h>
#include <chainparams.h>
#include "libzerocoin/bignum.h"
#include "libzerocoin/Denominations.h"
#include "key.h"
#include "serialize.h"

//struct that is safe to store essential mint data, without holding any information that allows for actual spending (serial, randomness, private key)
struct CMintMeta
{
    int nHeight;
    uint256 hashSerial;
    uint256 hashPubcoin;
    uint256 hashStake; //requires different hashing method than hashSerial above
    uint8_t nVersion;
    libzerocoin::CoinDenomination denom;
    uint256 txid;
    bool isUsed;
    bool isArchived;
    bool isDeterministic;
    bool isSeedCorrect;

    bool operator <(const CMintMeta& a) const;
};

uint256 GetSerialHash(const CBigNum& bnSerial);
uint256 GetPubCoinHash(const CBigNum& bnValue);

class CZerocoinMint
{
private:
    libzerocoin::CoinDenomination denomination;
    int nHeight;
    CBigNum value;
    CBigNum randomness;
    CBigNum serialNumber;
    uint256 txid;
    CPrivKey privkey;
    uint8_t version;
    bool isUsed;

public:
    static const int STAKABLE_VERSION = 2;
    static const int CURRENT_VERSION = 2;

    CZerocoinMint()
    {
        SetNull();
    }

    CZerocoinMint(libzerocoin::CoinDenomination denom, const CBigNum& value, const CBigNum& randomness, const CBigNum& serialNumber, bool isUsed, const uint8_t& nVersion, CPrivKey* privkey = nullptr)
    {
        SetNull();
        this->denomination = denom;
        this->value = value;
        this->randomness = randomness;
        this->serialNumber = serialNumber;
        this->isUsed = isUsed;
        this->version = nVersion;
        if (nVersion >= 2 && privkey)
            this->privkey = *privkey;
    }

    void SetNull()
    {
        isUsed = false;
        randomness = 0;
        value = 0;
        denomination = libzerocoin::ZQ_ERROR;
        nHeight = 0;
        txid = 0;
        version = 1;
        privkey.clear();
    }

    uint256 GetHash() const;

    CBigNum GetValue() const { return value; }
    void SetValue(CBigNum value){ this->value = value; }
    libzerocoin::CoinDenomination GetDenomination() const { return denomination; }
    int64_t GetDenominationAsAmount() const { return denomination * COIN; }
    void SetDenomination(libzerocoin::CoinDenomination denom){ this->denomination = denom; }
    int GetHeight() const { return nHeight; }
    void SetHeight(int nHeight){ this->nHeight = nHeight; }
    bool IsUsed() const { return this->isUsed; }
    void SetUsed(bool isUsed){ this->isUsed = isUsed; }
    CBigNum GetRandomness() const{ return randomness; }
    void SetRandomness(CBigNum rand){ this->randomness = rand; }
    CBigNum GetSerialNumber() const { return serialNumber; }
    void SetSerialNumber(CBigNum serial){ this->serialNumber = serial; }
    uint256 GetTxHash() const { return this->txid; }
    void SetTxHash(uint256 txid) { this->txid = txid; }
    uint8_t GetVersion() const { return this->version; }
    void SetVersion(const uint8_t nVersion) { this->version = nVersion; }
    CPrivKey GetPrivKey() const { return this->privkey; }
    void SetPrivKey(const CPrivKey& privkey) { this->privkey = privkey; }
    bool GetKeyPair(CKey& key) const;

    inline bool operator <(const CZerocoinMint& a) const { return GetHeight() < a.GetHeight(); }

    CZerocoinMint(const CZerocoinMint& other) {
        denomination = other.GetDenomination();
        nHeight = other.GetHeight();
        value = other.GetValue();
        randomness = other.GetRandomness();
        serialNumber = other.GetSerialNumber();
        txid = other.GetTxHash();
        isUsed = other.IsUsed();
        version = other.GetVersion();
        privkey = other.privkey;
    }

    std::string ToString() const;

    bool operator == (const CZerocoinMint& other) const
    {
        return this->GetValue() == other.GetValue();
    }
    
    // Copy another CZerocoinMint
    inline CZerocoinMint& operator=(const 