/*
 Itay Marom
 Cisco Systems, Inc.
*/

/*
Copyright (c) 2015-2015 Cisco Systems, Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include <trex_stateless.h>
#include <trex_stateless_port.h>
#include <trex_stateless_messaging.h>
#include <trex_streams_compiler.h>

#include <string>

#ifndef TREX_RPC_MOCK_SERVER

// DPDK c++ issue 
#ifndef UINT8_MAX
    #define UINT8_MAX 255
#endif

#ifndef UINT16_MAX
    #define UINT16_MAX 0xFFFF
#endif

// DPDK c++ issue 
#endif

#include <rte_ethdev.h>
#include <os_time.h>

void
port_id_to_cores(uint8_t port_id, std::vector<std::pair<uint8_t, uint8_t>> &cores_id_list);

using namespace std;

/***************************
 * trex stateless port
 * 
 **************************/
TrexStatelessPort::TrexStatelessPort(uint8_t port_id) : m_port_id(port_id) {
    m_port_state = PORT_STATE_UP_IDLE;
    clear_owner();
}


/**
 * starts the traffic on the port
 * 
 */
TrexStatelessPort::rc_e
TrexStatelessPort::start_traffic(double mul) {

    if (m_port_state != PORT_STATE_UP_IDLE) {
        return (RC_ERR_BAD_STATE_FOR_OP);
    }

    if (get_stream_table()->size() == 0) {
        return (RC_ERR_NO_STREAMS);
    }

    /* fetch all the streams from the table */
    vector<TrexStream *> streams;
    get_stream_table()->get_object_list(streams);

    /* compiler it */
    TrexStreamsCompiler compiler;
    TrexStreamsCompiledObj *compiled_obj = new TrexStreamsCompiledObj(m_port_id, mul);

    bool rc = compiler.compile(streams, *compiled_obj);
    if (!rc) {
        return (RC_ERR_FAILED_TO_COMPILE_STREAMS);
    }

    /* generate a message to all the relevant DP cores to start transmitting */
    TrexStatelessCpToDpMsgBase *start_msg = new TrexStatelessDpStart(compiled_obj);

    send_message_to_dp(start_msg);

    /* move the state to transmiting */
    m_port_state = PORT_STATE_TRANSMITTING;

    return (RC_OK);
}

TrexStatelessPort::rc_e
TrexStatelessPort::stop_traffic(void) {

    /* real code goes here */
    if (m_port_state != PORT_STATE_TRANSMITTING) {
        return (RC_ERR_BAD_STATE_FOR_OP);
    }

    /* generate a message to all the relevant DP cores to start transmitting */
    TrexStatelessCpToDpMsgBase *stop_msg = new TrexStatelessDpStop(m_port_id);

    send_message_to_dp(stop_msg);

    m_port_state = PORT_STATE_UP_IDLE;

    return (RC_OK);
}

/**
* access the stream table
* 
*/
TrexStreamTable * TrexStatelessPort::get_stream_table() {
    return &m_stream_table;
}


std::string 
TrexStatelessPort::get_state_as_string() {

    switch (get_state()) {
    case PORT_STATE_DOWN:
        return "down";

    case PORT_STATE_UP_IDLE:
        return  "idle";

    case PORT_STATE_TRANSMITTING:
        return "transmitting";
    }

    return "unknown";
}

void
TrexStatelessPort::get_properties(string &driver, string &speed) {

    /* take this from DPDK */
    driver = "e1000";
    speed  = "1 Gbps";
}


/**
 * generate a random connection handler
 * 
 */
std::string 
TrexStatelessPort::generate_handler() {
    std::stringstream ss;

    static const char alphanum[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";

    /* generate 8 bytes of random handler */
    for (int i = 0; i < 8; ++i) {
        ss << alphanum[rand() % (sizeof(alphanum) - 1)];
    }

    return (ss.str());
}


void
TrexStatelessPort::encode_stats(Json::Value &port) {

    const TrexPlatformApi *api = get_stateless_obj()->get_platform_api();

    TrexPlatformInterfaceStats stats;
    api->get_interface_stats(m_port_id, stats);

    port["tx_bps"]          = stats.m_stats.m_tx_bps;
    port["rx_bps"]          = stats.m_stats.m_rx_bps;

    port["tx_pps"]          = stats.m_stats.m_tx_pps;
    port["rx_pps"]          = stats.m_stats.m_rx_pps;

    port["total_tx_pkts"]   = Json::Value::UInt64(stats.m_stats.m_total_tx_pkts);
    port["total_rx_pkts"]   = Json::Value::UInt64(stats.m_stats.m_total_rx_pkts);

    port["total_tx_bytes"]  = Json::Value::UInt64(stats.m_stats.m_total_tx_bytes);
    port["total_rx_bytes"]  = Json::Value::UInt64(stats.m_stats.m_total_rx_bytes);
    
    port["tx_rx_errors"]    = Json::Value::UInt64(stats.m_stats.m_tx_rx_errors);
}

void 
TrexStatelessPort::send_message_to_dp(TrexStatelessCpToDpMsgBase *msg) {

    std::vector<std::pair<uint8_t, uint8_t>> cores_id_list;

    get_stateless_obj()->get_platform_api()->port_id_to_cores(m_port_id, cores_id_list);

    for (auto core_pair : cores_id_list) {

        /* send the message to the core */
        CNodeRing *ring = CMsgIns::Ins()->getCpDp()->getRingCpToDp(core_pair.first);
        ring->Enqueue((CGenNode *)msg->clone());
    }

}
