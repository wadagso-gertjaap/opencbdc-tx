// Copyright (c) 2021 MIT Digital Currency Initiative,
//                    Federal Reserve Bank of Boston
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "event_sampler.hpp"
#include "util/serialization/ostream_serializer.hpp"
#include "util/serialization/format.hpp"
#include <vector>

namespace cbdc {
    event_sampler::event_sampler(const std::string& output_name)
        : m_running(true),
          m_flush_thread(std::thread(&event_sampler::flush_thread, this)),
          m_output_stream("event_sampler_" + output_name + ".bin", std::ios_base::binary) {
        // Push dummy agg to have valid iterators
        auto e = sampled_event();
        m_data.push_back(e);
        m_data_head = m_data.begin();
        m_data_tail = m_data.end();
    }
    event_sampler::~event_sampler() {
        m_running = false;
        if(m_flush_thread.joinable()) {
            m_flush_thread.join();
        }
        flush();
    }
    void event_sampler::append(
        sampled_event_type event_type,
        const std::chrono::time_point<std::chrono::high_resolution_clock>&
            start_time) {
        append(event_type, start_time, 1);
    }
    void event_sampler::append(
        sampled_event_type event_type,
        const std::chrono::time_point<std::chrono::high_resolution_clock>&
            start_time,
        size_t count) {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            end_time - start_time)
                            .count();
        m_data.push_back({event_type,
                          end_time.time_since_epoch().count(),
                          duration,
                          count});
        m_data_tail = m_data.end();
        m_data.erase(m_data.begin(), m_data_head);
    }
    void event_sampler::flush_thread() {
        while(m_running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            flush();
        }
    }
    void event_sampler::flush() {
        auto next = m_data_head;
        if(++next == m_data_tail) {
            return;
        }
        auto first = next;
        auto last = next;
        size_t len = 1;
        while(++next != m_data_tail) {
            last = next;
            len++;
        }
        for(auto it = first; it != last; it++) {
            auto el = *it;
            m_output_stream.write(reinterpret_cast<const char *>(&(el.type)), sizeof(uint8_t));
            m_output_stream.write(reinterpret_cast<const char *>(&(el.timestamp)), sizeof(long));
            m_output_stream.write(reinterpret_cast<const char *>(&(el.latency)), sizeof(long));
            m_output_stream.write(reinterpret_cast<const char *>(&(el.count)), sizeof(size_t));
        }
        m_output_stream.flush();
        m_data_head = last;
    }
}
