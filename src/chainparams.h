// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef UNITE_CHAINPARAMS_H
#define UNITE_CHAINPARAMS_H

#include <blockchain/blockchain_behavior.h>
#include <blockchain/blockchain_parameters.h>
#include <chainparamsbase.h>
#include <consensus/params.h>
#include <dependency.h>
#include <primitives/block.h>
#include <protocol.h>
#include <esperanza/adminparams.h>
#include <esperanza/finalizationparams.h>
#include <snapshot/params.h>

#include <memory>
#include <vector>
#include <chain.h>
#include <amount.h>

struct SeedSpec6 {
    uint8_t addr[16];
    uint16_t port;
};

struct ChainTxData {
    int64_t nTime;
    int64_t nTxCount;
    double dTxRate;
};

/**
 * CChainParams defines various tweakable parameters of a given instance of the
 * Unit-e system. There are two: the public test network which gets reset from time to
 * time and a regression test mode which is intended for private networks only. It has
 * minimal difficulty to ensure that blocks can be found instantly.
 */
class CChainParams
{
public:
    const Consensus::Params& GetConsensus() const { return consensus; }
    const esperanza::FinalizationParams& GetFinalization() const { return finalization; }
    const esperanza::AdminParams& GetAdminParams() const { return adminParams; }
    const snapshot::Params& GetSnapshotParams() const { return snapshotParams; }
    const CMessageHeader::MessageStartChars& MessageStart() const { return parameters.message_start_characters; }
    int GetDefaultPort() const { return parameters.default_settings.p2p_port; }

    const CBlock& GenesisBlock() const { return genesis; }
    /** Default value for -checkmempool and -checkblockindex argument */
    bool DefaultConsistencyChecks() const { return fDefaultConsistencyChecks; }
    /** Policy: Filter transactions that do not match well-defined patterns */
    bool RequireStandard() const { return fRequireStandard; }
    /** Make miner stop after a block is found. In RPC, don't return until nGenProcLimit blocks are generated */
    bool MineBlocksOnDemand() const { return parameters.mine_blocks_on_demand; }
    /** Return the BIP70 network string (main, test or regtest) */
    const std::string NetworkIDString() const { return parameters.network_name; }
    /** Return the list of hostnames to look up for DNS seeds */
    const std::vector<std::string>& DNSSeeds() const { return vSeeds; }
    const std::vector<SeedSpec6>& FixedSeeds() const { return vFixedSeeds; }
    const ChainTxData& TxData() const { return chainTxData; }
    void UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout);
    void UpdateFinalizationParams(esperanza::FinalizationParams &params);

    const blockchain::Parameters parameters;

protected:
    explicit CChainParams(const blockchain::Parameters &parameters) : parameters(parameters) {}

    Consensus::Params consensus;
    esperanza::FinalizationParams finalization;
    esperanza::AdminParams adminParams;
    snapshot::Params snapshotParams;
    std::vector<std::string> vSeeds;
    CBlock genesis;
    std::vector<SeedSpec6> vFixedSeeds;
    bool fDefaultConsistencyChecks;
    bool fRequireStandard;
    ChainTxData chainTxData;
};

/**
 * Creates and returns a std::unique_ptr<CChainParams> of the chosen chain.
 * @returns a CChainParams* of the chosen chain.
 * @throws a std::runtime_error if the chain is not supported.
 */
std::unique_ptr<CChainParams> CreateChainParams(Dependency<blockchain::Behavior>, const std::string& chain);
std::unique_ptr<CChainParams> CreateChainParams(const std::string& chain);

/**
 * Return the currently selected parameters. This won't change after app
 * startup, except for unit tests.
 */
const CChainParams &Params();

/**
 * Sets the params returned by Params() to those for the given BIP70 chain name.
 * @throws std::runtime_error when the chain is not supported.
 */
void SelectParams(Dependency<blockchain::Behavior>, const std::string& chain);

/**
 * Allows modifying the Version Bits regtest parameters.
 */
void UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout);

/**
 * Allows modifying the esperanza regtest parameters.
 */
void UpdateFinalizationParams(esperanza::FinalizationParams &params);

#endif // UNITE_CHAINPARAMS_H
