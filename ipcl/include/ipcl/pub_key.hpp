// Copyright (C) 2021-2022 Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef IPCL_INCLUDE_IPCL_PUB_KEY_HPP_
#define IPCL_INCLUDE_IPCL_PUB_KEY_HPP_

#include "ipcl/bignum.h"

class PaillierPublicKey {
 public:
  /**
   * PaillierPublicKey construct function
   * @param[in] n n of public key in paillier paper
   * @param[in] bits bit length of public key
   */
  explicit PaillierPublicKey(const BigNumber& n, int bits = 1024,
                             bool enableDJN_ = false);

  /**
   * PaillierPublicKey construct function
   * @param[in] n n of public key in paillier paper
   * @param[in] bits bit length of public key
   */
  explicit PaillierPublicKey(const Ipp32u n, int bits = 1024,
                             bool enableDJN_ = false)
      : PaillierPublicKey(BigNumber(n), bits, enableDJN_) {}

  /**
   * DJN enabling function
   */
  void enableDJN();

  /**
   * Encrypt plaintext
   * @param[out] ciphertext output of the encryption
   * @param[in] value array plaintext need to be encrypted
   * @param[in] make_secure apply obfuscator
   */
  void encrypt(BigNumber ciphertext[8], const BigNumber value[8],
               bool make_secure = true);

  /**
   * Encrypt plaintext
   * @param[out] ciphertext output of the encryption
   * @param[in] value plaintext need to be encrypted
   */
  void encrypt(BigNumber& ciphertext, const BigNumber& value);

  /**
   * Modular exponentiation
   * @param[in] base base of the exponentiation
   * @param[in] pow pow of the exponentiation
   * @param[in] m modular
   */
  BigNumber ippMontExp(const BigNumber& base, const BigNumber& pow,
                       const BigNumber& m);

  /**
   * Multi-buffered modular exponentiation
   * @param[out] res array result of the modular exponentiation
   * @param[in] base array base of the exponentiation
   * @param[in] pow arrya pow of the exponentiation
   * @param[in] m arrayodular
   */
  void ippMultiBuffExp(BigNumber res[8], BigNumber base[8],
                       const BigNumber pow[8], BigNumber m[8]);

  /**
   * Invert function needed by encoder(float to integer)
   * @param[in] a input of a
   * @param[in] b input of b
   */
  BigNumber IPP_invert(BigNumber a, BigNumber b);

  /**
   * Get N of public key in paillier paper
   */
  BigNumber getN() const { return m_n; }

  /**
   * Get NSQ of public key in paillier paper
   */
  BigNumber getNSQ() const { return m_nsquare; }

  /**
   * Get G of public key in paillier paper
   */
  BigNumber getG() const { return m_g; }

  /**
   * Get bits of key
   */
  int getBits() const { return m_bits; }

  /**
   * Get Dword of key
   */
  int getDwords() const { return m_dwords; }

  /**
   * Apply obfuscator for ciphertext
   * @param[out] obfuscator output of obfuscator with random value
   */
  void apply_obfuscator(BigNumber obfuscator[8]);

  const void* addr = static_cast<const void*>(this);

 private:
  BigNumber m_n;
  BigNumber m_g;
  BigNumber m_nsquare;
  BigNumber m_hs;
  int m_bits;
  int m_randbits;
  int m_dwords;
  unsigned int m_init_seed;
  bool m_enable_DJN;

  /**
   * Get random value
   * @param[in] addr addr of random
   * @param[in] size size of random
   */
  Ipp32u* randIpp32u(Ipp32u* addr, int size);

  /**
   * Raw encrypt function
   * @param[out] ciphertext array output of the encryption
   * @param[in] plaintext array plaintext need to be encrypted
   * @param[in] make_secure apply obfuscator
   */
  void raw_encrypt(BigNumber ciphertext[8], const BigNumber plaintext[8],
                   bool make_secure = true);

  /**
   * Get random value
   * @param[in] length bit length of random
   */
  BigNumber getRandom(int length);
};

#endif  // IPCL_INCLUDE_IPCL_PUB_KEY_HPP_
