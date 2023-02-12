#!/usr/bin/env python3
# Copyright (c) 2010 ArtForz -- public domain half-a-node
# Copyright (c) 2012 Jeff Garzik
# Copyright (c) 2010-2017 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Bitcoin test framework primitive and message strcutures

CBlock, CTransaction, CBlockHeader, CTxIn, CTxOut, etc....:
    data structures that should map to corresponding structures in
    bitcoin/primitives

msg_block, msg_tx, msg_headers, etc.:
    data structures that represent network messages

ser_*, deser_*: functions that handle serialization/deserialization."""
from codecs import encode
import copy
import hashlib
from io import BytesIO
import random
import socket
import struct
import time

from test_framework.siphash import siphash256
from test_framework.util import hex_str_to_bytes, bytes_to_hex_str

MIN_VERSION_SUPPORTED = 60001
MY_VERSION = 70914  # past bip-31 for ping/pong
MY_SUBVERSION = b"/python-mininode-tester:0.0.3/"
MY_RELAY = 1 # from version 70001 onwards, fRelay should be appended to version messages (BIP37)

MAX_INV_SZ = 50000
MAX_BLOCK_BASE_SIZE = 1000000

COIN = 100000000 # 1 btc in satoshis

NODE_NETWORK = (1 << 0)
# NODE_GETUTXO = (1 << 1)
NODE_BLOOM = (1 << 2)

# Serialization/deserialization tools
def sha256(s):
    return hashlib.new('sha256', s).digest()

def ripemd160(s):
    return hashlib.new('ripemd160', s).digest()

def hash256(s):
    return sha256(sha256(s))

def ser_compact_size(l):
    r = b""
    if l < 253:
        r = struct.pack("B", l)
    elif l < 0x10000:
        r = struct.pack("<BH", 253, l)
    elif l < 0x100000000:
        r = struct.pack("<BI", 254, l)
    else:
        r = struct.pack("<BQ", 255, l)
    return r

def deser_compact_size(f):
    nit = struct.unpack("<B", f.read(1))[0]
    if nit == 253:
        nit = struct.unpack("<H", f.read(2))[0]
    elif nit == 254:
        nit = struct.unpack("<I", f.read(4))[0]
    elif nit == 255:
        nit = struct.unpack("<Q", f.read(8))[0]
    return nit

def deser_string(f):
    nit = deser_compact_size(f)
    return f.read(nit)

def ser_string(s):
    return ser_compact_size(len(s)) + s

def deser_uint256(f):
    r = 0
    for i in range(8):
        t = struct.unpack("<I", f.read(4))[0]
        r += t << (i * 32)
    return r


def ser_uint256(u):
    rs = b""
    for i in range(8):
        rs += struct.pack("<I", u & 0xFFFFFFFF)
        u >>= 32
    return rs


def ser_uint64(u):
    rs = b""
    for i in range(2):
        rs += struct.pack("<I", u & 0xFFFFFFFF)
        u >>= 32
    return rs


def uint256_from_str(s):
    r = 0
    t = struct.unpack("<IIIIIIII", s[:32])
    for i in range(8):
        r += t[i] << (i * 32)
    return r


def uint256_from_compact(c):
    nbytes = (c >> 24) & 0xFF
    v = (c & 0xFFFFFF) << (8 * (nbytes - 3))
    return v


def deser_vector(f, c):
    nit = deser_compact_size(f)
    r = []
    for i in range(nit):
        t = c()
        t.deserialize(f)
        r.append(t)
    return r


# ser_function_name: Allow for an alternate serialization function on the
# entries in the vector (we use this for serializing the vector of transactions
# for a witness block).
def ser_vector(l, ser_function_name=None):
    r = ser_compact_size(len(l))
    for i in l:
        if ser_function_name:
            r += getattr(i, ser_function_name)()
        else:
            r += i.serialize()
    return r


def deser_uint256_vector(f):
    nit = deser_compact_size(f)
    r = []
    for i in range(nit):
        t = deser_uint256(f)
        r.append(t)
    return r


def ser_uint256_vector(l):
    r = ser_compact_size(len(l))
    for i in l:
        r += ser_uint256(i)
    return r


def deser_string_vector(f):
    nit = deser_compact_size(f)
    r = []
    for i in range(nit):
        t = deser_string(f)
        r.append(t)
    return r


def ser_string_vector(l):
    r = ser_compact_size(len(l))
    for sv in l:
        r += ser_string(sv)
    return r


# Deserialize from a hex string representation (eg from RPC)
def FromHex(obj, hex_string):
    obj.deserialize(BytesIO(hex_str_to_bytes(hex_string)))
    return obj

# Convert a binary-serializable object to hex (eg for submission via RPC)
def ToHex(obj):
    return bytes_to_hex_str(obj.serialize())

# Objects that map to bitcoind objects, which can be serialized/deserialized

class CAddress():
    def __init__(self):
        self.nServices = 1
        self.pchReserved = b"\x00" * 10 + b"\xff" * 2
        self.ip = "0.0.0.0"
        self.port = 0

    def deserialize(self, f):
        self.nServices = struct.unpack("<Q", f.read(8))[0]
        self.pchReserved = f.read(12)
        self.ip = socket.inet_ntoa(f.read(4))
        self.port = struct.unpack(">H", f.read(2))[0]

    def serialize(self):
        r = b""
        r += struct.pack("<Q", self.nServices)
        r += self.pchReserved
        r += socket.inet_aton(self.ip)
        r += struct.pack(">H", self.port)
        return r

    def __repr__(self):
        return "CAddress(nServices=%i ip=%s port=%i)" % (self.nServices,
                                                         self.ip, self.port)


class CInv():
    typemap = {
        0: "Error",
        1: "TX",
        2: "Block",
    }

    def __init__(self, t=0, h=0):
        self.type = t
        self.hash = h

    def deserialize(self, f):
        self.type = struct.unpack("<i", f.read(4))[0]
        self.hash = deser_uint256(f)

    def serialize(self):
        r = b""
        r += struct.pack("<i", self.type)
        r += ser_uint256(self.hash)
        return r

    def __repr__(self):
        return "CInv(type=%s hash=%064x)" \
            % (self.typemap[self.type], self.hash)


class CBlockLocator():
    def __init__(self):
        self.nVersion = MY_VERSION
        self.vHave = []

    def deserialize(self, f):
        self.nVersion = struct.unpack("<i", f.read(4))[0]
        self.vHave = deser_uint256_vector(f)

    def serialize(self):
        r = b""
        r += struct.pack("<i", self.nVersion)
        r += ser_uint256_vector(self.vHave)
        return r

    def __repr__(self):
        return "CBlockLocator(nVersion=%i vHave=%s)" \
            % (self.nVersion, repr(self.vHave))


class COutPoint():
    def __init__(self, hash=0, n=0):
        self.hash = hash
        self.n = n

    def deserialize(self, f):
        self.hash = deser_uint256(f)
        self.n = struct.unpack("<I", f.read(4))[0]

    def serialize(self):
        r = b""
        r += ser_uint256(self.hash)
        r += struct.pack("<I", self.n)
        return r

    def __repr__(self):
        return "COutPoint(hash=%064x n=%i)" % (self.hash, self.n)


class CTxIn():
    def __init__(self, outpoint=None, scriptSig=b"", nSequence=0):
        if outpoint is None:
            self.prevout = COutPoint()
        else:
            self.prevout = outpoint
        self.scriptSig = scriptSig
        self.nSequence = nSequence

    def deserialize(self, f):
        self.prevout = COutPoint()
        self.prevout.deserialize(f)
        self.scriptSig = deser_string(f)
        self.nSequence = struct.unpack("<I", f.read(4))[0]

    def serialize(self):
        r = b""
        r += self.prevout.serialize()
        r += ser_string(self.scriptSig)
        r += struct.pack("<I", self.nSequence)
        return r

    def __repr__(self):
        return "CTxIn(prevout=%s scriptSig=%s nSequence=%i)" \
            % (repr(self.prevout), bytes_to_hex_str(self.scriptSig),
               self.nSequence)


class CTxOut():
    def __init__(self, nValue=0, scriptPubKey=b""):
        self.nValue = nValue
        self.scriptPubKey = scriptPubKey

    def deserialize(self, f):
        self.nValue = struct.unpack("<q", f.read(8))[0]
        self.scriptPubKey = deser_string(f)

    def serialize(self):
        r = b""
        r += struct.pack("<q", self.nValue)
        r += ser_string(self.scriptPubKey)
        return r

    def __repr__(self):
        return "CTxOut(nValue=%i.%08i scriptPubKey=%s)" \
            % (self.nValue // COIN, self.nValue % COIN,
               bytes_to_hex_str(self.scriptPubKey))


class CTransaction():
    def __init__(self, tx=None):
        if tx is None:
            self.nVersion = 1
            self.vin = []
            self.vout = []
            self.nLockTime = 0
            self.sha256 = None
            self.hash = None
        else:
            self.nVersion = tx.nVersion
            self.vin = copy.deepcopy(tx.vin)
            self.vout = copy.deepcopy(tx.vout)
            self.nLockTime = tx.nLockTime
            self.sha256 = tx.sha256
            self.hash = tx.hash

    def deserialize(self, f):
        self.nVersion = struct.unpack("<i", f.read(4))[0]
        self.vin = deser_vector(f, CTxIn)
        flags = 0
        if len(self.vin) == 0:
            flags = struct.unpack("<B", f.read(1))[0]
            # Not sure why flags can't be zero, but this
            # matches the implementation in bitcoind
            if (flags != 0):
                self.vin = deser_vector(f, CTxIn)
                self.vout = deser_vector(f, CTxOut)
        else:
            self.vout = deser_vector(f, CTxOut)
        self.nLockTime = struct.unpack("<I", f.read(4))[0]
        self.sha256 = None
        self.hash = None

    def serialize_without_witness(self):
        r = b""
        r += struct.pack("<i", self.nVersion)
        r += ser_vector(self.vin)
        r += ser_vector(self.vout)
        r += struct.pack("<I", self.nLockTime)
        return r

    # Regular serialization is with witness -- must explicitly
    # call serialize_without_witness to exclude witness data.
    def serialize(self):
        return self.serialize_without_witness()

    # Recalculate the txid (transaction hash without witness)
    def rehash(self):
        self.sha256 = None
        self.calc_sha256()

    # We will only cache the serialization without witness in
    # self.sha256 and self.hash -- those are expected to be the txid.
    def calc_sha256(self, with_witness=False):
        if self.sha256 is None:
            self.sha256 = uint256_from_str(hash256(self.serialize_without_witness()))
        self.hash = encode(hash256(self.serialize_without_witness())[::-1], 'hex_codec').decode('ascii')

    def is_valid(self):
        self.calc_sha256()
        for tout in self.vout:
            if tout.nValue < 0 or tout.nValue > 21000000 * COIN:
                return False
        return True

    def __repr__(self):
        return "CTransaction(nVersion=%i vin=%s vout=%s nLockTime=%i)" \
            % (self.nVersion, repr(self.vin), repr(self.vout), self.nLockTime)


class CBlockHeader():
    def __init__(self, header=None):
        if header is None:
            self.set_null()
        else:
            self.nVersion = header.nVersion
            self.hashPrevBlock = header.hashPrevBlock
            self.hashMerkleRoot = header.hashMerkleRoot
            self.nTime = header.nTime
            self.nBits = header.nBits
            self.nNonce = header.nNonce
            self.nAccumulatorCheckpoint = header.nAccumulatorCheckpoint
            self.sha256 = header.sha256
            self.hash = header.hash
            self.calc_sha256()

    def set_null(self):
        self.nVersion = 4
        self.hashPrevBlock = 0
        self.hashMerkleRoot = 0
        self.nTime = 0
        self.nBits = 0
        self.nNonce = 0
        self.nAccumulatorCheckpoint = 0
        self.sha256 = None
        self.hash = None

    def deserialize(self, f):
        self.nVersion = struct.unpack("<i", f.read(4))[0]
        self.hashPrevBlock = deser_uint256(f)
        self.hashMerkleRoot = deser_uint256(f)
        self.nTime = struct.unpack("<I", f.read(4))[0]
        self.nBits = struct.unpack("<I", f.read(4))[0]
        self.nNonce = struct.unpack("<I", f.read(4))[0]
        self.nAccumulatorCheckpoint = deser_uint256(f)
        self.sha256 = None
        self.hash = None

    def serialize(self):
        r = b""
        r += struct.pack("<i", self.nVersion)
        r += ser_uint256(self.hashPrevBlock)
        r += ser_uint256(self.hashMerkleRoot)
        r += struct.pack("<I", self.nTime)
        r += struct.pack("<I", self.nBits)
        r += struct.pack("<I", self.nNonce)
        r += ser_uint256(self.nAccumulatorCheckpoint)
        return r

    def calc_sha256(self):
        if self.sha256 is None:
            r = b""
            r += struct.pack("<i", self.nVersion)
            r += ser_uint256(self.hashPrevBlock)
            r += ser_uint256(self.hashMerkleRoot)
            r += struct.pack("<I", self.nTime)
            r += struct.pack("<I", self.nBits)
            r += struct.pack("<I", self.nNonce)
            r += ser_uint256(self.nAccumulatorCheckpoint)
            self.sha256 = uint256_from_str(hash256(r))
            self.hash = encode(hash256(r)[::-1], 'hex_codec').decode('ascii')

    def rehash(self):
        self.sha256 = None
        self.calc_sha256()
        return self.sha256

    # GIC Uniqueness
    def get_uniqueness(self, prevout):
        r = b""
        r += struct.pack("<I", prevout.n)
        r += ser_uint256(prevout.hash)
        return r

    def solve_stake(self, prevouts):
        target0 = uint256_from_compact(self.nBits)
        loop = True
        while loop:
            for prevout in prevouts:
                nvalue, txBlockTime, stakeModifier, hashStake = prevouts[prevout]
                target = int(target0 * nvalue / 100) % 2**256
                data = b""
                data += ser_uint64(stakeModifier)
                data += struct.pack("<I", txBlockTime)
                # prevout for zPoS is serial hashes hex strings
                if isinstance(prevout, COutPoint):
                    data += self.get_uniqueness(prevout)
                else:
                    data += ser_uint256(uint256_from_str(bytes.fromhex(hashStake)[::-1]))
                data += struct.pack("<I", self.nTime)
                posHash = uint256_from_str(hash256(data))
                if posHash <= target:
                    self.prevoutStake = prevout
                    loop = False
                    break
            if loop:
                self.nTime += 1
        return True

    def __repr__(self):
        return "CBlockHeader(nVersion=%i hashPrevBlock=%064x hashMerkleRoot=%064x nTime=%s nBits=%08x nNonce=%08x)" \
            % (self.nVersion, self.hashPrevBlock, self.hashMerkleRoot,
               time.ctime(self.nTime), self.nBits, self.nNonce)


class CBlock(CBlockHeader):
    def __init__(self, header=None):
        super(CBlock, self).__init__(header)
        self.vtx = []

    def deserialize(self, f):
        super(CBlock, self).deserialize(f)
        self.vtx = deser_vector(f, CTransaction)

    def serialize(self, with_witness=False):
        r = b""
        r += super(CBlock, self).serialize()
        if with_witness:
            r += ser_vector(self.vtx, "serialize_with_witness")
        else:
            r += ser_vector(self.vtx, "serialize_without_witness")
            if hasattr(self, 'vchBlockSig'):
                r += ser_string(self.vchBlockSig)
        return r

    # Calculate the merkle root given a vector of transaction hashes
    @classmethod
    def get_merkle_root(cls, hashes):
        while len(hashes) > 1:
            newhashes = []
            for i in range(0, len(hashes), 2):
                i2 = min(i+1, len(hashes)-1)
                newhashes.append(hash256(hashes[i] + hashes[i2]))
            hashes = newhashes
        return uint256_from_str(hashes[0])

    def calc_merkle_root(self):
        hashes = []
        for tx in self.vtx:
            tx.calc_sha256()
            hashes.append(ser_uint256(tx.sha256))
        return self.get_merkle_root(hashes)

    def calc_witness_merkle_root(self):
        # For witness root purposes, the hash of the
        # coinbase, with witness, is defined to be 0...0
        hashes = [ser_uint256(0)]

        for tx in self.vtx[1:]:
            # Calculate the hashes with witness data
            hashes.append(ser_uint256(tx.calc_sha256(True)))

        return self.get_merkle_root(hashes)

    def is_valid(self):
        self.calc_sha256()
        target = uint256_from_compact(self.nBits)
        if self.sha256 > target:
            return False
        for tx in self.vtx:
            if not tx.is_valid():
                return False
        if self.calc_merkle_root() != self.hashMerkleRoot:
            return False
        return True

    def solve(self):
        self.rehash()
        target = uint256_from_compact(self.nBits)
        while self.sha256 > target:
            self.nNonce += 1
            self.rehash()

    def sign_block(self, key, low_s=True):
        data = b""
        data += struct.pack("<i", self.nVersion)
        data += ser_uint256(self.hashPrevBlock)
        data += ser_uint256(self.hashMerkleRoot)
        data += struct.pack("<I", self.nTime)
        data += struct.pack("<I", self.nBits)
        data += struct.pack("<I", self.nNonce)
        data += ser_uint256(self.nAccumulatorCheckpoint)
        sha256NoSig = hash256(data)
        self.vchBlockSig = key.sign(sha256NoSig, low_s=low_s)

    def __repr__(self):
        return "CBlock(nVersion=%i hashPrevBlock=%064x hashMerkleRoot=%064x nTime=%s nBits=%08x nNonce=%08x vtx=%s)" \
            % (self.nVersion, self.hashPrevBlock, self.hashMerkleRoot,
               time.ctime(self.nTime), self.nBits, self.nNonce, repr(self.vtx))


class PrefilledTransaction():
    def __init__(self, index=0, tx = None):
        self.index = index
        self.tx = tx

    def deserialize(self, f):
        self.index = deser_compact_size(f)
        self.tx = CTransaction()
        self.tx.deserialize(f)

    def serialize(self, with_witness=True):
        r = b""
        r += ser_compact_size(self.index)
        if with_witness:
            r += self.tx.serialize_with_witness()
        else:
            r += self.tx.serialize_without_witness()
        return r

    def serialize_without_witness(self):
        return self.serialize(with_witness=False)

    def serialize_with_witness(self):
        return self.serialize(with_witness=True)

    def __repr__(self):
        return "PrefilledTransaction(index=%d, tx=%s)" % (self.index, repr(self.tx))

# This is what we send on the wire, in a cmpctblock message.
class P2PHeaderAndShortIDs():
    def __init__(self):
        self.header = CBlockHeader()
        self.nonce = 0
        self.shortids_length = 0
        self.shortids = []
        self.prefilled_txn_length = 0
        self.prefilled_txn = []

    def deserialize(self, f):
        self.header.deserialize(f)
        self.nonce = struct.unpack("<Q", f.read(8))[0]
        self.shortids_length = deser_compact_size(f)
        for i in range(self.shortids_length):
            # shortids are defined to be 6 bytes in the spec, so append
            # two zero bytes and read it in as an 8-byte number
            self.shortids.append(struct.unpack("<Q", f.read(6) + b'\x00\x00')[0])
        self.prefilled_txn = deser_vector(f, PrefilledTransaction)
        self.prefilled_txn_length = len(self.prefilled_txn)

    # When using version 2 compact blocks, we must serialize with_witness.
    def serialize(self, with_witness=False):
        r = b""
        r += self.header.serialize()
        r += struct.pack("<Q", self.nonce)
        r += ser_compact_size(self.shortids_length)
        for x in self.shortids:
            # We only want the first 6 bytes
            r += struct.pack("<Q", x)[0:6]
        if with_witness:
            r += ser_vector(self.prefilled_txn, "serialize_with_witness")
        else:
            r += ser_vector(self.prefilled_txn, "serialize_without_witness")
        return r

    def __repr__(self):
        return "P2PHeaderAndShortIDs(header=%s, nonce=%d, shortids_length=%d, shortids=%s, prefilled_txn_length=%d, prefilledtxn=%s" % (repr(self.header), self.nonce, self.shortids_length, repr(self.shortids), self.prefilled_txn_length, repr(self.prefilled_txn))

# P2P version of the above that will use witness serialization (for compact
# block version 2)
class P2PHeaderAndShortWitnessIDs(P2PHeaderAndShortIDs):
    def serialize(self):
        return super(P2PHeaderAndShortWitnessIDs, self).serialize(with_witness=True)

# Calculate the BIP 152-compact blocks shortid for a given transaction hash
def calculate_shortid(k0, k1, tx_hash):
    expected_shortid = siphash256(k0, k1, tx_hash)
    expected_shortid &= 0x0000ffffffffffff
    return expected_shortid

# This version gets rid of the array lengths, and reinterprets the differential
# encoding into indices that can be used for lookup.
class HeaderAndShortIDs():
    def __init__(self, p2pheaders_and_shortids = None):
        self.header = CBlockHeader()
        self.nonce = 0
        self.shortids = []
        self.prefilled_txn = []
        self.use_witness = False

        if p2pheaders_and_shortids != None:
            self.header = p2pheaders_and_shortids.header
            self.nonce = p2pheaders_and_shortids.nonce
            self.shortids = p2pheaders_and_shortids.shortids
            last_index = -1
            for x in p2pheaders_and_shortids.prefilled_txn:
                self.prefilled_txn.append(PrefilledTransaction(x.index + last_index + 1, x.tx))
                last_index = self.prefilled_txn[-1].index

    def to_p2p(self):
        if self.use_witness:
            ret = P2PHeaderAndShortWitnessIDs()
        else:
            ret = P2PHeaderAndShortIDs()
        ret.header = self.header
        ret.nonce = self.nonce
        ret.shortids_length = len(self.shortids)
        ret.shortids = self.shortids
        ret.prefilled_txn_length = len(self.prefilled_txn)
        ret.prefilled_txn = []
        last_index = -1
        for x in self.prefilled_txn:
            ret.prefilled_txn.append(PrefilledTransaction(x.index - last_index - 1, x.tx))
            last_index = x.index
        return ret

    def get_siphash_keys(self):
        header_nonce = self.header.serialize()
        header_nonce += struct.pack("<Q", self.nonce)
        hash_header_nonce_as_str = sha256(header_nonce)
        key0 = struct.unpack("<Q", hash_header_nonce_as_str[0:8])[0]
        key1 = struct.unpack("<