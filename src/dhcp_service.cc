#include "dhcp_service.hpp"

#include "PacketParser.hpp"
#include "api/Packet.hpp"
#include <runos/core/logging.hpp>

namespace runos
{

using namespace fluid_msg;
    
REGISTER_APPLICATION( dhcp_service, 
{
    "controller",
    "switch-manager",
    // "topology",
    "" })

void dhcp_service::init( Loader* loader, const Config& config )
{
    inet_aton( "192.168.1.0", ( struct in_addr* )&dhcp_pool.subnet );
    inet_aton( "255.255.255.0", ( struct in_addr* )&dhcp_pool.subnet_mask );
    inet_aton( "192.168.1.255", ( struct in_addr* )&dhcp_pool.broadcast );
    
    inet_aton( "192.168.1.1", ( struct in_addr* )&dhcp_pool.router );
    inet_aton( "192.168.1.2", ( struct in_addr* )&dhcp_pool.name_servers[0]);
    inet_aton( "192.168.1.4", ( struct in_addr* )&dhcp_pool.name_servers[1]);
    inet_aton( "192.168.1.7", ( struct in_addr* )&dhcp_pool.name_servers[2]);
    inet_aton( "192.168.1.2", ( struct in_addr* )&dhcp_pool.time_servers[0]);
    inet_aton( "192.168.1.4", ( struct in_addr* )&dhcp_pool.time_servers[1]);  
    
    Tins::NetworkInterface nic( NIC );
    Tins::NetworkInterface::Info info = nic.addresses();

    switch_manager_ = SwitchManager::get( loader );
    handler_ = Controller::get( loader )->register_handler
    (
        [=]( of13::PacketIn& pi, OFConnectionPtr ofconn ) mutable->bool
        {
            std::cout << "_handler\n";
            PacketParser pp( pi );
            runos::Packet& pkt( pp );
            
            src_mac_ = pkt.load( ofb::eth_src );
            dst_mac_ = pkt.load( ofb::eth_dst );
            in_port_ = pkt.load( ofb::in_port );
            dpid_ = ofconn->dpid();            
            
            if( pi.data_len() < 14 )
                return true;
            
            Tins::EthernetII frame(( const uint8_t* )pi.data(), pi.data_len());
            if( frame.payload_type() != 2048 )
                return true;
            
            Tins::IP *ip = frame.find_pdu<Tins::IP>();
            if( ip->protocol() != 17 )
                return true;
            
            Tins::UDP *udp = frame.find_pdu<Tins::UDP>();
            if( udp->dport() != 67 )
                return true;
            
            Tins::DHCP dhcp = udp->find_pdu<Tins::RawPDU>()->to<Tins::DHCP>();
            
            const auto dh_type = dhcp.search_option( Tins::DHCP::DHCP_MESSAGE_TYPE );
            if( dh_type )
            {
                if( *dh_type->data_ptr() == Tins::DHCP::Flags::DISCOVER )
                {
                    std::cout << "DHCP::DISCOVER \n";
                    
                    struct in_addr in_yiaddr, in_ciaddr, in_mask, in_broadcast;
                    in_yiaddr.s_addr = get_address( 0, dhcp.chaddr());
                    in_ciaddr.s_addr = 0; // ip клиента, указывается в случае, когда клиент .. может отвечать на запросы ARP.
                    in_mask.s_addr = dhcp_pool.subnet_mask;
                    in_broadcast.s_addr = dhcp_pool.broadcast;
                
                    Tins::DHCP *offer = new Tins::DHCP;
                    offer->opcode( Tins::BootP::BOOTREPLY );
                    offer->xid( dhcp.xid());
                    offer->type( Tins::DHCP::Flags::OFFER );
                
                    offer->ciaddr( inet_ntoa( in_ciaddr ));
                    offer->yiaddr( inet_ntoa( in_yiaddr ));
                    offer->siaddr( info.ip_addr );
                    offer->giaddr( dhcp.giaddr());
                    offer->chaddr( dhcp.chaddr());
                
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
                
                    Tins::EthernetII opkt = Tins::EthernetII( frame.src_addr(), info.hw_addr ) / 
                        Tins::IP( inet_ntoa( in_yiaddr ), info.ip_addr ) / Tins::UDP( 68, 67 ) / *offer;
                        
                    {   // Send PacketOut.
                        of13::PacketOut po;
                        
                        uint32_t leng_opkt = opkt.size();
                        uint8_t* str_opkt = new uint8_t[leng_opkt];;
                        Tins::PDU::serialization_type buffer = opkt.serialize();
                        for( unsigned i = 0; i < leng_opkt; i++ )
                            str_opkt[i] = buffer.at( i );
    
                        po.data(str_opkt, leng_opkt );
                        of13::OutputAction output_action( in_port_, of13::OFPCML_NO_BUFFER );
                        po.add_action( output_action );
                        switch_manager_->switch_( dpid_ )->connection()->send( po );
                    }   // Send PacketOut.
                }
                if( *dh_type->data_ptr() == Tins::DHCP::Flags::REQUEST )
                {
                    std::cout << "DHCP::REQUEST \n";
                    
                    struct in_addr in_yiaddr, in_ciaddr, in_mask, in_broadcast;
                
                    auto dh_request_address = dhcp.search_option( Tins::DHCP::OptionTypes::DHCP_REQUESTED_ADDRESS );
                    if( dh_request_address )
                        in_yiaddr.s_addr = *( uint32_t* )dh_request_address->data_ptr();
                
                    if( ntohl( in_yiaddr.s_addr ) > ntohl( dhcp_pool.subnet ))
                    {
                        if( ntohl( in_yiaddr.s_addr ) < ntohl( dhcp_pool.broadcast ))
                            in_yiaddr.s_addr = get_address( in_yiaddr.s_addr, dhcp.chaddr());
                        else
                            in_yiaddr.s_addr = get_address( 0, dhcp.chaddr());
                            // mk DHCP::NAK; continue;
                    }
                    else
                        in_yiaddr.s_addr = get_address( 0, dhcp.chaddr());
                        // mk DHCP::NAK; continue;
                
                    in_ciaddr.s_addr = 0; // ip клиента, указывается в случае, когда клиент .. может отвечать на запросы ARP.
                    in_mask.s_addr = dhcp_pool.subnet_mask;
                    in_broadcast.s_addr = dhcp_pool.broadcast;
                
                    Tins::DHCP *ack = new Tins::DHCP;
                    ack->opcode( Tins::BootP::BOOTREPLY );
                    ack->xid( dhcp.xid());
                    ack->type( Tins::DHCP::Flags::ACK );
                
                    ack->ciaddr( inet_ntoa( in_ciaddr ));
                    ack->yiaddr( inet_ntoa( in_yiaddr ));
                    ack->siaddr( info.ip_addr );
                    ack->giaddr( dhcp.giaddr());
                    ack->chaddr( dhcp.chaddr());
                
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
                
                    Tins::EthernetII opkt = Tins::EthernetII( frame.src_addr(), info.hw_addr ) / 
                        Tins::IP( inet_ntoa( in_yiaddr ), info.ip_addr ) / Tins::UDP( 68, 67 ) / *ack;
                    
                    {   // Send PacketOut.
                        of13::PacketOut po;
                        
                        uint32_t leng_opkt = opkt.size();
                        uint8_t* str_opkt = new uint8_t[leng_opkt];;
                        Tins::PDU::serialization_type buffer = opkt.serialize();
                        for( unsigned i = 0; i < leng_opkt; i++ )
                            str_opkt[i] = buffer.at( i );
    
                        po.data(str_opkt, leng_opkt );
                        of13::OutputAction output_action( in_port_, of13::OFPCML_NO_BUFFER );
                        po.add_action( output_action );
                        switch_manager_->switch_( dpid_ )->connection()->send( po );
                    }   // Send PacketOut.
                    
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
            return true;
        }, -5
    );  
}

bool dhcp_service::check_address( uint32_t ip )
{
    struct in_addr address;
    address.s_addr = ip;
    Tins::HWAddress<6> client_hw;
    
    Tins::PacketSender sender;
    sender.default_interface( NIC );
    try
    {
        client_hw = Tins::Utils::resolve_hwaddr( inet_ntoa( address ), sender );
        // std::cout << "have response \n";
        
        return true;
    }
    catch( std::exception &ex )
    {
        // std::cout << "response timeout \n";
        return false;
    }

    return true;
}
    
uint32_t dhcp_service::mk_addr()
{
    std::unordered_map< uint32_t, uint32_t >::iterator it_lease;
    
    for( uint32_t host_addr = 1; host_addr < ntohl( ~dhcp_pool.subnet_mask ); host_addr++ )
    {
        it_lease = lease_base.find( dhcp_pool.subnet + host_addr );
        if( it_lease != lease_base.end())
        {
            if( it_lease->second > ( uint32_t )time( nullptr ))
                return htonl( ntohl( dhcp_pool.subnet ) + host_addr );
            else
                continue;
        }
        else
            return htonl( ntohl( dhcp_pool.subnet ) + host_addr );
    }

    return 0;
}
    
uint32_t dhcp_service::get_address( uint32_t request_ip, Tins::HWAddress<6> client_hw )
{
    std::unordered_map< std::string, uint32_t >::iterator it_addr;
    std::unordered_map< uint32_t, uint32_t >::iterator it_lease;
    
    std::stringstream ss;
    ss << client_hw;
    std::string str_hw( ss.str());
    
    if( request_ip > 0 )
    {
        it_addr = addr_base.find( str_hw );
        if( it_addr != addr_base.end())
        {
            if( it_addr->second == request_ip )
            {
                it_lease = lease_base.find( it_addr->second );
                if( it_lease != lease_base.end())
                    lease_base.erase( it_lease );
                lease_base.insert({ it_addr->second, ( uint32_t )time( nullptr ) + TIME_LEASE });
                
                return it_addr->second;
            }
            else
            {   
                if( check_address( it_addr->second ))
                    addr_base.erase( it_addr );
                else
                {
                    it_lease = lease_base.find( it_addr->second );
                    if( it_lease != lease_base.end())
                    {
                        if( it_lease->second > ( uint32_t )time( nullptr ))
                        {
                            lease_base.erase( it_lease );
                            lease_base.insert({ it_addr->second, ( uint32_t )time( nullptr ) + TIME_LEASE });
                        
                            return it_addr->second;
                        }
                        else
                        {
                            lease_base.erase( it_lease );
                            addr_base.erase( it_addr );
                            addr_base.insert({ str_hw, request_ip });
                            lease_base.insert({ request_ip, ( uint32_t )time( nullptr ) + TIME_LEASE });
                        
                            return request_ip;
                        }
                    }
                    else
                    {
                        addr_base.erase( it_addr );
                        addr_base.insert({ str_hw, request_ip });
                        lease_base.insert({ request_ip, ( uint32_t )time( nullptr ) + TIME_LEASE });
                        
                        return request_ip;
                    }
                }
            }
        }
        else
        {
            it_addr = std::find_if( addr_base.begin(), addr_base.end(), [&request_ip]( const std::pair< std::string, uint32_t >& test )
            {
                if( test.second == request_ip )
                    return true;
                return false;
            });
            if( it_addr != addr_base.end())
            {
                it_lease = lease_base.find( request_ip );
                if( it_lease != lease_base.end())
                {
                    if( it_lease->second > ( uint32_t )time( nullptr ))
                    {
                        uint32_t addr = mk_addr();
                        while( check_address( addr ))
                            addr = mk_addr();
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
                        addr_base.erase( it_addr );
                        addr_base.insert({ str_hw, request_ip });
                        lease_base.insert({ request_ip, ( uint32_t )time( nullptr ) + TIME_LEASE });
                        
                        return request_ip;
                    }
                }
                else
                {
                    addr_base.erase( it_addr );
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
        it_addr = addr_base.find( str_hw );
        if( it_addr != addr_base.end())
        {
            uint32_t addr = mk_addr();
            while( check_address( addr ))
                addr = mk_addr();
            it_lease = lease_base.find( addr );
            if( it_lease != lease_base.end())
                lease_base.erase( it_lease );
            addr_base.insert({ str_hw, addr });
            lease_base.insert({ addr, ( uint32_t )time( nullptr ) + TIME_LEASE });
            
            return addr;
        }
        else
        {
            uint32_t addr = mk_addr();
            while( check_address( addr ))
                addr = mk_addr();
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
