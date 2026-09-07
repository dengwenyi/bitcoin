// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <policy/fees/block_policy_estimator.h>

#include <policy/feerate.h>
#include <random.h>
#include <sync.h>

#include <algorithm>
#include <set>

namespace {
std::set<double> MakeFeeSet(const CFeeRate& min_incremental_fee,
                            double max_filter_fee_rate,
                            double fee_filter_spacing)
{
    std::set<double> fee_set;

    const CAmount min_fee_limit{std::max(CAmount{1}, min_incremental_fee.GetFeePerK() / 2)};
    fee_set.insert(0);
    for (double bucket_boundary = min_fee_limit;
         bucket_boundary <= max_filter_fee_rate;
         bucket_boundary *= fee_filter_spacing) {
        fee_set.insert(bucket_boundary);
    }
    return fee_set;
}
} // namespace

FeeFilterRounder::FeeFilterRounder(const CFeeRate& min_incremental_fee, FastRandomContext& rng)
    : m_fee_set{MakeFeeSet(min_incremental_fee, MAX_FILTER_FEERATE, FEE_FILTER_SPACING)},
      insecure_rand{rng}
{
}

CAmount FeeFilterRounder::round(CAmount current_min_fee)
{
    AssertLockNotHeld(m_insecure_rand_mutex);
    std::set<double>::iterator it = m_fee_set.lower_bound(current_min_fee);
    if (it == m_fee_set.end() ||
        (it != m_fee_set.begin() &&
         WITH_LOCK(m_insecure_rand_mutex, return insecure_rand.rand32()) % 3 != 0)) {
        --it;
    }
    return static_cast<CAmount>(*it);
}
