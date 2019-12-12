#include "../include/dhcp_service.h"


int main( int ac, char** av )
{
    dhcp_service dhcp_server;

    inet_aton( "192.168.1.0", ( struct in_addr* )&dhcp_server.dhcp_pool.subnet );
    inet_aton( "255.255.255.0", ( struct in_addr* )&dhcp_server.dhcp_pool.subnet_mask );
    inet_aton( "192.168.1.255", ( struct in_addr* )&dhcp_server.dhcp_pool.broadcast );
    
    inet_aton( "192.168.1.1", ( struct in_addr* )&dhcp_server.dhcp_pool.router );
    inet_aton( "192.168.1.2", ( struct in_addr* )&dhcp_server.dhcp_pool.name_servers[0]);
    inet_aton( "192.168.1.4", ( struct in_addr* )&dhcp_server.dhcp_pool.name_servers[1]);
    inet_aton( "192.168.1.7", ( struct in_addr* )&dhcp_server.dhcp_pool.name_servers[2]);
    
    inet_aton( "192.168.1.2", ( struct in_addr* )&dhcp_server.dhcp_pool.time_servers[0]);
    inet_aton( "192.168.1.4", ( struct in_addr* )&dhcp_server.dhcp_pool.time_servers[1]);

    dhcp_server.run();
    
    return 0;
}
