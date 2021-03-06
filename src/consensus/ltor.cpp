// Copyright (c) 2019 The Unit-e developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <algorithm>
#include <consensus/ltor.h>

namespace ltor {

void SortTransactions(std::vector<CTransactionRef>& transactions) {
    if (transactions.size() <= 2) {
        // The first transaction has to be coinbase, and having just one
        // regular transaction does not require sorting neither.
        return;
    }

    // LTOR/CTOR: We ensure that all transactions (except the 0th, coinbase) are
    // sorted in lexicographical order.
    std::sort(
        std::begin(transactions) + 1, std::end(transactions),
        [](const CTransactionRef &a, const CTransactionRef &b) -> bool {
          return a->GetHash().CompareAsNumber(b->GetHash()) < 0;
        }
    );
}

}
