// Copyright (c) 2018-2019 The Unit-e developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <snapshot/snapshot_validation.h>

#include <snapshot/messages.h>
#include <test/test_unite.h>
#include <validation.h>
#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(snapshot_validation_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(validate_candidate_block_tx) {
  SetDataDir("snapshot_state");
  fs::remove_all(GetDataDir() / snapshot::SNAPSHOT_FOLDER);

  {
    // all non-coinbase tx are not checked
    CMutableTransaction mtx;
    mtx.vin.emplace_back(CTxIn());
    mtx.vin.emplace_back(CTxIn());

    CTransaction tx(mtx);
    CBlockIndex idx;

    CCoinsViewCache view(pcoinsdbview.get());
    BOOST_CHECK(snapshot::ValidateCandidateBlockTx(tx, &idx, view));
  }

  {
    // missing snapshot hash
    CMutableTransaction mtx;
    mtx.SetType(TxType::COINBASE);
    CBlockIndex idx;
    idx.nHeight = 100;
    CTxIn in;
    in.scriptSig << idx.nHeight << OP_0;
    mtx.vin.emplace_back(in);
    CTransaction tx(mtx);

    CCoinsViewCache view(pcoinsdbview.get());
    BOOST_CHECK(!snapshot::ValidateCandidateBlockTx(tx, &idx, view));
  }

  {
    // snapshot hash is incorrect
    CBlockIndex block;
    block.nHeight = 100;
    CBlockIndex prevBlock;
    prevBlock.stake_modifier = uint256S("aa");
    prevBlock.nChainWork = arith_uint256("bb");
    block.pprev = &prevBlock;

    snapshot::SnapshotHash hash1;
    hash1.AddUTXO(snapshot::UTXO());

    CCoinsMap map;
    BOOST_CHECK(pcoinsdbview->BatchWrite(map, uint256S("aa"), hash1));

    CMutableTransaction mtx;
    mtx.SetType(TxType::COINBASE);
    CTxIn in;

    uint256 h = snapshot::SnapshotHash().GetHash(prevBlock.stake_modifier,
                                                 ArithToUint256(prevBlock.nChainWork));
    std::vector<uint8_t> hash(h.begin(), h.end());
    in.scriptSig << block.nHeight << hash << OP_0;
    mtx.vin.emplace_back(in);
    CTransaction tx(mtx);

    CCoinsViewCache view(pcoinsdbview.get());
    BOOST_CHECK(!snapshot::ValidateCandidateBlockTx(tx, &block, view));
  }

  {
    // snapshot hash is correct
    CBlockIndex block;
    block.nHeight = 100;
    CBlockIndex prevBlock;
    prevBlock.stake_modifier = uint256S("aa");
    block.pprev = &prevBlock;

    snapshot::SnapshotHash snapHash;
    snapHash.AddUTXO(snapshot::UTXO());
    CCoinsMap map;
    BOOST_CHECK(pcoinsdbview->BatchWrite(map, uint256S("aa"), snapHash));
    uint256 hash = snapHash.GetHash(prevBlock.stake_modifier,
                                    ArithToUint256(prevBlock.nChainWork));

    CMutableTransaction mtx;
    mtx.SetType(TxType::COINBASE);
    CTxIn in;
    std::vector<uint8_t> data(hash.begin(), hash.end());
    in.scriptSig << block.nHeight << data << OP_0;
    mtx.vin.emplace_back(in);
    CTransaction tx(mtx);

    CCoinsViewCache view(pcoinsdbview.get());
    BOOST_CHECK(snapshot::ValidateCandidateBlockTx(tx, &block, view));
  }
}

BOOST_AUTO_TEST_SUITE_END()
