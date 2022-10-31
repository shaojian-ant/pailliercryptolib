// Copyright (C) 2021 Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef IPCL_INCLUDE_IPCL_MOD_EXP_HPP_
#define IPCL_INCLUDE_IPCL_MOD_EXP_HPP_

#include <vector>

#include "ipcl/bignum.h"

namespace ipcl {

/**
 * Set the number of mod exp operatiions
 * @param[in] total total number of mod exp operations.
 * @param[in] qat number of mod exp operations using QAT
 */
void setHybridModExp(int total, int qat);

/**
 * Turn off hybrid mod exp.
 */
void setHybridModExpOff();

/**
 * Modular exponentiation for multi BigNumber
 * @param[in] base base of the exponentiation
 * @param[in] exp pow of the exponentiation
 * @param[in] mod modular
 * @return the modular exponentiation result of type BigNumber
 */
std::vector<BigNumber> modExp(const std::vector<BigNumber>& base,
                              const std::vector<BigNumber>& exp,
                              const std::vector<BigNumber>& mod);
/**
 * Modular exponentiation for single BigNumber
 * @param[in] base base of the exponentiation
 * @param[in] exp pow of the exponentiation
 * @param[in] mod modular
 * @return the modular exponentiation result of type BigNumber
 */
BigNumber modExp(const BigNumber& base, const BigNumber& exp,
                 const BigNumber& mod);

/**
 * IPP modular exponentiation for multi buffer
 * @param[in] base base of the exponentiation
 * @param[in] exp pow of the exponentiation
 * @param[in] mod modular
 * @return the modular exponentiation result of type BigNumber
 */
std::vector<BigNumber> ippModExp(const std::vector<BigNumber>& base,
                                 const std::vector<BigNumber>& exp,
                                 const std::vector<BigNumber>& mod);

/**
 * IPP modular exponentiation for single buffer
 * @param[in] base base of the exponentiation
 * @param[in] exp pow of the exponentiation
 * @param[in] mod modular
 * @return the modular exponentiation result of type BigNumber
 */
BigNumber ippModExp(const BigNumber& base, const BigNumber& exp,
                    const BigNumber& mod);

/**
 * QAT modular exponentiation for multi BigNumber
 * @param[in] base base of the exponentiation
 * @param[in] exp pow of the exponentiation
 * @param[in] mod modular
 * @return the modular exponentiation result of type BigNumber
 */
std::vector<BigNumber> qatModExp(const std::vector<BigNumber>& base,
                                 const std::vector<BigNumber>& exp,
                                 const std::vector<BigNumber>& mod);

}  // namespace ipcl
#endif  // IPCL_INCLUDE_IPCL_MOD_EXP_HPP_
