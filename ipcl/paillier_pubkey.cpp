// Copyright (C) 2021 Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#include "ipcl/paillier_pubkey.hpp"

#include <crypto_mb/exp.h>

#include <climits>
#include <cstring>
#include <random>

namespace ipcl {

static inline auto randomUniformUnsignedInt() {
  std::random_device dev;
  std::mt19937 rng(dev());
  std::uniform_int_distribution<std::mt19937::result_type> dist(0, UINT_MAX);
  return dist(rng);
}

PaillierPublicKey::PaillierPublicKey(const BigNumber& n, int bits,
                                     bool enableDJN_)
    : m_n(n),
      m_g(n + 1),
      m_nsquare(n * n),
      m_bits(bits),
      m_dwords(BITSIZE_DWORD(bits * 2)),
      m_init_seed(randomUniformUnsignedInt()),
      m_enable_DJN(false),
      m_testv(false) {
  if (enableDJN_) this->enableDJN();  // this sets m_enable_DJN
}

// array of 32-bit random, using rand() from stdlib
void PaillierPublicKey::randIpp32u(std::vector<Ipp32u>& addr, int size) {
  for (auto& a : addr) a = (rand_r(&m_init_seed) << 16) + rand_r(&m_init_seed);
}

// length is Arbitery
BigNumber PaillierPublicKey::getRandom(int length) {
  IppStatus stat;
  int size;
  int seedBitSize = 160;
  int seedSize = BITSIZE_WORD(seedBitSize);

  auto seed = std::vector<Ipp32u>(seedSize);

  stat = ippsPRNGGetSize(&size);
  if (stat != ippStsNoErr)
    throw std::runtime_error("getRandom: get IppsPRNGState context error.");

  auto pRand = std::vector<Ipp8u>(size);

  stat =
      ippsPRNGInit(seedBitSize, reinterpret_cast<IppsPRNGState*>(pRand.data()));
  if (stat != ippStsNoErr)
    throw std::runtime_error("getRandom: init rand context error.");

  randIpp32u(seed, seedSize);
  BigNumber bseed(seed.data(), seedSize, IppsBigNumPOS);

  stat = ippsPRNGSetSeed(BN(bseed),
                         reinterpret_cast<IppsPRNGState*>(pRand.data()));
  if (stat != ippStsNoErr)
    throw std::runtime_error("getRandom: set up seed value error.");

  // define length Big Numbers
  int bn_size = BITSIZE_WORD(length);
  stat = ippsBigNumGetSize(bn_size, &size);
  if (stat != ippStsNoErr)
    throw std::runtime_error("getRandom: get IppsBigNumState context error.");

  IppsBigNumState* pBN = reinterpret_cast<IppsBigNumState*>(alloca(size));
  if (nullptr == pBN)
    throw std::runtime_error("getRandom: big number alloca error");

  stat = ippsBigNumInit(bn_size, pBN);
  if (stat != ippStsNoErr)
    throw std::runtime_error("getRandom: init big number context error.");

  int bnBitSize = length;
  ippsPRNGenRDRAND_BN(pBN, bnBitSize,
                      reinterpret_cast<IppsPRNGState*>(pRand.data()));
  BigNumber rand(pBN);

  return rand;
}

void PaillierPublicKey::enableDJN() {
  BigNumber gcd;
  BigNumber rmod;
  do {
    int rand_bit = m_n.BitSize();
    BigNumber rand = getRandom(rand_bit + 128);
    rmod = rand % m_n;
    gcd = rand.gcd(m_n);
  } while (gcd.compare(1));

  BigNumber rmod_sq = rmod * rmod;
  BigNumber rmod_neg = rmod_sq * -1;
  BigNumber h = rmod_neg % m_n;
  m_hs = ippMontExp(h, m_n, m_nsquare);
  m_randbits = m_bits >> 1;  // bits/2

  m_enable_DJN = true;
}

void PaillierPublicKey::apply_obfuscator(BigNumber obfuscator[8]) {
  std::vector<BigNumber> r(8);
  std::vector<BigNumber> pown(8, m_n);
  std::vector<BigNumber> base(8, m_hs);
  std::vector<BigNumber> sq(8, m_nsquare);

  if (m_enable_DJN) {
    for (auto& r_ : r) {
      r_ = getRandom(m_randbits);
    }
    ippMultiBuffExp(obfuscator, base.data(), r.data(), sq.data());
  } else {
    for (int i = 0; i < 8; i++) {
      if (m_testv) {
        r[i] = m_r[i];
      } else {
        r[i] = getRandom(m_bits);
        r[i] = r[i] % (m_n - 1) + 1;
      }
      pown[i] = m_n;
      sq[i] = m_nsquare;
    }
    ippMultiBuffExp(obfuscator, r.data(), pown.data(), sq.data());
  }
}

void PaillierPublicKey::raw_encrypt(BigNumber ciphertext[8],
                                    const BigNumber plaintext[8],
                                    bool make_secure) {
  // Based on the fact that: (n+1)^plaintext mod n^2 = n*plaintext + 1 mod n^2
  BigNumber sq = m_nsquare;
  for (int i = 0; i < 8; i++) {
    BigNumber bn(plaintext[i]);
    ciphertext[i] = (m_n * bn + 1) % sq;
  }

  if (make_secure) {
    BigNumber obfuscator[8];
    apply_obfuscator(obfuscator);

    for (int i = 0; i < 8; i++)
      ciphertext[i] = sq.ModMul(ciphertext[i], obfuscator[i]);
  }
}

void PaillierPublicKey::encrypt(BigNumber ciphertext[8],
                                const BigNumber value[8], bool make_secure) {
  raw_encrypt(ciphertext, value, make_secure);
}

// Used for CT+PT, where PT do not need to add obfuscator
void PaillierPublicKey::encrypt(BigNumber& ciphertext, const BigNumber& value) {
  // Based on the fact that: (n+1)^plaintext mod n^2 = n*plaintext + 1 mod n^2
  BigNumber bn = value;
  BigNumber sq = m_nsquare;
  ciphertext = (m_n * bn + 1) % sq;

  /*----- Path to caculate (n+1)^plaintext mod n^2 ------------
  BigNumber bn(value);
  ciphertext = ippMontExp(m_g, bn, m_nsquare);
  ---------------------------------------------------------- */
}

void PaillierPublicKey::ippMultiBuffExp(BigNumber res[8], BigNumber base[8],
                                        const BigNumber pow[8],
                                        BigNumber m[8]) {
  mbx_status st = MBX_STATUS_OK;
  int bits = m[0].BitSize();
  int dwords = BITSIZE_DWORD(bits);
  int bufferLen = mbx_exp_BufferSize(bits);
  auto pBuffer = std::vector<Ipp8u>(bufferLen);

  std::vector<int64u*> out_x(8), b_array(8), p_array(8);
  int length = dwords * sizeof(int64u);

  for (int i = 0; i < 8; i++) {
    out_x[i] = reinterpret_cast<int64u*>(alloca(length));
    b_array[i] = reinterpret_cast<int64u*>(alloca(length));
    p_array[i] = reinterpret_cast<int64u*>(alloca(length));

    if (out_x[i] == nullptr || b_array[i] == nullptr || p_array[i] == nullptr)
      throw std::runtime_error("ippMultiBuffExp: alloc memory for error");

    memset(out_x[i], 0, length);
    memset(b_array[i], 0, length);
    memset(p_array[i], 0, length);
  }

  /*
   * These two intermediate variables pow_b & pow_p are necessary
   * because if they are not used, the length returned from ippsRef_BN
   * will be inconsistent with the length allocated by b_array/p_array,
   * resulting in data errors.
   */
  std::vector<Ipp32u*> pow_b(8), pow_p(8), pow_nsquare(8);
  int bBitLen, pBitLen, nsqBitLen;
  int expBitLen = 0;

  for (int i = 0; i < 8; i++) {
    ippsRef_BN(nullptr, &bBitLen, reinterpret_cast<Ipp32u**>(&pow_b[i]),
               base[i]);
    ippsRef_BN(nullptr, &pBitLen, reinterpret_cast<Ipp32u**>(&pow_p[i]),
               pow[i]);
    ippsRef_BN(nullptr, &nsqBitLen, &pow_nsquare[i], m[i]);

    memcpy(b_array[i], pow_b[i], BITSIZE_WORD(bBitLen) * 4);
    memcpy(p_array[i], pow_p[i], BITSIZE_WORD(pBitLen) * 4);

    if (expBitLen < pBitLen) expBitLen = pBitLen;
  }

  /*
   *Note: If actual sizes of exp are different, set the exp_bits parameter equal
   *to maximum size of the actual module in bit size and extend all the modules
   *with zero bits
   */
  st = mbx_exp_mb8(out_x.data(), b_array.data(), p_array.data(), expBitLen,
                   reinterpret_cast<Ipp64u**>(pow_nsquare.data()), nsqBitLen,
                   reinterpret_cast<Ipp8u*>(pBuffer.data()), bufferLen);

  for (int i = 0; i < 8; i++) {
    if (MBX_STATUS_OK != MBX_GET_STS(st, i))
      throw std::runtime_error(
          std::string("error multi buffered exp modules, error code = ") +
          std::to_string(MBX_GET_STS(st, i)));
  }

  // It is important to hold a copy of nsquare for thread-safe purpose
  BigNumber bn_c(m[0]);

  for (int i = 0; i < 8; i++) {
    bn_c.Set(reinterpret_cast<Ipp32u*>(out_x[i]), BITSIZE_WORD(nsqBitLen),
             IppsBigNumPOS);
    res[i] = bn_c;
  }
}

BigNumber PaillierPublicKey::ippMontExp(const BigNumber& base,
                                        const BigNumber& pow,
                                        const BigNumber& m) {
  IppStatus stat = ippStsNoErr;
  // It is important to declear res * bform bit length refer to ipp-crypto spec:
  // R should not be less than the data length of the modulus m
  BigNumber res(m);

  int bnBitLen;
  Ipp32u* pow_m;
  ippsRef_BN(nullptr, &bnBitLen, &pow_m, BN(m));
  int nlen = BITSIZE_WORD(bnBitLen);

  int size;
  // define and initialize Montgomery Engine over Modulus N
  stat = ippsMontGetSize(IppsBinaryMethod, nlen, &size);
  if (stat != ippStsNoErr)
    throw std::runtime_error(
        "ippMontExp: get the size of IppsMontState context error.");

  auto pMont = std::vector<Ipp8u>(size);

  stat = ippsMontInit(IppsBinaryMethod, nlen,
                      reinterpret_cast<IppsMontState*>(pMont.data()));
  if (stat != ippStsNoErr)
    throw std::runtime_error("ippMontExp: init Mont context error.");

  stat =
      ippsMontSet(pow_m, nlen, reinterpret_cast<IppsMontState*>(pMont.data()));
  if (stat != ippStsNoErr)
    throw std::runtime_error("ippMontExp: set Mont input error.");

  // encode base into Montfomery form
  BigNumber bform(m);
  stat = ippsMontForm(BN(base), reinterpret_cast<IppsMontState*>(pMont.data()),
                      BN(bform));
  if (stat != ippStsNoErr)
    throw std::runtime_error(
        "ippMontExp: convet big number into Mont form error.");

  // compute R = base^pow mod N
  stat = ippsMontExp(BN(bform), BN(pow),
                     reinterpret_cast<IppsMontState*>(pMont.data()), BN(res));
  if (stat != ippStsNoErr)
    throw std::runtime_error(std::string("ippsMontExp: error code = ") +
                             std::to_string(stat));

  BigNumber one(1);
  // R = MontMul(R,1)
  stat = ippsMontMul(BN(res), BN(one),
                     reinterpret_cast<IppsMontState*>(pMont.data()), BN(res));
  if (stat != ippStsNoErr)
    throw std::runtime_error(std::string("ippsMontMul: error code = ") +
                             std::to_string(stat));

  return res;
}

BigNumber PaillierPublicKey::IPP_invert(BigNumber a, BigNumber b) {
  return b.InverseMul(a);
}

}  // namespace ipcl
