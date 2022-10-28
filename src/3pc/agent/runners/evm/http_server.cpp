// Copyright (c) 2021 MIT Digital Currency Initiative,
//                    Federal Reserve Bank of Boston
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "http_server.hpp"

#include "3pc/agent/runners/evm/format.hpp"
#include "3pc/agent/runners/evm/impl.hpp"
#include "3pc/agent/runners/evm/math.hpp"
#include "3pc/agent/runners/evm/serialization.hpp"
#include "3pc/agent/runners/evm/util.hpp"
#include "impl.hpp"
#include "util/serialization/format.hpp"

#include <cassert>
#include <future>

using namespace cbdc::threepc::agent::runner;

namespace cbdc::threepc::agent::rpc {
    http_server::http_server(std::unique_ptr<server_type> srv,
                             std::shared_ptr<broker::interface> broker,
                             std::shared_ptr<logging::log> log,
                             const cbdc::threepc::config& cfg)
        : server_interface(std::move(broker), std::move(log), cfg),
          m_srv(std::move(srv)) {
        m_srv->register_handler_callback(
            [&](const std::string& method,
                const Json::Value& params,
                const server_type::result_callback_type& callback) {
                return request_handler(method, params, callback);
            });
    }

    http_server::~http_server() {
        m_log->trace("Agent server shutting down...");
        m_srv.reset();
        m_log->trace("Shut down agent server");
    }

    auto http_server::init() -> bool {
        return m_srv->init();
    }

    auto http_server::request_handler(
        const std::string& method,
        const Json::Value& params,
        const server_type::result_callback_type& callback) -> bool {
        m_log->trace("received request", method);

        if(method == "eth_sendRawTransaction") {
            return handle_send_raw_transaction(params, callback);
        } else if(method == "eth_sendTransaction") {
            return handle_send_transaction(params, callback);
        } else if(method == "eth_getTransactionCount") {
            return handle_get_transaction_count(params, callback);
        } else if(method == "eth_chainId" || method == "net_version") {
            return handle_chain_id(params, callback);
        } else if(method == "eth_call") {
            return handle_call(params, callback);
        } else if(method == "eth_estimateGas") {
            return handle_estimate_gas(params, callback);
        } else if(method == "eth_gasPrice") {
            return handle_gas_price(params, callback);
        } else if(method == "web3_clientVersion") {
            return handle_client_version(params, callback);
        } else if(method == "eth_getCode") {
            return handle_get_code(params, callback);
        } else if(method == "eth_getBalance") {
            return handle_get_balance(params, callback);
        } else if(method == "eth_accounts") {
            return handle_accounts(params, callback);
        } else if(method == "eth_getTransactionByHash") {
            return handle_get_transaction_by_hash(params, callback);
        } else if(method == "eth_getTransactionReceipt") {
            return handle_get_transaction_receipt(params, callback);
        } else if(method == "eth_getBlockByNumber") {
            return handle_get_block(params, callback, false);
        } else if(method == "eth_getBlockByHash") {
            return handle_get_block(params, callback, true);
        } else if(method == "eth_blockNumber") {
            return handle_block_number(params, callback);
        } else if(method == "eth_feeHistory") {
            return handle_fee_history(params, callback);
        } else if(method == "eth_getLogs" || method == "evm_increaseTime") {
            return handle_not_supported(params, callback);
        }
        m_log->warn("Unknown method", method);
        return handle_not_supported(params, callback);
    }

    auto http_server::handle_send_raw_transaction(
        Json::Value params,
        const server_type::result_callback_type& callback) -> bool {
        auto res_cb = std::function<void(interface::exec_return_type)>();

        if(!params.isArray() || params.empty() || !params[0].isString()) {
            m_log->warn("Invalid parameters to sendRawTransaction");
            return false;
        }

        auto params_str = params[0].asString();
        auto maybe_raw_tx = cbdc::buffer::from_hex(params_str.substr(2));
        if(!maybe_raw_tx.has_value()) {
            m_log->warn("Unable to decode params", params_str);
            return false;
        }

        auto maybe_tx = tx_decode(maybe_raw_tx.value(), m_log);
        if(!maybe_tx.has_value()) {
            m_log->warn("Unable to deserialize transaction");
            return false;
        }
        auto& tx = maybe_tx.value();
        auto runner_params = make_buffer(*tx);

        return exec_tx(callback,
                       runner::evm_runner_function::execute_transaction,
                       runner_params,
                       false,
                       [callback, tx](interface::exec_return_type) {
                           auto txid = cbdc::make_buffer(tx_id(*tx));
                           auto ret = Json::Value();
                           ret["result"] = txid.to_hex();
                           callback(ret);
                       });
    }

    auto http_server::handle_fee_history(
        Json::Value params,
        const server_type::result_callback_type& callback) -> bool {
        auto ret = Json::Value();

        if(!params.isArray() || params.size() < 3 || !params[0].isString()
           || !params[1].isString() || !params[2].isArray()) {
            m_log->warn("Invalid parameters to feeHistory");
            return false;
        }

        auto blocks_str = params[0].asString();
        int blocks = 0;
        auto end_block_str = params[1].asString();
        int end_block = 0;
        if(end_block_str == "latest") {
            long epoch_sec
                = std::chrono::duration_cast<std::chrono::seconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();
            end_block = static_cast<int>(epoch_sec);
        } else {
            end_block = std::stoi(end_block_str);
        }
        blocks = std::stoi(blocks_str);
        ret["oldestBlock"] = end_block - blocks;
        ret["reward"] = Json::Value(Json::arrayValue);
        ret["baseFeePerGas"] = Json::Value(Json::arrayValue);
        ret["gasUsedRatio"] = Json::Value(Json::arrayValue);
        for(int i = 0; i < blocks; i++) {
            auto rwd = Json::Value(Json::arrayValue);
            for(Json::ArrayIndex j = 0; j < params[2].size(); j++) {
                rwd.append("0x0");
            }
            ret["reward"].append(rwd);
            ret["baseFeePerGas"].append("0x0");
            ret["gasUsedRatio"].append(0.0);
        }
        ret["baseFeePerGas"].append("0x0");
        callback(ret);
        return true;
    }

    auto http_server::handle_get_transaction_count(
        Json::Value params,
        const server_type::result_callback_type& callback) -> bool {
        if(!params.isArray() || params.empty() || !params[0].isString()) {
            m_log->warn("Invalid parameters to getTransactionCount");
            return false;
        }
        auto params_str = params[0].asString();
        auto maybe_runner_params
            = cbdc::buffer::from_hex(params_str.substr(2));
        if(!maybe_runner_params.has_value()) {
            m_log->warn("Unable to decode params", params_str);
            return false;
        }
        auto runner_params = std::move(maybe_runner_params.value());
        return exec_tx(
            callback,
            runner::evm_runner_function::read_account,
            runner_params,
            true,
            [callback, runner_params](interface::exec_return_type res) {
                auto& updates = std::get<return_type>(res);
                auto it = updates.find(runner_params);
                assert(it != updates.end());

                auto maybe_acc
                    = cbdc::from_buffer<runner::evm_account>(it->second);
                assert(maybe_acc.has_value());

                auto& acc = maybe_acc.value();

                auto tx_count = acc.m_nonce + evmc::uint256be(1);
                auto ret = Json::Value();
                ret["result"] = to_hex(tx_count);
                callback(ret);
            });
    }

    auto http_server::handle_get_balance(
        Json::Value params,
        const server_type::result_callback_type& callback) -> bool {
        if(!params.isArray() || params.empty() || !params[0].isString()) {
            m_log->warn("Invalid parameters to getBalance");
            return false;
        }
        auto params_str = params[0].asString();
        auto maybe_runner_params
            = cbdc::buffer::from_hex(params_str.substr(2));
        if(!maybe_runner_params.has_value()) {
            m_log->warn("Unable to decode params", params_str);
            return false;
        }
        auto runner_params = std::move(maybe_runner_params.value());
        return exec_tx(
            callback,
            runner::evm_runner_function::read_account,
            runner_params,
            true,
            [callback, runner_params](interface::exec_return_type res) {
                auto& updates = std::get<return_type>(res);
                auto it = updates.find(runner_params);
                assert(it != updates.end());

                auto maybe_acc
                    = cbdc::from_buffer<runner::evm_account>(it->second);
                assert(maybe_acc.has_value());

                auto& acc = maybe_acc.value();
                auto ret = Json::Value();
                ret["result"] = to_hex(acc.m_balance);
                callback(ret);
            });
    }

    auto http_server::handle_get_transaction_by_hash(
        Json::Value params,
        const server_type::result_callback_type& callback) -> bool {
        if(!params.isArray() || params.empty() || !params[0].isString()) {
            m_log->warn("Invalid parameters to getTransactionByHash");
            return false;
        }
        auto params_str = params[0].asString();
        auto maybe_runner_params
            = cbdc::buffer::from_hex(params_str.substr(2));
        if(!maybe_runner_params.has_value()) {
            m_log->warn("Unable to decode params", params_str);
            return false;
        }
        auto runner_params = std::move(maybe_runner_params.value());
        return exec_tx(
            callback,
            runner::evm_runner_function::get_transaction,
            runner_params,
            true,
            [callback, runner_params, this](interface::exec_return_type res) {
                auto& updates = std::get<return_type>(res);
                auto it = updates.find(runner_params);
                assert(it != updates.end());

                auto maybe_tx = cbdc::from_buffer<runner::evm_tx>(it->second);
                assert(maybe_tx.has_value());

                auto& tx = maybe_tx.value();
                auto ret = Json::Value();
                ret["result"] = tx_to_json(tx, m_secp);
                callback(ret);
            });
    }

    auto http_server::handle_get_transaction_receipt(
        Json::Value params,
        const server_type::result_callback_type& callback) -> bool {
        if(!params.isArray() || params.empty() || !params[0].isString()) {
            m_log->warn("Invalid parameters to getTransactionReceipt");
            return false;
        }
        auto params_str = params[0].asString();
        auto maybe_runner_params
            = cbdc::buffer::from_hex(params_str.substr(2));
        if(!maybe_runner_params.has_value()) {
            m_log->warn("Unable to decode params", params_str);
            return false;
        }
        auto runner_params = std::move(maybe_runner_params.value());
        return exec_tx(
            callback,
            runner::evm_runner_function::get_transaction_receipt,
            runner_params,
            true,
            [callback, runner_params, this](interface::exec_return_type res) {
                auto& updates = std::get<return_type>(res);
                auto it = updates.find(runner_params);
                assert(it != updates.end());

                auto maybe_rcpt
                    = cbdc::from_buffer<runner::evm_tx_receipt>(it->second);
                assert(maybe_rcpt.has_value());

                auto& rcpt = maybe_rcpt.value();
                auto ret = Json::Value();
                ret["result"] = tx_receipt_to_json(rcpt, m_secp);
                callback(ret);
            });
    }

    auto http_server::handle_get_code(
        Json::Value params,
        const server_type::result_callback_type& callback) -> bool {
        if(!params.isArray() || params.empty() || !params[0].isString()) {
            m_log->warn("Invalid parameters to getBalance");
            return false;
        }
        auto params_str = params[0].asString();
        auto maybe_runner_params
            = cbdc::buffer::from_hex(params_str.substr(2));
        if(!maybe_runner_params.has_value()) {
            m_log->warn("Unable to decode params", params_str);
            return false;
        }
        auto runner_params = std::move(maybe_runner_params.value());
        return exec_tx(
            callback,
            runner::evm_runner_function::read_account_code,
            runner_params,
            true,
            [callback, runner_params](interface::exec_return_type res) {
                auto& updates = std::get<return_type>(res);
                auto it = updates.find(runner_params);
                assert(it != updates.end());
                auto ret = Json::Value();
                ret["result"] = it->second.to_hex();
                callback(ret);
            });
    }

    auto http_server::handle_chain_id(
        Json::Value /*params*/,
        const server_type::result_callback_type& callback) -> bool {
        auto ret = Json::Value();
        ret["result"] = to_hex(evmc::uint256be(opencbdc_chain_id));
        callback(ret);
        return true;
    }

    auto http_server::handle_block_number(
        Json::Value /*params*/,
        const server_type::result_callback_type& callback) -> bool {
        auto ret = Json::Value();
        long epoch_sec
            = std::chrono::duration_cast<std::chrono::seconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count();
        ret["result"] = to_hex(evmc::uint256be(epoch_sec));
        callback(ret);
        return true;
    }

    auto http_server::handle_get_block(
        Json::Value params,
        const server_type::result_callback_type& callback,
        bool by_hash) -> bool {
        auto ret = Json::Value();

        if(by_hash) {
            ret["number"] = 1;
            ret["hash"] = params[0];
        } else {
            ret["number"] = params[0];
            ret["hash"]
                = "0x00000000000000000000000000000000000000000000000000000"
                  "00000000000";
        }
        ret["parentHash"] = "0x00000000000000000000000000000000000000000000000"
                            "00000000000000000";
        ret["timestamp"] = ret["number"];
        ret["gasLimit"] = "0xffffffff";
        ret["gasUsed"] = "0x0";
        ret["baseFeePerGas"] = "0x0";
        ret["miner"] = "0x0000000000000000000000000000000000000000";
        ret["transactions"] = Json::Value(Json::arrayValue);
        ret["nonce"] = "0x00000000";
        auto buf = cbdc::buffer();
        buf.extend(256);
        ret["logsBloom"] = buf.to_hex_prefixed();
        callback(ret);
        return true;
    }

    auto http_server::handle_accounts(
        Json::Value /*params*/,
        const server_type::result_callback_type& callback) -> bool {
        auto ret = Json::Value();
        ret["result"] = Json::Value(Json::arrayValue);
        callback(ret);
        return true;
    }

    auto http_server::handle_not_supported(
        Json::Value /*params*/,
        const server_type::result_callback_type& callback) -> bool {
        auto ret = Json::Value();
        ret["error"] = Json::Value();
        ret["error"]["code"] = -32601;
        ret["error"]["message"] = "Method not supported";
        callback(ret);
        return true;
    }

    auto http_server::handle_estimate_gas(
        Json::Value /*params*/,
        const server_type::result_callback_type& callback) -> bool {
        auto ret = Json::Value();
        // TODO: actually estimate gas
        ret["result"] = "0xffffffffff";
        callback(ret);
        return true;
    }

    auto http_server::handle_client_version(
        Json::Value /*params*/,
        const server_type::result_callback_type& callback) -> bool {
        auto ret = Json::Value();
        ret["result"] = "opencbdc/v0.0";
        callback(ret);
        return true;
    }

    auto http_server::handle_gas_price(
        Json::Value /*params*/,
        const server_type::result_callback_type& callback) -> bool {
        auto ret = Json::Value();
        ret["result"] = "0x0";
        callback(ret);
        return true;
    }

    auto
    http_server::handle_call(Json::Value params,
                             const server_type::result_callback_type& callback)
        -> bool {
        if(!params.isArray() || params.empty() || !params[0].isObject()) {
            m_log->warn("Parameter to call is invalid");
            return false;
        }

        auto maybe_tx = dryrun_tx_from_json(params[0]);
        if(!maybe_tx) {
            m_log->warn("Parameter is not a valid transaction");
            return false;
        }

        auto& tx = maybe_tx.value();
        auto runner_params = make_buffer(*tx);

        return exec_tx(callback,
                       runner::evm_runner_function::dryrun_transaction,
                       runner_params,
                       true,
                       [callback, tx](interface::exec_return_type res) {
                           auto ret = Json::Value();
                           auto txid = cbdc::make_buffer(tx_id(tx->m_tx));

                           auto& updates = std::get<return_type>(res);
                           auto it = updates.find(txid);
                           if(it == updates.end()) {
                               ret["error"] = -32001;
                               callback(ret);
                               return;
                           }

                           auto maybe_receipt
                               = cbdc::from_buffer<evm_tx_receipt>(it->second);
                           if(!maybe_receipt) {
                               ret["error"] = -32002;
                               callback(ret);
                               return;
                           }

                           auto buf = cbdc::buffer();
                           buf.extend(maybe_receipt->m_output_data.size());
                           std::memcpy(buf.data(),
                                       maybe_receipt->m_output_data.data(),
                                       maybe_receipt->m_output_data.size());
                           ret["result"] = "0x" + buf.to_hex();
                           callback(ret);
                       });
    }

    auto http_server::handle_send_transaction(
        Json::Value params,
        const server_type::result_callback_type& callback) -> bool {
        if(!params.isArray() || params.empty() || !params[0].isObject()) {
            m_log->warn("Invalid parameters to sendTransaction");
            return false;
        }

        auto maybe_tx = tx_from_json(params[0]);
        if(!maybe_tx) {
            m_log->warn("Parameter is not a valid transaction");
            return false;
        }

        auto& tx = maybe_tx.value();
        auto runner_params = make_buffer(*tx);
        return exec_tx(callback,
                       runner::evm_runner_function::execute_transaction,
                       runner_params,
                       false,
                       [callback, tx](interface::exec_return_type) {
                           auto txid = cbdc::make_buffer(tx_id(*tx));
                           auto ret = Json::Value();
                           ret["result"] = txid.to_hex();
                           callback(ret);
                       });
    }

    auto http_server::exec_tx(
        const server_type::result_callback_type& callback,
        runner::evm_runner_function f_type,
        cbdc::buffer& runner_params,
        bool dry_run,
        std::function<void(interface::exec_return_type)> res_cb) -> bool {
        auto function = cbdc::buffer();
        function.append(&f_type, sizeof(f_type));
        auto cb = [res_cb, callback](interface::exec_return_type res) {
            if(!std::holds_alternative<return_type>(res)) {
                auto ec = std::get<interface::error_code>(res);
                auto ret = Json::Value();
                ret["error"] = static_cast<int>(ec);
                callback(ret);
                return;
            }

            res_cb(res);
        };

        auto id = m_next_id++;
        auto a = [&]() {
            auto agent = std::make_shared<impl>(
                m_log,
                m_cfg,
                &runner::factory<runner::evm_runner>::create,
                m_broker,
                function,
                runner_params,
                [this, id, res_cb](interface::exec_return_type res) {
                    auto success = std::holds_alternative<return_type>(res);
                    if(!success) {
                        auto ec = std::get<interface::error_code>(res);
                        if(ec == interface::error_code::retry) {
                            m_retry_queue.push(id);
                            return;
                        }
                    }
                    res_cb(res);
                    m_cleanup_queue.push(id);
                },
                runner::evm_runner::initial_lock_type,
                dry_run,
                m_secp,
                m_threads);
            {
                std::unique_lock l(m_agents_mut);
                m_agents.emplace(id, agent);
            }
            return agent;
        }();
        return a->exec();
    }
}
