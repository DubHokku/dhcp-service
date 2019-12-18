#pragma once

#include "Application.hpp"
#include "Loader.hpp"
#include "SwitchManager.hpp"
#include "api/SwitchFwd.hpp"
#include "oxm/openflow_basic.hh"

#include <iostream>
#include <algorithm>
#include <unordered_map>
#include <sstream>

#include <tins/tins.h>
#include <arpa/inet.h>

#define NIC "enp3s0"
// #define NIC "wlo1"

#define TIME_LEASE 1712
#define TIME_RENEWAL 856
#define TIME_REBIND 1208

namespace runos
{
    using SwitchPtr = safe::shared_ptr<Switch>; 
    namespace of13 = fluid_msg::of13;
    
    class dhcp_service : public Application
    {
        Q_OBJECT
        SIMPLE_APPLICATION( dhcp_service, "dhcp-service" )

        public:
        
        void init( Loader* loader, const Config& config ) override;
    
        struct pool_addr_t
        {
            uint32_t subnet;
            uint32_t subnet_mask;
            uint32_t router;
            uint32_t broadcast;
            uint32_t time_servers[2];
            uint32_t name_servers[3];
        };
    
        pool_addr_t dhcp_pool;
    
        int run();
        
        protected slots:
        void onSwitchUp( SwitchPtr sw );
    
        private:
        // OFMessageHandlerPtr handler_;
        SwitchManager* switch_manager_;
        // uint64_t dpid_;
        
        uint32_t mk_addr();
        bool check_address( uint32_t );
        uint32_t get_address( uint32_t, Tins::HWAddress<6>);
    
        std::unordered_map< uint32_t, uint32_t > lease_base;
        std::unordered_map< std::string, uint32_t > addr_base;
    };
}
