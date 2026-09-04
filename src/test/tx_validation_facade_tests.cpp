// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/validation.h>
#include <node/tx_validation_facade.h>
#include <primitives/transaction.h>
#include <test/util/setup_common.h>
#include <txmempool.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(tx_validation_facade_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(rejects_coinbase_transaction_and_package)
{
    auto facade{node::MakeTxValidationFacade(*m_node.chainman, *m_node.mempool)};
    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vin[0].scriptSig = CScript{} << OP_0 << OP_0;
    coinbase.vout.resize(1);
    coinbase.vout[0].nValue = 1;
    const CTransactionRef transaction{MakeTransactionRef(coinbase)};
    BOOST_REQUIRE(transaction->IsCoinBase());

    LOCK(cs_main);
    const size_t initial_pool_size{m_node.mempool->size()};
    const MempoolAcceptResult transaction_result{facade->ProcessTransaction(transaction)};
    BOOST_CHECK(transaction_result.m_result_type == MempoolAcceptResult::ResultType::INVALID);
    BOOST_CHECK_EQUAL(transaction_result.m_state.GetRejectReason(), "coinbase");
    BOOST_CHECK_EQUAL(m_node.mempool->size(), initial_pool_size);

    const PackageMempoolAcceptResult package_result{facade->ProcessPackage({transaction})};
    const auto package_tx_result{package_result.m_tx_results.find(transaction->GetWitnessHash())};
    BOOST_REQUIRE(package_tx_result != package_result.m_tx_results.end());
    BOOST_CHECK(package_tx_result->second.m_result_type == MempoolAcceptResult::ResultType::INVALID);
    BOOST_CHECK_EQUAL(package_tx_result->second.m_state.GetRejectReason(), "coinbase");
    BOOST_CHECK_EQUAL(m_node.mempool->size(), initial_pool_size);
}

BOOST_AUTO_TEST_SUITE_END()
