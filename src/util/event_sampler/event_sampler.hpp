// Copyright (c) 2021 MIT Digital Currency Initiative,
//                    Federal Reserve Bank of Boston
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OPENCBDC_TX_SRC_COMMON_EVENT_SAMPLER_H_
#define OPENCBDC_TX_SRC_COMMON_EVENT_SAMPLER_H_

#include <chrono>
#include <fstream>
#include <list>
#include <string>
#include <thread>

namespace cbdc {
    enum class sampled_event_type : uint8_t {
        unknown = 0,
        server_handler_tx_notify = 1,
        tx_notify = 2,
        send_complete_txs = 3,
        state_machine_tx_notify = 4,
        insert_complete = 5,
        discarded_expired = 6,
        discarded_spent = 7,
        make_block = 8
    };

    struct sampled_event {
        sampled_event_type type;
        long timestamp;
        long latency;
        size_t count;
    };
    /// Keeps a collection of samples in memory to write to disk
    /// periodically in a separate thread - not to interfere with the
    /// code paths being measured
    class event_sampler {
      public:
        event_sampler(const event_sampler&) = delete;
        auto operator=(const event_sampler&) -> event_sampler& = delete;

        event_sampler(event_sampler&&) = delete;
        auto operator=(event_sampler&&) -> event_sampler& = delete;

        explicit event_sampler(const std::string& output_name);
        ~event_sampler();

        /// Adds the given sampled event to the collection
        /// \param event_type The event that happened.
        /// \param start_time The time the event started.
        void append(
            sampled_event_type event_type,
            const std::chrono::time_point<std::chrono::high_resolution_clock>&
                start_time);

        /// Adds the given sampled event to the collection
        /// \param event_type The event that happened.
        /// \param start_time The time the event started.
        /// \param count The count of data involved. For instance when
        /// processing multiple transactions, this would be the number of
        /// transactions
        void append(
            sampled_event_type event_type,
            const std::chrono::time_point<std::chrono::high_resolution_clock>&
                start_time,
            size_t count);

      private:
        void flush_thread();
        void flush();
        std::list<sampled_event> m_data;
        bool m_running;
        std::thread m_flush_thread;
        std::list<sampled_event>::iterator m_data_head;
        std::list<sampled_event>::iterator m_data_tail;
        std::ofstream m_output_stream;
    };
}

#endif // OPENCBDC_TX_SRC_COMMON_EVENT_SAMPLER_H_
