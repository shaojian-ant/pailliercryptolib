// Copyright (C) 2021 Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#include <benchmark/benchmark.h>

#include <vector>
#ifdef IPCL_BENCHMARK_OMP
#include <omp.h>
#endif

#include "ipcl/paillier_keygen.hpp"

static void BM_KeyGen(benchmark::State& state) {
  int64_t n_length = state.range(0);
  for (auto _ : state) {
    keyPair key = generateKeypair(n_length, true);
  }
}
BENCHMARK(BM_KeyGen)->Unit(benchmark::kMicrosecond)->Args({1024})->Args({2048});

static void BM_Encrypt(benchmark::State& state) {
  size_t dsize = state.range(0);
  keyPair key = generateKeypair(2048, true);

  BigNumber** pt = new BigNumber*[dsize];
  BigNumber** ct = new BigNumber*[dsize];

  for (size_t i = 0; i < dsize; ++i) {
    pt[i] = new BigNumber[8];
    ct[i] = new BigNumber[8];
    pt[i][0] = BigNumber((unsigned int)i);
  }

  for (auto _ : state) {
    for (size_t i = 0; i < dsize; ++i) key.pub_key->encrypt(ct[i], pt[i]);
  }

  for (size_t i = 0; i < dsize; ++i) {
    delete[] pt[i];
    delete[] ct[i];
  }

  delete[] pt;
  delete[] ct;
}

BENCHMARK(BM_Encrypt)->Unit(benchmark::kMicrosecond)->Args({16})->Args({64});

static void BM_Encrypt_buff8(benchmark::State& state) {
  size_t dsize = state.range(0);
  keyPair key = generateKeypair(2048, true);

  BigNumber** pt = new BigNumber*[dsize / 8];
  BigNumber** ct = new BigNumber*[dsize / 8];

  for (size_t i = 0; i < dsize / 8; ++i) {
    pt[i] = new BigNumber[8];
    ct[i] = new BigNumber[8];
    for (size_t j = 0; j < 8; ++j)
      pt[i][j] = BigNumber((unsigned int)(i * 8 + j));
  }

  for (auto _ : state) {
    for (size_t i = 0; i < dsize / 8; ++i) key.pub_key->encrypt(ct[i], pt[i]);
  }

  for (size_t i = 0; i < dsize / 8; ++i) {
    delete[] pt[i];
    delete[] ct[i];
  }
  delete[] pt;
  delete[] ct;
}

BENCHMARK(BM_Encrypt_buff8)
    ->Unit(benchmark::kMicrosecond)
    ->Args({16})
    ->Args({64});

static void BM_Decrypt(benchmark::State& state) {
  size_t dsize = state.range(0);
  keyPair key = generateKeypair(2048, true);

  BigNumber** pt = new BigNumber*[dsize];
  BigNumber** ct = new BigNumber*[dsize];
  BigNumber** de_ct = new BigNumber*[dsize];
  for (size_t i = 0; i < dsize; ++i) {
    pt[i] = new BigNumber[8];
    ct[i] = new BigNumber[8];
    de_ct[i] = new BigNumber[8];
    pt[i][0] = BigNumber((unsigned int)i);
  }

  for (size_t i = 0; i < dsize; ++i) key.pub_key->encrypt(ct[i], pt[i]);

  for (auto _ : state) {
    for (size_t i = 0; i < dsize; ++i) {
      key.priv_key->decrypt(de_ct[i], ct[i]);
    }
  }

  for (size_t i = 0; i < dsize; ++i) {
    delete[] pt[i];
    delete[] ct[i];
    delete[] de_ct[i];
  }

  delete[] pt;
  delete[] ct;
  delete[] de_ct;
}

BENCHMARK(BM_Decrypt)->Unit(benchmark::kMicrosecond)->Args({16})->Args({64});

static void BM_Decrypt_buff8(benchmark::State& state) {
  size_t dsize = state.range(0);
  keyPair key = generateKeypair(2048, true);

  BigNumber** pt = new BigNumber*[dsize / 8];
  BigNumber** ct = new BigNumber*[dsize / 8];
  BigNumber** de_ct = new BigNumber*[dsize / 8];

  for (size_t i = 0; i < dsize / 8; ++i) {
    pt[i] = new BigNumber[8];
    ct[i] = new BigNumber[8];
    de_ct[i] = new BigNumber[8];
    for (size_t j = 0; j < 8; ++j)
      pt[i][j] = BigNumber((unsigned int)(i * 8 + j));
  }

  for (size_t i = 0; i < dsize / 8; ++i) key.pub_key->encrypt(ct[i], pt[i]);

  for (auto _ : state) {
    for (size_t i = 0; i < dsize / 8; ++i)
      key.priv_key->decrypt(de_ct[i], ct[i]);
  }

  for (size_t i = 0; i < dsize / 8; ++i) {
    delete[] pt[i];
    delete[] ct[i];
    delete[] de_ct[i];
  }
  delete[] pt;
  delete[] ct;
  delete[] de_ct;
}

BENCHMARK(BM_Decrypt_buff8)
    ->Unit(benchmark::kMicrosecond)
    ->Args({16})
    ->Args({64});

#ifdef IPCL_BENCHMARK_OMP
static void BM_Encrypt_OMP(benchmark::State& state) {
  size_t dsize = state.range(0);
  keyPair key = generateKeypair(2048, true);

  BigNumber** pt = new BigNumber*[dsize];
  BigNumber** ct = new BigNumber*[dsize];

  for (size_t i = 0; i < dsize; ++i) {
    pt[i] = new BigNumber[8];
    ct[i] = new BigNumber[8];
    pt[i][0] = BigNumber((unsigned int)i);
  }

  for (auto _ : state) {
#pragma omp parallel for
    for (size_t i = 0; i < dsize; ++i) key.pub_key->encrypt(ct[i], pt[i]);
  }

  for (size_t i = 0; i < dsize; ++i) {
    delete[] pt[i];
    delete[] ct[i];
  }

  delete[] pt;
  delete[] ct;
}
BENCHMARK(BM_Encrypt_OMP)
    ->Unit(benchmark::kMicrosecond)
    ->Args({16})
    ->Args({64});

static void BM_Encrypt_buff8_OMP(benchmark::State& state) {
  size_t dsize = state.range(0);
  keyPair key = generateKeypair(2048, true);

  BigNumber** pt = new BigNumber*[dsize / 8];
  BigNumber** ct = new BigNumber*[dsize / 8];

  for (size_t i = 0; i < dsize / 8; ++i) {
    pt[i] = new BigNumber[8];
    ct[i] = new BigNumber[8];
    for (size_t j = 0; j < 8; ++j)
      pt[i][j] = BigNumber((unsigned int)(i * 8 + j));
  }

  for (auto _ : state) {
#pragma omp parallel for
    for (size_t i = 0; i < dsize / 8; ++i) key.pub_key->encrypt(ct[i], pt[i]);
  }

  for (size_t i = 0; i < dsize / 8; ++i) {
    delete[] pt[i];
    delete[] ct[i];
  }
  delete[] pt;
  delete[] ct;
}

BENCHMARK(BM_Encrypt_buff8_OMP)
    ->Unit(benchmark::kMicrosecond)
    ->Args({16})
    ->Args({64});

static void BM_Decrypt_OMP(benchmark::State& state) {
  size_t dsize = state.range(0);
  keyPair key = generateKeypair(2048, true);

  BigNumber** pt = new BigNumber*[dsize];
  BigNumber** ct = new BigNumber*[dsize];
  BigNumber** de_ct = new BigNumber*[dsize];
  for (size_t i = 0; i < dsize; ++i) {
    pt[i] = new BigNumber[8];
    ct[i] = new BigNumber[8];
    de_ct[i] = new BigNumber[8];
    pt[i][0] = BigNumber((unsigned int)i);
  }

  for (size_t i = 0; i < dsize; ++i) key.pub_key->encrypt(ct[i], pt[i]);

  for (auto _ : state) {
#pragma omp parallel for
    for (size_t i = 0; i < dsize; ++i) {
      key.priv_key->decrypt(de_ct[i], ct[i]);
    }
  }

  for (size_t i = 0; i < dsize; ++i) {
    delete[] pt[i];
    delete[] ct[i];
    delete[] de_ct[i];
  }

  delete[] pt;
  delete[] ct;
  delete[] de_ct;
}

BENCHMARK(BM_Decrypt_OMP)
    ->Unit(benchmark::kMicrosecond)
    ->Args({16})
    ->Args({64});

static void BM_Decrypt_buff8_OMP(benchmark::State& state) {
  size_t dsize = state.range(0);
  keyPair key = generateKeypair(2048, true);

  BigNumber** pt = new BigNumber*[dsize / 8];
  BigNumber** ct = new BigNumber*[dsize / 8];
  BigNumber** de_ct = new BigNumber*[dsize / 8];

  for (size_t i = 0; i < dsize / 8; ++i) {
    pt[i] = new BigNumber[8];
    ct[i] = new BigNumber[8];
    de_ct[i] = new BigNumber[8];
    for (size_t j = 0; j < 8; ++j)
      pt[i][j] = BigNumber((unsigned int)(i * 8 + j));
  }

  for (size_t i = 0; i < dsize / 8; ++i) key.pub_key->encrypt(ct[i], pt[i]);

  for (auto _ : state) {
#pragma omp parallel for
    for (size_t i = 0; i < dsize / 8; ++i)
      key.priv_key->decrypt(de_ct[i], ct[i]);
  }

  for (size_t i = 0; i < dsize / 8; ++i) {
    delete[] pt[i];
    delete[] ct[i];
    delete[] de_ct[i];
  }
  delete[] pt;
  delete[] ct;
  delete[] de_ct;
}

BENCHMARK(BM_Decrypt_buff8_OMP)
    ->Unit(benchmark::kMicrosecond)
    ->Args({16})
    ->Args({64});
#endif
