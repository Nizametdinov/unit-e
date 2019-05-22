// Copyright (c) 2018-2019 The Unit-e developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <proposer/finalization_reward_logic.h>

#include <blockdb.h>
#include <finalization/state_repository.h>
#include <staking/validation_result.h>

#include <test/test_unite.h>
#include <test/test_unite_mocks.h>
#include <boost/test/unit_test.hpp>

#include <functional>

namespace {

class BlockDBMock : public ::BlockDB {
 public:
  std::vector<CBlock> blocks;

  boost::optional<CBlock> ReadBlock(const CBlockIndex &index) override {
    if (index.nHeight < blocks.size()) {
      return blocks[index.nHeight];
    }
    return boost::none;
  }
};

struct Fixture {
  finalization::Params fin_params;
  blockchain::Parameters parameters = [this]() {
    auto p = blockchain::Parameters::TestNet();
    uint32_t period_blocks = fin_params.epoch_length - 1;  // To have different rewards within each epoch
    p.reward_function = [period_blocks](const blockchain::Parameters &p, blockchain::Height h) -> CAmount {
      int halvings = h / period_blocks;
      if (halvings >= 64) {
        return 0;
      }
      return p.reward >> halvings;
    };
    return p;
  }();
  std::unique_ptr<blockchain::Behavior> behavior = blockchain::Behavior::NewFromParameters(parameters);

  mocks::StateRepositoryMock state_repository{fin_params};
  BlockDBMock block_db;
  std::vector<CBlock> blocks;
  std::vector<CBlockIndex> block_indices;

  CTransactionRef MakeCoinbaseTx(blockchain::Height height, const WitnessV0KeyHash &dest) {
    CMutableTransaction tx;
    auto value = behavior->CalculateBlockReward(height);
    auto script = GetScriptForDestination(dest);
    tx.vout.emplace_back(value, script);
    return MakeTransactionRef(tx);
  }

  const CBlockIndex &BlockIndexAtHeight(blockchain::Height h) { return block_indices.at(h); }

  const CBlock &BlockAtHeight(blockchain::Height h) { return blocks.at(h); }

  void BuildChain(blockchain::Height max_height) {
    blocks.resize(max_height + 1);
    block_indices.resize(max_height + 1);
    for (blockchain::Height h = 1; h <= max_height; ++h) {
      blocks[h].hashPrevBlock = blocks[h - 1].GetHash();
      std::vector<unsigned char> dest(20, static_cast<unsigned char>(h));
      blocks[h].vtx.push_back(MakeCoinbaseTx(h, WitnessV0KeyHash(dest)));
      blocks[h].ComputeMerkleTrees();

      block_indices[h].nHeight = static_cast<int>(h);
      block_indices[h].pprev = &block_indices[h - 1];
    }
    block_db.blocks = blocks;
  }

  std::unique_ptr<proposer::FinalizationRewardLogic> GetFinalizationRewardLogic() {
    return proposer::FinalizationRewardLogic::New(behavior.get(), &fin_params, &state_repository, &block_db);
  }
};

}  // namespace

BOOST_FIXTURE_TEST_SUITE(finalization_reward_logic_tests, ReducedTestingSetup)

BOOST_AUTO_TEST_CASE(get_finalization_rewards) {
  Fixture f;
  auto logic = f.GetFinalizationRewardLogic();
  FinalizationStateSpy &fin_state = f.state_repository.state;

  f.BuildChain(f.fin_params.GetEpochCheckpointHeight(6) + 1);

  uint160 finalizer_1 = RandValidatorAddr();
  uint160 finalizer_2 = RandValidatorAddr();
  CAmount deposit_size_1 = fin_state.MinDepositSize();
  CAmount deposit_size_2 = fin_state.MinDepositSize() * 2;
  fin_state.ProcessDeposit(finalizer_1, deposit_size_1);
  fin_state.ProcessDeposit(finalizer_2, deposit_size_2);

  std::vector<CTxOut> rewards = logic->GetFinalizationRewards(f.BlockIndexAtHeight(0));
  std::vector<CAmount> reward_amounts = logic->GetFinalizationRewardAmounts(f.BlockIndexAtHeight(0));
  BOOST_CHECK_EQUAL(rewards.size(), 0);
  BOOST_CHECK_EQUAL(reward_amounts.size(), 0);

  for (uint32_t epoch = 1; epoch <= 3; ++epoch) {
    fin_state.InitializeEpoch(f.fin_params.GetEpochStartHeight(epoch));
    BOOST_REQUIRE_EQUAL(fin_state.GetCurrentEpoch(), epoch);

    for (auto height = f.fin_params.GetEpochStartHeight(epoch); height < f.fin_params.GetEpochCheckpointHeight(epoch);
         ++height) {
      rewards = logic->GetFinalizationRewards(f.BlockIndexAtHeight(height));
      reward_amounts = logic->GetFinalizationRewardAmounts(f.BlockIndexAtHeight(height));
      BOOST_CHECK_EQUAL(rewards.size(), 0);
      BOOST_CHECK_EQUAL(reward_amounts.size(), 0);
    }

    auto checkpoint_height = fin_state.GetEpochCheckpointHeight(epoch);

    // We must pay out the rewards in the first block of an epoch, i.e. when the current tip is a checkpoint block
    rewards = logic->GetFinalizationRewards(f.BlockIndexAtHeight(checkpoint_height));
    reward_amounts = logic->GetFinalizationRewardAmounts(f.BlockIndexAtHeight(checkpoint_height));
    BOOST_CHECK_EQUAL(rewards.size(), f.fin_params.epoch_length);
    BOOST_CHECK_EQUAL(reward_amounts.size(), f.fin_params.epoch_length);
    for (std::size_t i = 0; i < rewards.size(); ++i) {
      auto h = static_cast<blockchain::Height>(f.fin_params.GetEpochStartHeight(epoch) + i);
      auto r = static_cast<CAmount>(f.parameters.reward_function(f.parameters, h) * 0.4);
      BOOST_CHECK_EQUAL(rewards[i].nValue, r);
      BOOST_CHECK_EQUAL(reward_amounts[i], r);
      auto s = f.BlockAtHeight(h).vtx[0]->vout[0].scriptPubKey;
      BOOST_CHECK_EQUAL(HexStr(rewards[i].scriptPubKey), HexStr(s));
    }
  }

  {
    BOOST_CHECK_EQUAL(fin_state.Checkpoints()[3].m_is_justified, false);
    BOOST_CHECK_EQUAL(fin_state.Checkpoints()[3].m_is_finalized, false);

//  BOOST_CHECK_EQUAL(fin_state.ValidateVote(vote), +Result::SUCCESS);

    uint32_t epoch = 4;
    fin_state.InitializeEpoch(f.fin_params.GetEpochStartHeight(epoch));
    BOOST_REQUIRE_EQUAL(fin_state.GetCurrentEpoch(), epoch);

    Vote vote{finalizer_2, GetRandHash(), 2, 3};
    fin_state.ProcessVote(vote);

    BOOST_CHECK_EQUAL(fin_state.Checkpoints()[3].m_is_justified, true);
    BOOST_CHECK_EQUAL(fin_state.Checkpoints()[3].m_is_finalized, true);

    for (auto height = f.fin_params.GetEpochStartHeight(epoch); height < f.fin_params.GetEpochCheckpointHeight(epoch);
         ++height) {
      rewards = logic->GetFinalizationRewards(f.BlockIndexAtHeight(height));
      reward_amounts = logic->GetFinalizationRewardAmounts(f.BlockIndexAtHeight(height));
      BOOST_CHECK_EQUAL(rewards.size(), 0);
      BOOST_CHECK_EQUAL(reward_amounts.size(), 0);
    }

    auto checkpoint_height = fin_state.GetEpochCheckpointHeight(epoch);

    // We must pay out the rewards in the first block of an epoch, i.e. when the current tip is a checkpoint block
    rewards = logic->GetFinalizationRewards(f.BlockIndexAtHeight(checkpoint_height));
    reward_amounts = logic->GetFinalizationRewardAmounts(f.BlockIndexAtHeight(checkpoint_height));
    BOOST_CHECK_EQUAL(rewards.size(), f.fin_params.epoch_length);
    BOOST_CHECK_EQUAL(reward_amounts.size(), f.fin_params.epoch_length);
    for (std::size_t i = 0; i < rewards.size(); ++i) {
      auto h = static_cast<blockchain::Height>(f.fin_params.GetEpochStartHeight(epoch) + i);
      auto r = ufp64::to_uint(ufp64::div_2uint(f.behavior->CalculateFinalizationReward(h) * 2, 3));
      BOOST_CHECK_EQUAL(rewards[i].nValue, r);
      BOOST_CHECK_EQUAL(reward_amounts[i], r);
      auto s = f.BlockAtHeight(h).vtx[0]->vout[0].scriptPubKey;
      BOOST_CHECK_EQUAL(HexStr(rewards[i].scriptPubKey), HexStr(s));
    }
  }

  {
    uint32_t epoch = 5;
    fin_state.InitializeEpoch(f.fin_params.GetEpochStartHeight(epoch));
    BOOST_REQUIRE_EQUAL(fin_state.GetCurrentEpoch(), epoch);

    Vote vote{finalizer_1, GetRandHash(), 3, 4};
    fin_state.ProcessVote(vote);

    BOOST_CHECK_EQUAL(fin_state.Checkpoints()[4].m_is_justified, false);
    BOOST_CHECK_EQUAL(fin_state.Checkpoints()[4].m_is_finalized, false);

    for (auto height = f.fin_params.GetEpochStartHeight(epoch); height < f.fin_params.GetEpochCheckpointHeight(epoch);
         ++height) {
      rewards = logic->GetFinalizationRewards(f.BlockIndexAtHeight(height));
      reward_amounts = logic->GetFinalizationRewardAmounts(f.BlockIndexAtHeight(height));
      BOOST_CHECK_EQUAL(rewards.size(), 0);
      BOOST_CHECK_EQUAL(reward_amounts.size(), 0);
    }

    auto checkpoint_height = fin_state.GetEpochCheckpointHeight(epoch);

    // We must pay out the rewards in the first block of an epoch, i.e. when the current tip is a checkpoint block
    rewards = logic->GetFinalizationRewards(f.BlockIndexAtHeight(checkpoint_height));
    reward_amounts = logic->GetFinalizationRewardAmounts(f.BlockIndexAtHeight(checkpoint_height));
    BOOST_CHECK_EQUAL(rewards.size(), f.fin_params.epoch_length);
    BOOST_CHECK_EQUAL(reward_amounts.size(), f.fin_params.epoch_length);
    for (std::size_t i = 0; i < rewards.size(); ++i) {
      auto h = static_cast<blockchain::Height>(f.fin_params.GetEpochStartHeight(epoch) + i);
      auto r = ufp64::to_uint(f.behavior->CalculateFinalizationReward(h) * ufp64::div_2uint(1, 3));
      BOOST_CHECK_EQUAL(rewards[i].nValue, r);
      BOOST_CHECK_EQUAL(reward_amounts[i], r);
      auto s = f.BlockAtHeight(h).vtx[0]->vout[0].scriptPubKey;
      BOOST_CHECK_EQUAL(HexStr(rewards[i].scriptPubKey), HexStr(s));
    }
  }

  {
    uint32_t epoch = 6;
    fin_state.InitializeEpoch(f.fin_params.GetEpochStartHeight(epoch));
    BOOST_REQUIRE_EQUAL(fin_state.GetCurrentEpoch(), epoch);

    Vote vote1{finalizer_1, GetRandHash(), 3, 5};
    Vote vote2{finalizer_2, GetRandHash(), 3, 5};
    fin_state.ProcessVote(vote1);
    fin_state.ProcessVote(vote2);

    BOOST_CHECK_EQUAL(fin_state.Checkpoints()[5].m_is_justified, true);
    BOOST_CHECK_EQUAL(fin_state.Checkpoints()[5].m_is_finalized, false);

    for (auto height = f.fin_params.GetEpochStartHeight(epoch); height < f.fin_params.GetEpochCheckpointHeight(epoch);
         ++height) {
      rewards = logic->GetFinalizationRewards(f.BlockIndexAtHeight(height));
      reward_amounts = logic->GetFinalizationRewardAmounts(f.BlockIndexAtHeight(height));
      BOOST_CHECK_EQUAL(rewards.size(), 0);
      BOOST_CHECK_EQUAL(reward_amounts.size(), 0);
    }

    auto checkpoint_height = fin_state.GetEpochCheckpointHeight(epoch);

    // We must pay out the rewards in the first block of an epoch, i.e. when the current tip is a checkpoint block
    rewards = logic->GetFinalizationRewards(f.BlockIndexAtHeight(checkpoint_height));
    reward_amounts = logic->GetFinalizationRewardAmounts(f.BlockIndexAtHeight(checkpoint_height));
    BOOST_CHECK_EQUAL(rewards.size(), f.fin_params.epoch_length);
    BOOST_CHECK_EQUAL(reward_amounts.size(), f.fin_params.epoch_length);
    for (std::size_t i = 0; i < rewards.size(); ++i) {
      auto h = static_cast<blockchain::Height>(f.fin_params.GetEpochStartHeight(epoch) + i);
      auto r = f.parameters.reward_function(f.parameters, h) * 4 / 10;
      BOOST_CHECK_EQUAL(rewards[i].nValue, r);
      BOOST_CHECK_EQUAL(reward_amounts[i], r);
      auto s = f.BlockAtHeight(h).vtx[0]->vout[0].scriptPubKey;
      BOOST_CHECK_EQUAL(HexStr(rewards[i].scriptPubKey), HexStr(s));
    }
  }
}

BOOST_AUTO_TEST_SUITE_END()
