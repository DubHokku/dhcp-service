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
    // "topology",
    "" })

void dhcp_service::init( Loader* loader, const Config& config )
{
    switch_manager_ = SwitchManager::get( loader );
    connect( switch_manager_, &SwitchManager::switchUp, this, &dhcp_service::onSwitchUp );
    
    pool();
    handler_ = Controller::get( loader )->register_handler
    (
        [=]( of13::PacketIn& pi, OFConnectionPtr ofconn ) mutable->bool
        {
            PacketParser pp( pi );
            runos::Packet& pkt( pp );
            
            in_port_ = pkt.load( ofb::in_port );
            dpid_ = ofconn->dpid();
            
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
            
            struct in_addr in_yiaddr, in_ciaddr, in_mask, in_broadcast;
            in_yiaddr.s_addr = get_address( 0, dhcp->chaddr());
            in_ciaddr.s_addr = 0; // ip клиента, указывается в случае, когда клиент .. может отвечать на запросы ARP.
            in_mask.s_addr = dhcp_pool.subnet_mask;
            in_broadcast.s_addr = dhcp_pool.broadcast;
            
            Tins::DHCP *offer = new Tins::DHCP;
            offer->opcode( Tins::BootP::BOOTREPLY );
            offer->xid( dhcp->xid());
            offer->type( Tins::DHCP::Flags::OFFER );
            
            offer->ciaddr( inet_ntoa( in_ciaddr ));
            offer->yiaddr( inet_ntoa( in_yiaddr ));
            offer->siaddr( info.ip_addr );
            offer->giaddr( dhcp->giaddr());
            offer->chaddr( dhcp->chaddr());
            
            offer->server_identifier( info.ip_addr );
            offer->subnet_mask( inet_ntoa( in_mask ));
            offer->broadcast( inet_ntoa( in_broadcast ));
            offer->add_option({ Tins::DHCP::OptionTypes::ROUTERS, sizeof( dhcp_pool.router ), 
                ( const unsigned char* )&dhcp_pool.router });
            offer->add_option({ Tins::DHCP::OptionTypes::DOMAIN_NAME_SERVERS, sizeof( dhcp_pool.name_servers ), 
                ( const unsigned char* )dhcp_pool.name_servers });
            offer->add_option({ Tins::DHCP::OptionTypes::NTP_SERVERS, sizeof( dhcp_pool.time_servers ), 
                ( const unsigned char* )dhcp_pool.time_servers });
            
            offer->domain_name( "same_dh" );
            offer->lease_time( TIME_LEASE );
            offer->renewal_time( TIME_RENEWAL );
            offer->rebind_time( TIME_REBIND );
            offer->end();
            
            std::cout << "dhcp::offer " << inet_ntoa( in_yiaddr ) << std::endl;
            Tins::EthernetII opkt = Tins::EthernetII( src_mac, info.hw_addr ) / 
                Tins::IP( inet_ntoa( in_yiaddr ), info.ip_addr ) / Tins::UDP( 68, 67 ) / *offer;
            
            {   // Send PacketOut.
                of13::PacketOut po;
                Tins::PDU::serialization_type buffer = opkt.serialize();
                uint8_t* str_opkt = new uint8_t[opkt.size()];
                
                for( auto i : buffer )
                    str_opkt[i] = buffer.at( i );
                
                po.data( str_opkt, opkt.size());
                of13::OutputAction output_action( in_port_, of13::OFPCML_NO_BUFFER );
                // of13::OutputAction output_action( in_port_, of13::OFPXMT_OFB_IN_PORT );
                po.add_action( output_action );
                switch_manager_->switch_( dpid_ )->connection()->send( po );
            }   // Send PacketOut.
        }
        if( *dh_type->data_ptr() == Tins::DHCP::Flags::REQUEST )
        {
            std::cout << "DHCP::REQUEST \n";
            
            struct in_addr in_yiaddr, in_ciaddr, in_mask, in_broadcast;
            
            auto dh_request_address = dhcp->search_option( Tins::DHCP::OptionTypes::DHCP_REQUESTED_ADDRESS );
            if( dh_request_address )
                in_yiaddr.s_addr = *( uint32_t* )dh_request_address->data_ptr();
            
            if( ntohl( in_yiaddr.s_addr ) > ntohl( dhcp_pool.subnet ))
            {
                if( ntohl( in_yiaddr.s_addr ) < ntohl( dhcp_pool.broadcast ))
                    in_yiaddr.s_addr = get_address( in_yiaddr.s_addr, dhcp->chaddr());
                else
                    in_yiaddr.s_addr = get_address( 0, dhcp->chaddr());
                    // mk DHCP::NAK; continue;
            }
            else
                in_yiaddr.s_addr = get_address( 0, dhcp->chaddr());
                // mk DHCP::NAK; continue;
            
            in_ciaddr.s_addr = 0; // ip клиента, указывается в случае, когда клиент .. может отвечать на запросы ARP.
            in_mask.s_addr = dhcp_pool.subnet_mask;
            in_broadcast.s_addr = dhcp_pool.broadcast;
            
            Tins::DHCP *ack = new Tins::DHCP;
            ack->opcode( Tins::BootP::BOOTREPLY );
            ack->xid( dhcp->xid());
            ack->type( Tins::DHCP::Flags::ACK );
            
            ack->ciaddr( inet_ntoa( in_ciaddr ));
            ack->yiaddr( inet_ntoa( in_yiaddr ));
            ack->siaddr( info.ip_addr );
            ack->giaddr( dhcp->giaddr());
            ack->chaddr( dhcp->chaddr());
            
            ack->server_identifier( info.ip_addr );
            ack->subnet_mask( inet_ntoa( in_mask ));
            ack->broadcast( inet_ntoa( in_broadcast ));
            ack->add_option({ Tins::DHCP::OptionTypes::ROUTERS, sizeof( dhcp_pool.router ), 
                ( const unsigned char* )&dhcp_pool.router });
            ack->add_option({ Tins::DHCP::OptionTypes::DOMAIN_NAME_SERVERS, sizeof( dhcp_pool.name_servers ), 
                ( const unsigned char* )dhcp_pool.name_servers });
            ack->add_option({ Tins::DHCP::OptionTypes::NTP_SERVERS, sizeof( dhcp_pool.time_servers ), 
                ( const unsigned char* )dhcp_pool.time_servers });
            
            ack->domain_name( "same_dh" );
            ack->lease_time( TIME_LEASE );
            ack->renewal_time( TIME_RENEWAL );
            ack->rebind_time( TIME_REBIND );
            ack->end();
            
            Tins::EthernetII opkt = Tins::EthernetII( src_mac, info.hw_addr ) / 
                Tins::IP( inet_ntoa( in_yiaddr ), info.ip_addr ) / Tins::UDP( 68, 67 ) / *ack;
            
            {   // Send PacketOut.
                of13::PacketOut po;
                Tins::PDU::serialization_type buffer = opkt.serialize();
                uint8_t* str_opkt = new uint8_t[opkt.size()];
                
                for( auto i : buffer )
                    str_opkt[i] = buffer.at( i );
                
                po.data( str_opkt, opkt.size());
                of13::OutputAction output_action( in_port_, of13::OFPCML_NO_BUFFER );
                // of13::OutputAction output_action( in_port_, of13::OFPXMT_OFB_IN_PORT );
                po.add_action( output_action );
                switch_manager_->switch_( dpid_ )->connection()->send( po );
            }   // Send PacketOut.  */
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
