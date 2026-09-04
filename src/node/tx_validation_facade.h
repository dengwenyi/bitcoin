// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NODE_TX_VALIDATION_FACADE_H
#define BITCOIN_NODE_TX_VALIDATION_FACADE_H

#include <primitives/transaction.h>

#include <memory>
#include <vector>

class ChainstateManager;
class CTxMemPool;
struct MempoolAcceptResult;
struct PackageMempoolAcceptResult;

namespace node {

/** Transaction-validation port used by P2P relay without exposing a Chainstate object. */
class TxValidationFacade
{
public:
    virtual ~TxValidationFacade() = default;

    virtual MempoolAcceptResult ProcessTransaction(const CTransactionRef& transaction,
                                                    bool test_accept = false) = 0;
    virtual PackageMempoolAcceptResult ProcessPackage(const std::vector<CTransactionRef>& transactions) = 0;
};

std::unique_ptr<TxValidationFacade> MakeTxValidationFacade(ChainstateManager& chainman, CTxMemPool& mempool);

} // namespace node

#endif // BITCOIN_NODE_TX_VALIDATION_FACADE_H
