#!/usr/bin/env python3
# Copyright (c) 2019 The Unit-e developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from decimal import Decimal

from test_framework.test_framework import UnitETestFramework
from test_framework.util import (
    assert_equal,
    connect_nodes,
    disconnect_nodes,
    wait_until,
    sync_blocks,
    assert_finalizationstate,
)


CB_DEFAULT_OUTPUTS = 2
FULL_FINALIZATION_REWARD = Decimal(15)
EPOCH_LENGTH = 5


class FinalizationRewardsTest(UnitETestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True

        self.extra_args = [
            ['-stakesplitthreshold=0'],
            ['-stakesplitthreshold=0', '-validating=1'],
            ['-stakesplitthreshold=0', '-validating=1'],
        ]
        self.customchainparams = [{}]

    def setup_network(self):
        self.setup_nodes()

    def run_test(self):
        def get_coinbase_of_last_block(node):
            block_hash = node.getbestblockhash()
            block = node.getblock(block_hash)
            return node.getrawtransaction(block['tx'][0], True, block_hash)

        def check_finalization_rewards(block, reward_size, reward_addresses):
            for out in epoch_first_cb['vout'][1:EPOCH_LENGTH + 1]:
                assert_equal(out['value'], reward_size)

            addresses = sum((x['scriptPubKey']['addresses']
                             for x in epoch_first_cb['vout'][1:EPOCH_LENGTH + 1]), [])
            assert_equal(addresses, reward_addresses)

        node = self.nodes[0]
        finalizer1, finalizer2 = self.nodes[1:]

        self.setup_stake_coins(*self.nodes)

        assert_finalizationstate(node, {'currentDynasty': 0,
                                        'currentEpoch': 0,
                                        'lastJustifiedEpoch': 0,
                                        'lastFinalizedEpoch': 0})

        proposer_address1 = node.getnewaddress('', 'bech32')
        proposer_address2 = node.getnewaddress('', 'bech32')

        node.proposetoaddress(1, proposer_address1)
        assert_equal(node.getblockcount(), 1)
        epoch_first_cb = get_coinbase_of_last_block(node)
        # The first block of the first epoch does not have finalization rewards
        assert_equal(len(epoch_first_cb['vout']), CB_DEFAULT_OUTPUTS)

        connect_nodes(node, finalizer1.index)
        connect_nodes(node, finalizer2.index)
        sync_blocks([node, finalizer1, finalizer2])

        payto = finalizer1.getnewaddress('', 'legacy')
        txid = finalizer1.deposit(payto, 3000)
        wait_until(lambda: txid in node.getrawmempool())
        payto = finalizer2.getnewaddress('', 'legacy')
        txid = finalizer2.deposit(payto, 6000)
        wait_until(lambda: txid in node.getrawmempool())

        disconnect_nodes(node, finalizer1.index)
        disconnect_nodes(node, finalizer2.index)

        assert_equal(node.getblockcount(), 1)

        for _ in range(3):
            reward_addresses = [proposer_address1]
            for _ in range(EPOCH_LENGTH - 3):
                node.proposetoaddress(1, proposer_address1)
                reward_addresses.append(proposer_address1)
                assert_equal(len(get_coinbase_of_last_block(node)['vout']), CB_DEFAULT_OUTPUTS)

            for _ in range(2):
                node.proposetoaddress(1, proposer_address2)
                reward_addresses.append(proposer_address2)
                assert_equal(len(get_coinbase_of_last_block(node)['vout']), CB_DEFAULT_OUTPUTS)

            node.proposetoaddress(1, proposer_address1)

            # The first block of each epoch must have finalization rewards for each block of the previous epoch
            epoch_first_cb = get_coinbase_of_last_block(node)
            assert_equal(len(epoch_first_cb['vout']), CB_DEFAULT_OUTPUTS + EPOCH_LENGTH)
            check_finalization_rewards(epoch_first_cb, FULL_FINALIZATION_REWARD, reward_addresses)

        assert_equal(node.getblockcount(), EPOCH_LENGTH * 3 + 1)
        print(node.getfinalizationstate())

        # No votes included
        node.proposetoaddress(EPOCH_LENGTH, proposer_address2)
        reward_addresses = [proposer_address1] + [proposer_address2] * (EPOCH_LENGTH - 1)
        epoch_first_cb = get_coinbase_of_last_block(node)
        assert_equal(len(epoch_first_cb['vout']), CB_DEFAULT_OUTPUTS + EPOCH_LENGTH)
        check_finalization_rewards(epoch_first_cb, FULL_FINALIZATION_REWARD, reward_addresses)
        print(node.getfinalizationstate())

        # Vote of the firs validator included
        self.wait_for_vote_and_disconnect(finalizer=finalizer1, node=node)
        node.proposetoaddress(EPOCH_LENGTH, proposer_address1)
        reward_addresses = [proposer_address2] + [proposer_address1] * (EPOCH_LENGTH - 1)
        epoch_first_cb = get_coinbase_of_last_block(node)
        assert_equal(len(epoch_first_cb['vout']), CB_DEFAULT_OUTPUTS + EPOCH_LENGTH)
        check_finalization_rewards(epoch_first_cb, FULL_FINALIZATION_REWARD, reward_addresses)
        print(node.getfinalizationstate())

        # Vote of the second validator included
        self.wait_for_vote_and_disconnect(finalizer=finalizer2, node=node)
        node.proposetoaddress(EPOCH_LENGTH, proposer_address2)
        reward_addresses = [proposer_address1] + [proposer_address2] * (EPOCH_LENGTH - 1)
        epoch_first_cb = get_coinbase_of_last_block(node)
        assert_equal(len(epoch_first_cb['vout']), CB_DEFAULT_OUTPUTS + EPOCH_LENGTH)
        check_finalization_rewards(epoch_first_cb, FULL_FINALIZATION_REWARD, reward_addresses)
        print(node.getfinalizationstate())

        # All the votes are included
        self.wait_for_vote_and_disconnect(finalizer=finalizer1, node=node)
        self.wait_for_vote_and_disconnect(finalizer=finalizer2, node=node)
        node.proposetoaddress(EPOCH_LENGTH, proposer_address1)
        reward_addresses = [proposer_address2] + [proposer_address1] * (EPOCH_LENGTH - 1)
        epoch_first_cb = get_coinbase_of_last_block(node)
        assert_equal(len(epoch_first_cb['vout']), CB_DEFAULT_OUTPUTS + EPOCH_LENGTH)
        check_finalization_rewards(epoch_first_cb, FULL_FINALIZATION_REWARD, reward_addresses)
        print(node.getfinalizationstate())

        # Check that rewards are not spendable until the corresponding epoch is finalized


if __name__ == '__main__':
    FinalizationRewardsTest().main()
