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
    
    pool();
    handler_ = Controller::get( loader )->register_handler
    (
        [=]( of13::PacketIn& pi, OFConnectionPtr ofconn ) mutable->bool
        {
            PacketParser pp( pi );
            runos::Packet& pkt( pp );
            
            src_mac_ = pkt.load( oxm::eth_src());
            dst_mac_ = pkt.load( oxm::eth_dst());
            in_port_ = pkt.load( oxm::in_port());
            dpid_ = ofconn->dpid();
            
            // auto target_port = data_base->getPort( dpid_, dst_mac_ );
            // auto target_port = HostsDatabase::getPort( dpid_, dst_mac_ );
            
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
            
            struct in_addr in_yiaddr;
            Tins::DHCP *offer = new Tins::DHCP;
            offer->type( Tins::DHCP::Flags::OFFER );
            
            auto dh_request_address = dhcp->search_option( Tins::DHCP::OptionTypes::DHCP_REQUESTED_ADDRESS );
            if( dh_request_address )
            {
                in_yiaddr.s_addr = *( uint32_t* )dh_request_address->data_ptr();
                // std::cout << "\trequest " << inet_ntoa( in_yiaddr ) << std::endl;
                
                if( in_yiaddr.s_addr == 0 )
                    in_yiaddr.s_addr = get_address( 0, dhcp->chaddr());
                else
                {
                    if( check_address( in_yiaddr.s_addr ))
                        in_yiaddr.s_addr = 0;
                    else
                        in_yiaddr.s_addr = get_address( in_yiaddr.s_addr, dhcp->chaddr());
                    if( in_yiaddr.s_addr == 0 )
                        offer->type( Tins::DHCP::Flags::NAK );
                }
            }
            else
                in_yiaddr.s_addr = get_address( 0, dhcp->chaddr());
            
            offer->padding( htons( 128 ));
            offer->yiaddr( inet_ntoa( in_yiaddr ));
            mk_reply( dhcp, offer );
            
            // std::cout << "\tresponse " << inet_ntoa( in_yiaddr ) << std::endl;
            Tins::EthernetII opkt = Tins::EthernetII( src_mac, info.hw_addr ) /
                Tins::IP( offer->yiaddr(), info.ip_addr ) / Tins::UDP( 68, 67 ) / *offer;
                
            of_send( &opkt );
        }
        if( *dh_type->data_ptr() == Tins::DHCP::Flags::REQUEST )
        {
            std::cout << "DHCP::REQUEST \n";

            struct in_addr in_yiaddr;
            Tins::DHCP *ack = new Tins::DHCP;
            
            auto dh_request_address = dhcp->search_option( Tins::DHCP::OptionTypes::DHCP_REQUESTED_ADDRESS );
            if( dh_request_address )
            {
                in_yiaddr.s_addr = *( uint32_t* )dh_request_address->data_ptr();
                in_yiaddr.s_addr = get_address( in_yiaddr.s_addr, dhcp->chaddr());
            }
            else
                in_yiaddr.s_addr = 0;
            
            if( in_yiaddr.s_addr == 0 )
                ack->type( Tins::DHCP::Flags::NAK );
            else
                ack->type( Tins::DHCP::Flags::ACK );
                        
            ack->yiaddr( inet_ntoa( in_yiaddr ));
            mk_reply( dhcp, ack );
                
            Tins::EthernetII opkt = Tins::EthernetII( src_mac, info.hw_addr ) / 
                Tins::IP( ack->yiaddr(), info.ip_addr ) / Tins::UDP( 68, 67 ) / *ack;
                
            auto dh_server_identifier = dhcp->search_option( Tins::DHCP::OptionTypes::DHCP_SERVER_IDENTIFIER );
            if( dh_server_identifier )
            {
                if( dhcp->server_identifier() == info.ip_addr )
                    of_send( &opkt );
                else
                    std::cout << "remove lease_base rec. " << inet_ntoa( in_yiaddr ) << std::endl;
            }
            else
                of_send( &opkt );
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
    struct in_addr in_ciaddr, in_mask, in_broadcast;
    
    in_ciaddr.s_addr = 0; // ip клиента, указывается в случае, когда клиент .. может отвечать на запросы ARP.
    in_mask.s_addr = dhcp_pool.subnet_mask;
    in_broadcast.s_addr = dhcp_pool.broadcast;
    
    response->opcode( Tins::BootP::BOOTREPLY );
    response->xid( request->xid());
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
}

void dhcp_service::of_send( Tins::EthernetII* eth )
{
    of13::PacketOut po;
    Tins::PDU::serialization_type buffer = eth->serialize();
    uint8_t* eth_str = new uint8_t[eth->size()];
                
    // for( auto i : buffer )
    for( unsigned i = 0; i < eth->size(); i++ )
        eth_str[i] = buffer.at( i );
         
    po.data( eth_str, eth->size());
    of13::OutputAction output_action( in_port_, of13::OFPCML_NO_BUFFER );
    // of13::OutputAction output_action( in_port_, of13::OFPXMT_OFB_IN_PORT );
    
    po.add_action( output_action );
    switch_manager_->switch_( dpid_ )->connection()->send( po );
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

bool dhcp_service::arp_resolve( uint32_t addr, Tins::HWAddress<6>* hw_addr )
{
    struct in_addr address;
    address.s_addr = addr;
    Tins::HWAddress<6> client_hw;
    
    std::cout << "  arp_resolve( " << inet_ntoa( address ) << " ) \t";
    Tins::PacketSender sender;
    sender.default_interface( NIC );
    try
    {
        client_hw = Tins::Utils::resolve_hwaddr( inet_ntoa( address ), sender );
        hw_addr = &client_hw;
        std::cout << client_hw << std::endl;
        return true;
    }
    catch( std::exception &ex )
    {
        std::cout << "timeout \n";
        return false;
    }

    return true;
}

bool dhcp_service::icmp_echo( uint32_t addr )
{
    struct in_addr address;
    address.s_addr = addr;
    
    std::cout << "  icmp_echo( " << inet_ntoa( address ) << " ) \t";
    const Tins::NetworkInterface nic( NIC );
    Tins::NetworkInterface::Info info = nic.addresses();
    
    char data[] = { "abcdefghijklmnopqrstuvwxyz0123456789abcdefghijklm" };
    Tins::IP ip = Tins::IP( inet_ntoa( address ), info.ip_addr ) / 
        Tins::ICMP(( uint8_t* )data, sizeof( data ));
    
    Tins::ICMP& icmp = ip.rfind_pdu<Tins::ICMP>();
        
    srand( time( nullptr ));
    icmp.type( Tins::ICMP::Flags::ECHO_REQUEST );
    icmp.id(( uint16_t )rand());
        
    ip.flags( Tins::IP::Flags::DONT_FRAGMENT );
    ip.ttl( 64 );
        
    Tins::PacketSender request;
    Tins::PDU *response = request.send_recv( ip, nic );
        
    if( response == 0 )
    {   std::cout << "timeout \n";
        return false;
    }
    else
    {   
        Tins::ICMP& reply = response->rfind_pdu<Tins::ICMP>();
        if( reply.type() == Tins::ICMP::Flags::ECHO_REPLY )
        {   std::cout << "ICMP::ECHO_REPLY \n";
            return true;
        }
        else
        {   std::cout << "reply " << reply.type() << std::endl;
            return false;
        }
    }
    
    return true;
}

bool dhcp_service::of_echo( uint32_t addr )
{
    struct in_addr address;
    address.s_addr = addr;
    
    std::cout << "  of_echo( " << inet_ntoa( address ) << " )\t false \n";
    // const Tins::NetworkInterface nic( NIC );
    // Tins::NetworkInterface::Info info = nic.addresses();
    // of13::OutputAction output_action( of13::OFPP_ALL, of13::OFPCML_NO_BUFFER );
/*    
    {
        of13::PacketOut po;
        po.data( pi.data(), pi.data_len());
        // po.in_port( in_port_ );
        of13::OutputAction output_action( of13::OFPP_ALL, of13::OFPCML_NO_BUFFER );
        
        po.add_action( output_action );
        switch_manager_->switch_( dpid_ )->connection()->send( po );
    }
*/    
    return false;
}

bool dhcp_service::check_address( uint32_t addr )
{
    if( arp_resolve( addr, nullptr ))
        return true;
    
    if( icmp_echo( addr ))
        return true;
    
    if( of_echo( addr ))
        return true;
    
    return false;
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
        if( ntohl( request_ip ) <= ntohl( dhcp_pool.subnet ))
            return 0;
        if( ntohl( request_ip ) >= ntohl( dhcp_pool.broadcast ))
            return 0; 
        
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
