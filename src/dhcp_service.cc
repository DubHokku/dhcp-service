#include "dhcp_service.hpp"
#include "PacketParser.hpp"
#include "api/Packet.hpp"
#include <runos/core/logging.hpp>

namespace runos
{

REGISTER_APPLICATION( dhcp_service, 
{
    "controller",
    "switch-manager",
    // "l2-learning-switch",
    // "topology",
    "" })

void dhcp_service::init( Loader* loader, const Config& config )
{
    switch_manager_ = SwitchManager::get( loader );
    connect( switch_manager_, &SwitchManager::switchUp, this, &dhcp_service::onSwitchUp );
    
    // auto data_base = std::make_shared<runos::HostsDatabase>();
    // data_base llc;
    
    pool();
    handler_ = Controller::get( loader )->register_handler
    (
        [=]( of13::PacketIn& pi, OFConnectionPtr ofconn ) mutable->bool
        {
            PacketParser pp( pi );
            runos::Packet& pkt( pp );
            
            src_mac_ = pkt.load( oxm::eth_src());
            // src_mac_ = pkt.load( ofb::eth_src );
            
            dst_mac_ = pkt.load( oxm::eth_dst());
            // dst_mac_ = pkt.load( ofb::eth_dst );
            
            in_port_ = pkt.load( oxm::in_port());
            // in_port_ = pkt.load( ofb::in_port );
            
            dpid_ = ofconn->dpid();
            
            // auto target_port = data_base->getPort( dpid_, dst_mac_ );
            // auto target_port = runos::HostsDatabase::getPort( dpid_, dst_mac_ );
            
            Tins::DHCP dhcp;
            Tins::EthernetII frame(( const uint8_t* )pi.data(), pi.data_len());
            try
            {
                Tins::UDP& udp = frame.rfind_pdu<Tins::UDP>();
                if( udp.dport() == 67 )
                    dhcp = udp.find_pdu<Tins::RawPDU>()->to<Tins::DHCP>();
                else
                    return false;
            }
            catch( std::exception &ex )
            {
                return false;
            }
            
            src_mac = frame.src_addr();
            service( &dhcp );
            
            return true;
        }, -5
    );
}

void dhcp_service::pool()
{
    inet_aton( "172.17.0.0", ( struct in_addr* )&dhcp_pool.subnet );
    inet_aton( "255.255.0.0", ( struct in_addr* )&dhcp_pool.subnet_mask );
    inet_aton( "172.17.255.255", ( struct in_addr* )&dhcp_pool.broadcast );
    
    inet_aton( "172.17.0.1", ( struct in_addr* )&dhcp_pool.router );
    inet_aton( "172.17.1.2", ( struct in_addr* )&dhcp_pool.name_servers[0]);
    inet_aton( "172.17.1.4", ( struct in_addr* )&dhcp_pool.name_servers[1]);
    inet_aton( "172.17.1.7", ( struct in_addr* )&dhcp_pool.name_servers[2]);
    inet_aton( "172.17.1.2", ( struct in_addr* )&dhcp_pool.time_servers[0]);
    inet_aton( "172.17.1.4", ( struct in_addr* )&dhcp_pool.time_servers[1]);
    
    dhcp_pool.dynamic_hosts = 0; // lowest assigned host address
    dhcp_pool.dynamic_hosts = htonl( dhcp_pool.dynamic_hosts );
}

void dhcp_service::service( Tins::DHCP *dhcp )
{
    Tins::NetworkInterface nic( NIC );
    Tins::NetworkInterface::Info info = nic.addresses();
    
    const auto dh_type = dhcp->search_option( Tins::DHCP::DHCP_MESSAGE_TYPE );
    if( dh_type )
    {
        if( *dh_type->data_ptr() == Tins::DHCP::Flags::DISCOVER )
        {
            std::cout << "DHCP::DISCOVER \n";
           
            Tins::DHCP *offer = new Tins::DHCP;
            offer->type( Tins::DHCP::Flags::OFFER );
            mk_reply( dhcp, offer );
            
            Tins::EthernetII opkt = Tins::EthernetII( src_mac, info.hw_addr ) / 
                Tins::IP( offer->yiaddr(), info.ip_addr ) / Tins::UDP( 68, 67 ) / *offer;
                
            of_send( &opkt );
        }
        if( *dh_type->data_ptr() == Tins::DHCP::Flags::REQUEST )
        {
            std::cout << "DHCP::REQUEST \n";

            Tins::DHCP *ack = new Tins::DHCP;
            ack->type( Tins::DHCP::Flags::ACK );
            mk_reply( dhcp, ack );
         
            Tins::EthernetII opkt = Tins::EthernetII( src_mac, info.hw_addr ) / 
                Tins::IP( ack->yiaddr(), info.ip_addr ) / Tins::UDP( 68, 67 ) / *ack;
                
            of_send( &opkt );
        }
        if( *dh_type->data_ptr() == Tins::DHCP::Flags::OFFER )
        {
            std::cout << "DHCP::OFFER \n";
        }
        if( *dh_type->data_ptr() == Tins::DHCP::Flags::ACK )
        {
            std::cout << "DHCP::ACK \n";
        }
        if( *dh_type->data_ptr() == Tins::DHCP::Flags::NAK )
        {
            std::cout << "DHCP::NAK \n";
        }
        if( *dh_type->data_ptr() == Tins::DHCP::Flags::DECLINE )
        {
            std::cout << "DHCP::DECLINE \n";
        }
        if( *dh_type->data_ptr() == Tins::DHCP::Flags::RELEASE )
        {
            std::cout << "DHCP::RELEASE \n";
        }
        if( *dh_type->data_ptr() == Tins::DHCP::Flags::INFORM )
        {
            std::cout << "DHCP::INFORM \n";
        }
    }
}

void dhcp_service::mk_reply( Tins::DHCP *request, Tins::DHCP *response )
{
    Tins::NetworkInterface nic( NIC );
    Tins::NetworkInterface::Info info = nic.addresses();
    struct in_addr in_yiaddr, in_ciaddr, in_mask, in_broadcast;
    
    in_ciaddr.s_addr = 0; // ip клиента, указывается в случае, когда клиент .. может отвечать на запросы ARP.
    in_mask.s_addr = dhcp_pool.subnet_mask;
    in_broadcast.s_addr = dhcp_pool.broadcast;
    
    response->opcode( Tins::BootP::BOOTREPLY );
    response->xid( request->xid());
    
    auto dh_request_address = request->search_option( Tins::DHCP::OptionTypes::DHCP_REQUESTED_ADDRESS );
    if( dh_request_address )
    {
        in_yiaddr.s_addr = *( uint32_t* )dh_request_address->data_ptr();
        std::cout << "\trequest " << inet_ntoa( in_yiaddr ) << " " 
            << Tins::HWAddress<6>( request->chaddr()) << std::endl;
        
        if( ntohl( in_yiaddr.s_addr ) > ntohl( dhcp_pool.subnet ))
        {
            if( ntohl( in_yiaddr.s_addr ) < ntohl( dhcp_pool.broadcast ))
                in_yiaddr.s_addr = get_address( in_yiaddr.s_addr, request->chaddr());
            else
            {
                in_yiaddr.s_addr = get_address( 0, request->chaddr());
                response->type( Tins::DHCP::Flags::NAK );
            }
        }
        else
        {
            in_yiaddr.s_addr = get_address( 0, request->chaddr());
            response->type( Tins::DHCP::Flags::NAK );
        }
    }
    else
        in_yiaddr.s_addr = get_address( 0, request->chaddr());
   
    response->yiaddr( inet_ntoa( in_yiaddr ));
    response->ciaddr( inet_ntoa( in_ciaddr ));
    response->siaddr( info.ip_addr );
    response->giaddr( request->giaddr());
    response->chaddr( request->chaddr());
    
    response->server_identifier( info.ip_addr );
    response->subnet_mask( inet_ntoa( in_mask ));
    response->broadcast( inet_ntoa( in_broadcast ));
    response->add_option({ Tins::DHCP::OptionTypes::ROUTERS, sizeof( dhcp_pool.router ), 
        ( const unsigned char* )&dhcp_pool.router });
    response->add_option({ Tins::DHCP::OptionTypes::DOMAIN_NAME_SERVERS, sizeof( dhcp_pool.name_servers ), 
        ( const unsigned char* )dhcp_pool.name_servers });
    response->add_option({ Tins::DHCP::OptionTypes::NTP_SERVERS, sizeof( dhcp_pool.time_servers ), 
        ( const unsigned char* )dhcp_pool.time_servers });
            
    response->domain_name( "same_dh" );
    response->lease_time( TIME_LEASE );
    response->renewal_time( TIME_RENEWAL );
    response->rebind_time( TIME_REBIND );
    response->end();
    
    std::cout << "\tresponse " << inet_ntoa( in_yiaddr ) << " " 
        << Tins::HWAddress<6>( request->chaddr()) << std::endl;
}

void dhcp_service::of_send( Tins::EthernetII* eth )
{
    // auto target_port = data_base->getPort( dpid_, dst_mac_ );
    
    // Send PacketOut.
    of13::PacketOut po;
    Tins::PDU::serialization_type buffer = eth->serialize();
    uint8_t* eth_str = new uint8_t[eth->size()];
                
    for( auto i : buffer )
        eth_str[i] = buffer.at( i );
                
    po.data( eth_str, eth->size());
    of13::OutputAction output_action( in_port_, of13::OFPCML_NO_BUFFER );
    // of13::OutputAction output_action( target_port, of13::OFPCML_NO_BUFFER );
    // of13::OutputAction output_action( in_port_, of13::OFPXMT_OFB_IN_PORT );
    
    po.add_action( output_action );
    switch_manager_->switch_( dpid_ )->connection()->send( po );
    
    // Send PacketOut.
/*        
    {   // Create FlowMod.
        of13::FlowMod fm;
        fm.command( of13::OFPFC_ADD );
        fm.table_id( 0 );
        fm.priority( 2 );
        std::stringstream ss;
        fm.idle_timeout( uint64_t( 60 ));
        fm.hard_timeout( uint64_t( 1800 ));

        ss.str( std::string());
        ss.clear();
        ss << src_mac_;
        fm.add_oxm_field( new of13::EthSrc{ fluid_msg::EthAddress( ss.str())});
        ss.str( std::string());
        ss.clear();
        ss << dst_mac_;
        fm.add_oxm_field( new of13::EthDst{ fluid_msg::EthAddress(ss.str())});

        of13::ApplyActions applyActions;
        // of13::OutputAction output_action( target_port, of13::OFPCML_NO_BUFFER );
        of13::OutputAction output_action( in_port_, of13::OFPCML_NO_BUFFER );
        applyActions.add_action( output_action );
        fm.add_instruction( applyActions );
        switch_manager_->switch_( dpid_ )->connection()->send( fm );
    }   // Create FlowMod.
*/
}

void dhcp_service::onSwitchUp( SwitchPtr sw )
{
    of13::FlowMod fm;
    fm.command( of13::OFPFC_ADD );
    fm.table_id( 0 );
    fm.priority( 1 );
    of13::ApplyActions applyActions;
    of13::OutputAction output_action( of13::OFPP_CONTROLLER, 0xFFFF );
    applyActions.add_action( output_action );
    fm.add_instruction( applyActions );
    sw->connection()->send( fm );
}

uint32_t dhcp_service::mk_addr( uint32_t addr )
{
    std::unordered_map< uint32_t, uint32_t >::iterator it_lease;
    
    uint32_t previous( ~dhcp_pool.subnet_mask &addr );
    previous = ntohl( previous );
    
    for( uint32_t host_addr = previous + 1; host_addr < ntohl( ~dhcp_pool.subnet_mask ); host_addr++ )
    {
        it_lease = lease_base.find( htonl( ntohl( dhcp_pool.subnet ) + host_addr ));
        if( it_lease != lease_base.end())
        {   
            if( it_lease->second < ( uint32_t )time( nullptr ))
                return htonl( ntohl( dhcp_pool.subnet ) + host_addr );
            else
                continue;
        }
        else
            return htonl( ntohl( dhcp_pool.subnet ) + host_addr );
    }

    return 0;
}

bool dhcp_service::of_check_address( uint32_t addr )
{
    struct in_addr address;
    address.s_addr = addr;
    // of13::ICMPv4Type request;
    // request.value( uint8_t value );
    
    of13::PacketOut po;
    po.msg_type( of13::OFPT_ECHO_REQUEST );
    // po.msg_type( 2 );
    // po.data( eth_str, eth->size());
    
    of13::OutputAction output_action( of13::OFPP_ALL, of13::OFPCML_NO_BUFFER );
    po.add_action( output_action );
    
    std::cout << "of check " << inet_ntoa( address ) << std::endl;
    
    // switch_manager_->switch_( dpid_ )->connection()->send( po );
    
    return true;
}

bool dhcp_service::arp_check_address( uint32_t addr )
{
    return true;
}

bool dhcp_service::check_address( uint32_t addr )
{
    struct in_addr address;
    address.s_addr = addr;
    
    const Tins::NetworkInterface nic( NIC );
    Tins::NetworkInterface::Info info = nic.addresses();
    
    char data[] = { "abcdefghijklmnopqrstuvwxyz0123456789abcdefghijklm" };
    Tins::IP ip = Tins::IP( inet_ntoa( address ), info.ip_addr ) / Tins::ICMP(( uint8_t* )data, sizeof( data ));
    
    Tins::ICMP& icmp = ip.rfind_pdu<Tins::ICMP>();
        
    srand( time( nullptr ));
    icmp.type( Tins::ICMP::Flags::ECHO_REQUEST );
    icmp.id(( uint16_t )rand());
        
    ip.flags( Tins::IP::Flags::DONT_FRAGMENT );
    ip.ttl( 64 );
        
    Tins::PacketSender request;
    Tins::PDU *response = request.send_recv( ip, nic );
        
    if( response == 0 )
        return false;
    else
    {
        Tins::ICMP& reply = response->rfind_pdu<Tins::ICMP>();
        if( reply.type() == Tins::ICMP::Flags::ECHO_REPLY )
            return true;
        else
            return false;
    }
    
    return true;

}

uint32_t dhcp_service::get_address( uint32_t request_ip, Tins::HWAddress<6> client_hw )
{   
    /* use boost::multi_index_container */
    boost::bimap< std::string, uint32_t >::left_iterator addr_left;
    boost::bimap< std::string, uint32_t >::right_iterator addr_right;
    std::unordered_map< uint32_t, uint32_t >::iterator it_lease;
    
    std::stringstream ss;
    ss << client_hw;
    std::string str_hw( ss.str());
    
    if( request_ip > 0 )
    {
        addr_left = addr_base.left.find( str_hw );
        if( addr_left != addr_base.left.end())
        {
            if( addr_left->second == request_ip )
            {
                it_lease = lease_base.find( addr_left->second );
                if( it_lease != lease_base.end())
                    lease_base.erase( it_lease );

                lease_base.insert({ addr_left->second, ( uint32_t )time( nullptr ) + TIME_LEASE });
                
                return addr_left->second;
            }

            else
            {
                if( check_address( addr_left->second ))
                    addr_base.left.erase( addr_left );
                else
                {
                    it_lease = lease_base.find( addr_left->second );
                    if( it_lease != lease_base.end())
                    {
                        if( it_lease->second > ( uint32_t )time( nullptr ))
                        {
                            lease_base.erase( it_lease );
                            lease_base.insert({ addr_left->second, ( uint32_t )time( nullptr ) + TIME_LEASE });
                            
                            return addr_left->second;
                        }
                        else
                        {
                            lease_base.erase( it_lease );
                            addr_base.left.erase( addr_left );
                            addr_base.insert({ str_hw, request_ip });
                            lease_base.insert({ request_ip, ( uint32_t )time( nullptr ) + TIME_LEASE });
                            
                            return request_ip;
                        }
                    }
                    else
                    {
                        addr_base.left.erase( addr_left );
                        addr_base.insert({ str_hw, request_ip });
                        lease_base.insert({ request_ip, ( uint32_t )time( nullptr ) + TIME_LEASE });
                        
                        return request_ip;
                    }
                }
            }
        }

        else
        {
            addr_right = addr_base.right.find( request_ip );
            if( addr_right != addr_base.right.end())
            {
                it_lease = lease_base.find( request_ip );
                if( it_lease != lease_base.end())
                {
                    if( it_lease->second > ( uint32_t )time( nullptr ))
                    {
                        uint32_t addr = mk_addr( dhcp_pool.dynamic_hosts );
                        while( check_address( addr ))
                            addr = mk_addr( addr );
                        addr_base.insert({ str_hw, addr });
                        it_lease = lease_base.find( addr );
                        if( it_lease != lease_base.end())
                            lease_base.erase( it_lease );
                        lease_base.insert({ addr, ( uint32_t )time( nullptr ) + TIME_LEASE });
                        
                        return addr;
                    }
                    else
                    {
                        lease_base.erase( it_lease );
                        addr_base.right.erase( addr_right );
                        addr_base.insert({ str_hw, request_ip });
                        lease_base.insert({ request_ip, ( uint32_t )time( nullptr ) + TIME_LEASE });
                        
                        return request_ip;
                    }
                }
                else
                {
                    addr_base.right.erase( addr_right );
                    addr_base.insert({ str_hw, request_ip });
                    lease_base.insert({ request_ip, ( uint32_t )time( nullptr ) + TIME_LEASE });
                    
                    return request_ip;
                }
            }
            else
            {
                it_lease = lease_base.find( request_ip );
                if( it_lease != lease_base.end())
                    lease_base.erase( it_lease );
                addr_base.insert({ str_hw, request_ip });
                lease_base.insert({ request_ip, ( uint32_t )time( nullptr ) + TIME_LEASE });
                
                return request_ip;
            }
        }
    }

    if( request_ip == 0 )
    {
        addr_left = addr_base.left.find( str_hw );
        if( addr_left != addr_base.left.end())
        {
            uint32_t addr = addr_left->second;
            // cite rfc 2131
            while( check_address( addr ))
                addr = mk_addr( addr );
            it_lease = lease_base.find( addr );
            if( it_lease != lease_base.end())
                lease_base.erase( it_lease );
            addr_base.insert({ str_hw, addr });
            lease_base.insert({ addr, ( uint32_t )time( nullptr ) + TIME_LEASE });
            
            return addr;
        }
        else
        {
            uint32_t addr = mk_addr( dhcp_pool.dynamic_hosts );
            while( check_address( addr ))
                addr = mk_addr( addr );
            it_lease = lease_base.find( addr );
            if( it_lease != lease_base.end())
                lease_base.erase( it_lease );
            addr_base.insert({ str_hw, addr });
            lease_base.insert({ addr, ( uint32_t )time( nullptr ) + TIME_LEASE });
            
            return addr;
        }
    }

    return 0;
}
} // namespace runos
