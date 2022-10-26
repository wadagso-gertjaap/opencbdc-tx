// Copyright (c) 2021 MIT Digital Currency Initiative,
//                    Federal Reserve Bank of Boston
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CBDC_UNIVERSE0_SRC_3PC_AGENT_FORMAT_H_
#define CBDC_UNIVERSE0_SRC_3PC_AGENT_FORMAT_H_

#include "messages.hpp"
#include "util/serialization/serializer.hpp"

namespace cbdc {
    auto operator<<(serializer& ser, const threepc::agent::rpc::request& req)
        -> serializer&;
    auto operator>>(serializer& deser, threepc::agent::rpc::request& req)
        -> serializer&;
}

#endif
