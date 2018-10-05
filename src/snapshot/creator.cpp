// Copyright (c) 2018 The unit-e core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <snapshot/creator.h>

#include <clientversion.h>
#include <fs.h>
#include <serialize.h>
#include <snapshot/indexer.h>
#include <snapshot/p2p_processing.h>
#include <util.h>
#include <validation.h>
#include <boost/thread.hpp>

namespace snapshot {

std::unique_ptr<Creator> creator = nullptr;

void Creator::Init(CCoinsViewDB *view, CScheduler &scheduler) {
  if (creator == nullptr) {
    creator = MakeUnique<Creator>(view);
    scheduler.scheduleEvery(std::bind(&Creator::Generate, creator.get()),
                            60 * 60 * 1000);  // every hour
  }
}

Creator::Creator(CCoinsViewDB *view) : m_view(view) {}

void Creator::Generate() {
  if (IsInitialSnapshotDownload()) {
    return;
  }
  Create();
}

CreationInfo Creator::Create() {
  LogPrint(BCLog::SNAPSHOT, "start creating snapshot\n");

  CreationInfo info;

  uint32_t snapshotId = 0;
  if (!m_view->ReserveSnapshotId(snapshotId)) {
    info.m_status = Status::RESERVE_SNAPSHOT_ID_ERROR;
    return info;
  }
  LogPrint(BCLog::SNAPSHOT, "reserve id=%i for the new snapshot\n", snapshotId);

  std::unique_ptr<CCoinsViewCursor> cursor(m_view->Cursor());

  Indexer indexer(snapshotId, /* m_snapshotHash */ uint256(),
                  cursor->GetBestBlock(), m_step, m_stepsPerFile);

  std::map<uint32_t, CTxOut> outputs;
  Coin prevCoin;
  uint256 prevTxHash;

  while (cursor->Valid()) {
    boost::this_thread::interruption_point();

    COutPoint key;
    Coin coin;
    if (cursor->GetKey(key) && cursor->GetValue(coin)) {
      if (!outputs.empty() && key.hash != prevTxHash) {
        info.m_totalOutputs += outputs.size();

        UTXOSet utxoSet{prevTxHash, prevCoin.nHeight, prevCoin.IsCoinBase(),
                        std::move(outputs)};

        outputs.clear();

        if (!indexer.WriteUTXOSet(utxoSet)) {
          info.m_status = Status::WRITE_ERROR;
          return info;
        }

        if (indexer.GetMeta().m_totalUTXOSets == m_maxUTXOSets) {
          break;
        }
      }

      outputs[key.n] = coin.out;
    }
    prevCoin = coin;
    prevTxHash = key.hash;

    cursor->Next();
  }

  if (!outputs.empty()) {
    info.m_totalOutputs += outputs.size();

    UTXOSet utxoSet(prevTxHash, prevCoin.nHeight, prevCoin.IsCoinBase(),
                    std::move(outputs));
    outputs.clear();

    if (!indexer.WriteUTXOSet(utxoSet)) {
      info.m_status = Status::WRITE_ERROR;
      return info;
    }
  }

  if (!indexer.Flush()) {
    info.m_status = Status::WRITE_ERROR;
    return info;
  }

  if (indexer.GetMeta().m_snapshotHash.IsNull()) {
    info.m_status = Status::CALC_SNAPSHOT_HASH_ERROR;
    return info;
  }
  info.m_indexerMeta = indexer.GetMeta();

  if (!m_view->SetSnapshotId(snapshotId)) {
    info.m_status = Status::SET_SNAPSHOT_ID_ERROR;
    return info;
  }
  LogPrint(BCLog::SNAPSHOT, "snapshot id=%i is created\n", snapshotId);

  std::vector<uint32_t> ids = m_view->GetSnapshotIds();
  uint64_t keepSnapshots = 5;
  if (ids.size() > keepSnapshots) {
    std::vector<uint32_t> newIds;
    newIds.reserve(keepSnapshots);

    for (uint64_t i = 0; i < ids.size() - keepSnapshots; ++i) {
      if (Indexer::Delete(ids.at(i))) {
        LogPrint(BCLog::SNAPSHOT, "snapshot id=%i is deleted\n", ids.at(i));
      } else {
        // collect IDs that can't be deleted,
        // will be re-tried during next snapshot creation
        newIds.emplace_back(ids.at(i));
      }
    }
    newIds.insert(newIds.end(), ids.end() - keepSnapshots, ids.end());

    if (!m_view->SetSnapshotIds(newIds)) {
      info.m_status = Status::SET_ALL_SNAPSHOTS_ERROR;
      return info;
    }
  }

  return info;
}

}  // namespace snapshot