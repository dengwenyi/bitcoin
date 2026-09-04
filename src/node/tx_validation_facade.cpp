// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <node/tx_validation_facade.h>

#include <txmempool.h>
#include <validation.h>

#include <optional>

namespace node {
namespace {

class TxValidationFacadeImpl final : public TxValidationFacade
{
public:
    TxValidationFacadeImpl(ChainstateManager& chainman, CTxMemPool& mempool)
        : m_chainman{chainman}, m_mempool{mempool}
    {
    }

    MempoolAcceptResult ProcessTransaction(const CTransactionRef& transaction, bool test_accept) override
    {
        return m_chainman.ProcessTransaction(transaction, test_accept);
    }

    PackageMempoolAcceptResult ProcessPackage(const std::vector<CTransactionRef>& transactions) override
    {
        return ProcessNewPackage(m_chainman.ActiveChainstate(), m_mempool, transactions,
                                 /*test_accept=*/false, /*client_maxfeerate=*/std::nullopt);
    }

private:
    ChainstateManager& m_chainman;
    CTxMemPool& m_mempool;
};

} // namespace

std::unique_ptr<TxValidationFacade> MakeTxValidationFacade(ChainstateManager& chainman, CTxMemPool& mempool)
{
    return std::make_unique<TxValidationFacadeImpl>(chainman, mempool);
}

} // namespace node
