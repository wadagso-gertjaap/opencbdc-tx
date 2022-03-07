// Copyright (c) 2021 MIT Digital Currency Initiative,
//                    Federal Reserve Bank of Boston
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "atomizer.hpp"

#include "uhs/transaction/messages.hpp"
#include "util/common/config.hpp"
#include "util/serialization/buffer_serializer.hpp"
#include "util/serialization/format.hpp"

namespace cbdc::atomizer {
    static constexpr auto initial_spent_cache_size = 500000;

    auto atomizer::make_block() -> uint64_t {
        const auto start = std::chrono::high_resolution_clock::now();
        auto blk = std::make_shared<block>();

        blk->m_transactions.swap(m_complete_txs);

        m_best_height++;

        for(size_t i = m_spent_cache_depth; i > 0; i--) {
            m_spent[i] = std::move(m_spent[i - 1]);
        }

        m_spent[0].clear();
        m_spent[0].max_load_factor(std::numeric_limits<float>::max());
        m_spent[0].rehash(initial_spent_cache_size);

        blk->m_height = m_best_height;
        m_blocks.insert(std::make_pair(m_best_height, blk));
        m_event_sampler.append(sampled_event_type::make_block,
                               start,
                               blk->m_transactions.size());
        return m_best_height;
    }

    auto atomizer::insert_complete(uint64_t oldest_attestation,
                                   transaction::compact_tx&& tx)
        -> std::optional<cbdc::watchtower::tx_error> {
        const auto start = std::chrono::high_resolution_clock::now();
        const auto height_offset = get_notification_offset(oldest_attestation);

        auto offset_err = check_notification_offset(height_offset, tx);
        if(offset_err) {
            m_event_sampler.append(sampled_event_type::discarded_expired,
                                   start);
            return offset_err;
        }

        auto cache_check_range = height_offset;

        auto err_set = check_stxo_cache(tx, cache_check_range);
        if(err_set) {
            m_event_sampler.append(sampled_event_type::discarded_spent, start);
            return err_set;
        }

        add_tx_to_stxo_cache(tx);

        m_complete_txs.push_back(std::move(tx));
        m_event_sampler.append(sampled_event_type::insert_complete, start);
        return std::nullopt;
    }

    auto atomizer::get_block(uint64_t height)
        -> std::optional<std::shared_ptr<cbdc::atomizer::block>> {
        auto it = m_blocks.find(height);
        if(it == m_blocks.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    void atomizer::prune(uint64_t height) {
        for(auto it = m_blocks.begin(); it != m_blocks.end();) {
            if(it->second->m_height < height) {
                it = m_blocks.erase(it);
            } else {
                it++;
            }
        }
    }

    auto atomizer::pending_transactions() const -> size_t {
        return m_complete_txs.size();
    }

    auto atomizer::height() const -> uint64_t {
        return m_best_height;
    }

    atomizer::atomizer(const uint64_t best_height,
                       const size_t stxo_cache_depth)
        : m_best_height(best_height),
          m_spent_cache_depth(stxo_cache_depth),
          m_event_sampler("atomizer") {
        m_spent.resize(stxo_cache_depth + 1);
        for(size_t i = 0; i < m_spent_cache_depth; i++) {
            m_spent[i].max_load_factor(std::numeric_limits<float>::max());
            m_spent[i].rehash(initial_spent_cache_size);
        }
    }

    auto atomizer::serialize() -> cbdc::buffer {
        auto buf = cbdc::buffer();
        auto ser = cbdc::buffer_serializer(buf);

        ser << static_cast<uint64_t>(m_spent_cache_depth) << m_best_height
            << m_complete_txs << m_spent;

        return buf;
    }

    void atomizer::deserialize(cbdc::serializer& buf) {
        m_complete_txs.clear();

        m_spent.clear();

        buf >> m_spent_cache_depth >> m_best_height >> m_complete_txs
            >> m_spent;
    }

    auto atomizer::operator==(const atomizer& other) const -> bool {
        return m_complete_txs == other.m_complete_txs
            && m_spent == other.m_spent && m_best_height == other.m_best_height
            && m_spent_cache_depth == other.m_spent_cache_depth;
    }

    auto atomizer::get_notification_offset(uint64_t block_height) const
        -> uint64_t {
        // Calculate the offset from the current block height when the shard
        // attested to this transaction.
        return m_best_height - block_height;
    }

    auto atomizer::check_notification_offset(uint64_t height_offset,
                                             const transaction::compact_tx& tx)
        const -> std::optional<cbdc::watchtower::tx_error> {
        // Check whether this TX notification is recent enough that we can
        // safely process it by checking our spent UTXO caches.
        if(height_offset > m_spent_cache_depth && !tx.m_inputs.empty()) {
            return cbdc::watchtower::tx_error{
                tx.m_id,
                cbdc::watchtower::tx_error_stxo_range{}};
        }
        return std::nullopt;
    }

    auto atomizer::check_stxo_cache(const transaction::compact_tx& tx,
                                    uint64_t cache_check_range) const
        -> std::optional<cbdc::watchtower::tx_error> {
        // For each height offset in our STXO cache up to the offset of the
        // oldest attestation we're using, check that the inputs have not
        // already been spent.
        auto err_set = std::unordered_set<hash_t, hashing::null>{};
        for(size_t offset = 0; offset <= cache_check_range; offset++) {
            for(const auto& inp : tx.m_inputs) {
                if(m_spent[offset].find(inp) != m_spent[offset].end()) {
                    err_set.insert(inp);
                }
            }
        }

        if(!err_set.empty()) {
            return cbdc::watchtower::tx_error{
                tx.m_id,
                cbdc::watchtower::tx_error_inputs_spent{std::move(err_set)}};
        }

        return std::nullopt;
    }

    void atomizer::add_tx_to_stxo_cache(const transaction::compact_tx& tx) {
        // None of the inputs have previously been spent during block heights
        // we used attestations from, so spend all the TX inputs in the current
        // block height (offset 0).
        m_spent[0].insert(tx.m_inputs.begin(), tx.m_inputs.end());
    }
}
