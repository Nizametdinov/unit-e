// Copyright (c) 2018-2019 The Unit-e developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/esperanza/finalizationstate_utils.h>

BOOST_FIXTURE_TEST_SUITE(finalizationstate_deposit_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(validate_deposit_tx_not_enough_deposit) {

  FinalizationStateSpy spy;
  uint160 validatorAddress = RandValidatorAddr();
  CAmount depositSize = spy.MinDepositSize() - 1;

  BOOST_CHECK_EQUAL(spy.ValidateDeposit(validatorAddress, depositSize),
                    +Result::DEPOSIT_INSUFFICIENT);
}

BOOST_AUTO_TEST_CASE(validate_deposit_tx_double_deposit) {

  FinalizationStateSpy spy;

  uint160 validatorAddress = RandValidatorAddr();
  CAmount depositSize = spy.MinDepositSize();

  BOOST_CHECK_EQUAL(spy.ValidateDeposit(validatorAddress, depositSize),
                    +Result::SUCCESS);
  spy.ProcessDeposit(validatorAddress, depositSize);
  BOOST_CHECK_EQUAL(spy.ValidateDeposit(validatorAddress, depositSize),
                    +Result::DEPOSIT_DUPLICATE);
}

BOOST_AUTO_TEST_CASE(process_deposit_tx) {

  FinalizationStateSpy spy;
  uint160 validatorAddress = RandValidatorAddr();
  uint160 validatorAddress2 = RandValidatorAddr();
  CAmount depositSize = spy.MinDepositSize();

  BOOST_CHECK_EQUAL(spy.ValidateDeposit(validatorAddress, depositSize),
                    +Result::SUCCESS);
  BOOST_CHECK_EQUAL(spy.ValidateDeposit(validatorAddress2, depositSize),
                    +Result::SUCCESS);
  spy.ProcessDeposit(validatorAddress, depositSize);
  spy.ProcessDeposit(validatorAddress2, depositSize);

  std::map<uint160, Validator> validators = spy.Validators();
  auto it = validators.find(validatorAddress2);
  BOOST_CHECK(it != validators.end());

  it = validators.find(validatorAddress);
  BOOST_CHECK(it != validators.end());

  Validator validator = it->second;
  BOOST_CHECK_EQUAL(validator.m_start_dynasty, 3);  // assuming we start from 0
  BOOST_CHECK(validator.m_deposit > 0);
  BOOST_CHECK_EQUAL(it->first.GetHex(), validatorAddress.GetHex());
}

BOOST_AUTO_TEST_SUITE_END()
