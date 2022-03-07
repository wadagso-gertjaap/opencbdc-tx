// Copyright (c) 2021 MIT Digital Currency Initiative,
//                    Federal Reserve Bank of Boston
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "log_store.hpp"

#include <array>
#include <cstring>
#include <leveldb/write_batch.h>
#include <libnuraft/buffer_serializer.hxx>
#include <iostream>

namespace cbdc::raft {
    static auto constexpr log_limit = 10000;
    log_store::log_store() {
        auto buf = nuraft::buffer::alloc(sizeof(uint64_t));
        m_db[0] = nuraft::cs_new<nuraft::log_entry>(0, buf);
    }

    auto log_store::load(const std::string& db_dir) -> bool {
        if(db_dir.length() > 0) {}
        {
            std::lock_guard<std::mutex> l(m_db_mut);
            m_next_idx = 1;
            m_start_idx = 1;
        }

        return true;
    }

    auto log_store::next_slot() const -> uint64_t {
        std::lock_guard<std::mutex> l(m_db_mut);
        return m_next_idx;
    }

    auto log_store::start_index() const -> uint64_t {
        std::lock_guard<std::mutex> l(m_db_mut);
        return m_start_idx;
    }

    auto log_store::last_entry() const -> nuraft::ptr<nuraft::log_entry> {
        std::lock_guard<std::mutex> l(m_db_mut);
        auto entry = m_db.find(m_next_idx - 1);
        if(entry == m_db.end()) {
            entry = m_db.find(0);
        }
        return make_clone(entry->second);
    }

    auto log_store::make_clone(const nuraft::ptr<nuraft::log_entry>& entry)
        -> nuraft::ptr<nuraft::log_entry> {
        nuraft::ptr<nuraft::log_entry> clone
            = nuraft::cs_new<nuraft::log_entry>(
                entry->get_term(),
                nuraft::buffer::clone(entry->get_buf()),
                entry->get_val_type());
        return clone;
    }

    auto log_store::append(nuraft::ptr<nuraft::log_entry>& entry) -> uint64_t {
        auto clone = make_clone(entry);
        {
            std::lock_guard<std::mutex> l(m_db_mut);
            m_db[m_next_idx] = clone;
            m_next_idx++;
            if(m_next_idx > log_limit) {
                compact(m_next_idx-1-log_limit);
            }
            return m_next_idx - 1;
        }
    }

    void log_store::write_at(uint64_t index,
                             nuraft::ptr<nuraft::log_entry>& entry) {
        auto clone = make_clone(entry);
        {
            std::lock_guard<std::mutex> l(m_db_mut);
            auto itr = m_db.lower_bound(index);
            while(itr != m_db.end()) {
                itr = m_db.erase(itr);
            }
            m_db[index] = clone;
            m_next_idx = index + 1;
            if(m_next_idx > log_limit) {
                compact(m_next_idx-1-log_limit);
            }
        }
    }

    auto log_store::log_entries(uint64_t start, uint64_t end)
        -> log_entries_t {
        auto ret = nuraft::cs_new<log_entries_t::element_type>(end - start);
        {
            std::lock_guard<std::mutex> l(m_db_mut);
            for(size_t i = start; i < end; i++) {
                auto entry = m_db.find(i);
                if(entry == m_db.end()) {
                    entry = m_db.find(0);
                }
                (*ret)[i-start] = make_clone(entry->second);
            }
        }
        return ret;
    }

    auto log_store::entry_at(uint64_t index)
        -> nuraft::ptr<nuraft::log_entry> {
        nuraft::ptr<nuraft::log_entry> src = nullptr;
        {
            std::lock_guard<std::mutex> l(m_db_mut);
            auto entry = m_db.find(index);
            if(entry == m_db.end()) {
                entry = m_db.find(0);
            }
            src = entry->second;
        }
        return make_clone(src);
    }

    auto log_store::term_at(uint64_t index) -> uint64_t {
        const auto entry = entry_at(index);
        return entry->get_term();
    }

    auto log_store::pack(uint64_t index, int32_t cnt)
        -> nuraft::ptr<nuraft::buffer> {
        assert(cnt >= 0);
        const auto entries
            = log_entries(index, index + static_cast<uint64_t>(cnt));

        std::vector<nuraft::ptr<nuraft::buffer>> bufs(
            static_cast<size_t>(cnt));

        size_t i{0};
        size_t total_len{0};
        for(const auto& entry : *entries) {
            auto buf = entry->serialize();
            total_len += buf->size();
            bufs[i] = std::move(buf);
            i++;
        }

        auto ret = nuraft::buffer::alloc(
            sizeof(uint64_t) + static_cast<size_t>(cnt) * sizeof(uint64_t)
            + total_len);
        nuraft::buffer_serializer bs(ret);

        bs.put_u64(static_cast<uint64_t>(cnt));

        for(const auto& buf : bufs) {
            bs.put_u64(buf->size());
            bs.put_raw(buf->data_begin(), buf->size());
        }

        return ret;
    }

    void log_store::apply_pack(uint64_t index, nuraft::buffer& pack) {
        nuraft::buffer_serializer bs(pack);

        const auto cnt = bs.get_u64();

        std::vector<nuraft::ptr<nuraft::log_entry>> entries(cnt);

        for(size_t i{0}; i < cnt; i++) {
            const auto len = bs.get_u64();
            auto buf = nuraft::buffer::alloc(len);
            bs.get_buffer(buf);
            auto entry = nuraft::log_entry::deserialize(*buf);
            assert(entry);
            entries[i] = std::move(entry);
        }
        {
            for(auto& entry : entries) {
                write_at(index, entry);
                index++;
            }
            m_start_idx = 1;
            m_next_idx = index;
        }
    }

    auto log_store::compact(uint64_t last_log_index) -> bool {
        for(ulong ii = m_start_idx; ii <= last_log_index; ++ii) {
            auto entry = m_db.find(ii);
            if(entry != m_db.end()) {
                m_db.erase(entry);
            }
        }
        return true;
    }

    auto log_store::flush() -> bool {
        return true;
    }
}
