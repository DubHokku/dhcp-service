#pragma once

#include "Application.hpp"
#include "Loader.hpp"
#include "SwitchManager.hpp"
#include "api/SwitchFwd.hpp"
#include "oxm/openflow_basic.hh"

#include <iostream>
#include <sstream>
#include <boost/bimap.hpp>
#include <unordered_map>

#include <tins/tins.h>
#include <arpa/inet.h>

#define NIC "eth0"
#define TIME_LEASE 1712
#define TIME_RENEWAL 856
#define TIME_REBIND 1208

namespace runos
{
    using SwitchPtr = safe::shared_ptr<Switch>;
    namespace of13 = fluid_msg::of13;
    namespace ofb
    {
        constexpr auto in_port = oxm::in_port();
        // constexpr auto eth_src = oxm::eth_src();
    }
    
    class dhcp_service : public Application
    {
        Q_OBJECT
        SIMPLE_APPLICATION( dhcp_service, "dhcp-service" )
        
        public:
        void init( Loader* loader, const Config& config ) override;
        
        protected slots:
        void onSwitchUp( SwitchPtr sw );
        
        private:
        OFMessageHandlerPtr handler_;
        SwitchManager* switch_manager_;
        
        uint64_t dpid_;
        uint32_t in_port_;
        // ethaddr src_mac_;
        Tins::HWAddress<6> src_mac;
        
        struct pool_addr_t
        {
            uint32_t subnet;
            uint32_t subnet_mask;
            uint32_t router;
            uint32_t broadcast;
            uint32_t time_servers[2];
            uint32_t name_servers[3];
            uint32_t dynamic_hosts;
        };
        pool_addr_t dhcp_pool;
        
        void pool();
        void service( Tins::DHCP* );
        void of_send( Tins::EthernetII* );
        void mk_reply( Tins::DHCP*, Tins::DHCP* );
        uint32_t mk_addr( uint32_t );
        uint32_t get_address( uint32_t, Tins::HWAddress<6>);
        bool check_address( uint32_t );
        
        boost::bimap< std::string, uint32_t > addr_base;
        std::unordered_map< uint32_t, uint32_t > lease_base;
    };
}
