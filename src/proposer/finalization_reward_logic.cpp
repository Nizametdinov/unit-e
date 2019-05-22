// Copyright (c) 2019 The Unit-e developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/licenses/MIT.

#include <proposer/finalization_reward_logic.h>
#include <util.h>

#include <boost/optional.hpp>

#include <algorithm>

namespace proposer {

class FinalizationRewardLogicImpl : public FinalizationRewardLogic {
 private:
  const Dependency<blockchain::Behavior> m_blockchain_behavior;
  const Dependency<finalization::Params> m_finalization_params;
  const Dependency<finalization::StateRepository> m_fin_state_repo;
  const Dependency<BlockDB> m_block_db;

  CScript GetRewardScript(const CBlockIndex &index) const {
    // TODO UNIT-E: implement more efficient reward script retrieval
    boost::optional<CBlock> block = m_block_db->ReadBlock(index);
    if (!block) {
      LogPrintf("Cannot read block=%s.\n", index.GetBlockHash().GetHex());
      throw MissingBlockError(index);
    }
    return block->vtx[0]->vout[0].scriptPubKey;
  }

 public:
  FinalizationRewardLogicImpl(const Dependency<blockchain::Behavior> blockchain_behavior,
                              const Dependency<finalization::Params> finalization_params,
                              const Dependency<finalization::StateRepository> repo,
                              const Dependency<BlockDB> block_db)
      : m_blockchain_behavior(blockchain_behavior),
        m_finalization_params(finalization_params),
        m_fin_state_repo(repo),
        m_block_db(block_db) {
  }

  std::vector<CTxOut> GetFinalizationRewards(const CBlockIndex &last_block) const override {
    if (last_block.nHeight < m_finalization_params->GetEpochCheckpointHeight(1)) {
      return {};
    }

    const auto prev_height = static_cast<blockchain::Height>(last_block.nHeight);
    if (!m_finalization_params->IsCheckpoint(prev_height)) {
      return {};
    }

    std::vector<CTxOut> result;
    result.reserve(m_finalization_params->epoch_length);
    const auto epoch = m_finalization_params->GetEpoch(prev_height);
    const blockchain::Height epoch_start = m_finalization_params->GetEpochStartHeight(epoch);
    const auto fin_state = m_fin_state_repo->Find(last_block);
    assert(fin_state->GetCurrentEpoch() == epoch);

    const auto votes = fin_state->GetCurrentDynastyVotes();
    const auto deposits = fin_state->GetCurrentDynastyDeposits();
    assert(deposits >= 0);
    // TODO UNIT-E: move fraction calculation to FinalizationState
    ufp64::ufp64_t fraction = deposits == 0 ? ufp64::to_ufp64(1) : ufp64::div_2uint(votes, deposits);

    for (const CBlockIndex *walk = &last_block; walk != nullptr && walk->nHeight >= epoch_start; walk = walk->pprev) {
      CAmount full_reward = m_blockchain_behavior->CalculateFinalizationReward(static_cast<blockchain::Height>(walk->nHeight));
      uint64_t reward = ufp64::to_uint(ufp64::mul_by_uint(fraction, full_reward));
      result.emplace_back(
          reward,
          GetRewardScript(*walk));
    }
    assert(result.size() == m_finalization_params->epoch_length);
    std::reverse(result.begin(), result.end());
    return result;
  }

  std::vector<CAmount> GetFinalizationRewardAmounts(const CBlockIndex &last_block) const override {
    if (last_block.nHeight < m_finalization_params->GetEpochCheckpointHeight(1)) {
      return {};
    }

    const auto prev_height = static_cast<blockchain::Height>(last_block.nHeight);
    if (!m_finalization_params->IsCheckpoint(prev_height)) {
      return {};
    }

    std::vector<CAmount> result;
    result.reserve(m_finalization_params->epoch_length);

    const auto epoch = m_finalization_params->GetEpoch(prev_height);
    const blockchain::Height epoch_start = m_finalization_params->GetEpochStartHeight(epoch);
    for (auto height = epoch_start; height <= prev_height; ++height) {
      result.push_back(m_blockchain_behavior->CalculateFinalizationReward(height));
    }

    return result;
  }

  std::size_t GetNumberOfRewardOutputs(const blockchain::Height current_height) const override {
    if (m_finalization_params->IsEpochStart(current_height) &&
        m_finalization_params->GetEpoch(current_height) > 1) {
      return m_finalization_params->epoch_length;
    }
    return 0;
  }
};

std::unique_ptr<FinalizationRewardLogic> FinalizationRewardLogic::New(
    const Dependency<blockchain::Behavior> behavior,
    const Dependency<finalization::Params> finalization_params,
    const Dependency<finalization::StateRepository> repo,
    const Dependency<BlockDB> block_db) {
  return MakeUnique<FinalizationRewardLogicImpl>(behavior, finalization_params, repo, block_db);
}

}  // namespace proposer
