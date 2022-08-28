/**********************************************************************
 * Copyright (c) 2014-2015 Pieter Wuille                              *
 * Distributed under the MIT software license, see the accompanying   *
 * file COPYING or http://www.opensource.org/licenses/mit-license.php.*
 **********************************************************************/
#include <stdio.h>

#include "include/secp256k1.h"

#include "util.h"
#include "hash_impl.h"
#include "num_impl.h"
#include "field_impl.h"
#include "group_impl.h"
#include "scalar_impl.h"
#include "ecmult_const_impl.h"
#include "ecmult_impl.h"
#include "bench.h"
#include "secp256k1.c"

typedef struct {
    secp256k1_scalar scalar_x, scalar_y;
    secp256k1_fe fe_x, fe_y;
    secp256k1_ge ge_x, ge_y;
    secp256k1_gej gej_x, gej_y;
    unsigned char data[64];
    int wnaf[256];
} bench_inv;

void bench_setup(void* arg) {
    bench_inv *data = (bench_inv*)arg;

  