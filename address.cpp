/*
 * class Address implementation, just a nice wrapping around struct
 * sockaddr_storage.
 */
#include "swift.h"


using namespace swift;

#define addr_debug 	false

Address::Address()
{
    if (addr_debug)
	fprintf(stderr,"Addres::Address()\n");
    clear();
}

Address::Address(const char* ip, uint16_t port)
{
    if (addr_debug)
	fprintf(stderr,"Addres::Address(ip=%s,port=%u)\n", ip, port);
    clear();
    set_ip(ip,AF_UNSPEC);
    set_port(port);
}

Address::Address(const char* ip_port)
{
    if (addr_debug)
	fprintf(stderr,"Addres::Address(ip_port=%s)\n", ip_port);
    clear();
    if (strlen(ip_port)>=1024 || strlen(ip_port) == 0)
        return;
    char ipp[1024];
    strncpy(ipp,ip_port,1024);
    if (ipp[0] == '[')
    {
	// IPV6 in square brackets following RFC2732
	char* closesb = strchr(ipp,']');
	if (closesb == NULL)
	    return;
	char* semi = strchr(closesb,':');
	*closesb = '\0';
	if (semi) { // has port
	    *semi = '\0';
	    set_ipv6(ipp+1);
	    set_port(semi+1);
	} else
	{
	    set_ipv6(ipp+1);
	}
    }
    else
    {
	char* semi = strchr(ipp,':');
	if (semi) {
	    *semi = 0;
	    set_ipv4(ipp);
	    set_port(semi+1);
	} else {
	    if (strchr(ipp, '.')) {
		set_ipv4(ipp);
		set_port((uint16_t)0);
	    } else { // Arno: if just port, then IPv6
		set_ipv6("::0");
		set_port(ipp);
	    }
	}
    }
}


Address::Address(uint32_t ipv4addr, uint16_t port)
{
    if (addr_debug)
	fprintf(stderr,"Addres::Address(ipv4addr=%08x,port=%u)\n", ipv4addr, port);
    clear();
    set_ipv4(ipv4addr);
    set_port(port);
}


Address::Address(struct in6_addr ipv6addr, uint16_t port)
{
    clear();
    set_ipv6(ipv6addr);
    set_port(port);
}

void Address::set_port (uint16_t port) {
    if (addr.ss_family == AF_INET) {
	struct sockaddr_in *addr4ptr = (struct sockaddr_in *)&addr;
	addr4ptr->sin_port = htons(port);
    }
    else {
	struct sockaddr_in6 *addr6ptr = (struct sockaddr_in6 *)&addr;
	addr6ptr->sin6_port = htons(port);
    }
}

void Address::set_port (const char* port_str) {
    int p;
    if (sscanf(port_str,"%i",&p))
	set_port(p);
}

void Address::set_ipv4 (uint32_t ipv4) {
    addr.ss_family = AF_INET;
    struct sockaddr_in *addr4ptr = (struct sockaddr_in *)&addr;
    addr4ptr->sin_addr.s_addr = htonl(ipv4);
}

void Address::set_ipv6(struct in6_addr &ipv6) {
    addr.ss_family = AF_INET6;
    struct sockaddr_in6 *addr6ptr = (struct sockaddr_in6 *)&addr;
    memcpy(&addr6ptr->sin6_addr.s6_addr,&ipv6.s6_addr,sizeof(ipv6.s6_addr) );
}


void Address::clear () {
    memset(&addr,0,sizeof(struct sockaddr_storage));
    addr.ss_family = AF_UNSPEC;
}

uint32_t Address::ipv4() const
{
    if (addr.ss_family == AF_INET)
    {
	struct sockaddr_in *addr4ptr = (struct sockaddr_in *)&addr;
	return ntohl(addr4ptr->sin_addr.s_addr);
    }
    else
	return (uint32_t)INADDR_ANY;
}

struct in6_addr Address::ipv6() const
{
    if (addr.ss_family == AF_INET6)
    {
	struct sockaddr_in6 *addr6ptr = (struct sockaddr_in6 *)&addr;
	return addr6ptr->sin6_addr;
    }
    else
	return in6addr_any;
}


uint16_t Address::port () const
{
    if (addr.ss_family == AF_INET) {
	struct sockaddr_in *addr4ptr = (struct sockaddr_in *)&addr;
	return ntohs(addr4ptr->sin_port);
    }
    else {
	struct sockaddr_in6 *addr6ptr = (struct sockaddr_in6 *)&addr;
	return ntohs(addr6ptr->sin6_port);
    }
}

bool Address::operator == (const Address& b) const {

    if (addr.ss_family == AF_UNSPEC && b.addr.ss_family == AF_UNSPEC)
    {
	// For comparing empty Address-es
	return true;
    }
    else if (addr.ss_family == AF_INET && b.addr.ss_family == AF_INET)
    {
	struct sockaddr_in *aaddr4ptr = (struct sockaddr_in *)&addr;
	struct sockaddr_in *baddr4ptr = (struct sockaddr_in *)&b.addr;
	return aaddr4ptr->sin_port   == baddr4ptr->sin_port &&
	       aaddr4ptr->sin_addr.s_addr==baddr4ptr->sin_addr.s_addr;
    }
    else if (addr.ss_family == AF_INET6 && b.addr.ss_family == AF_INET6)
    {
	struct sockaddr_in6 *aaddr6ptr = (struct sockaddr_in6 *)&addr;
	struct sockaddr_in6 *baddr6ptr = (struct sockaddr_in6 *)&b.addr;
	return aaddr6ptr->sin6_port   == baddr6ptr->sin6_port &&
	       !memcmp(&aaddr6ptr->sin6_addr.s6_addr,&baddr6ptr->sin6_addr.s6_addr,sizeof(struct sockaddr_in6) );
    }
    else // IPv4-mapped IP6 addr
    {
	struct sockaddr_in6 *xaddr6ptr = NULL;
	struct sockaddr_in  *yaddr4ptr = NULL;
	if (addr.ss_family == AF_INET6 && b.addr.ss_family == AF_INET)
	{
	    xaddr6ptr = (struct sockaddr_in6 *)&addr;
	    yaddr4ptr = (struct sockaddr_in *)&b.addr;
	}
	else
	{
	    xaddr6ptr = (struct sockaddr_in6 *)&b.addr;
	    yaddr4ptr = (struct sockaddr_in *)&addr;
	}
	// Convert IPv4 to IPv4-mapped IPv6 RFC4291
	struct sockaddr_in6 y6map;
	y6map.sin6_port = yaddr4ptr->sin_port;
	int i=0;
	for (i=0; i<10; i++)
	    y6map.sin6_addr.s6_addr[i] = 0x00;
	for (i=10; i<12; i++)
	    y6map.sin6_addr.s6_addr[i] = 0xFF;
	memcpy(&y6map.sin6_addr.s6_addr[i], &yaddr4ptr->sin_addr.s_addr, 4);

	struct sockaddr_in6 *yaddr6ptr = (struct sockaddr_in6 *)&y6map;
	return xaddr6ptr->sin6_port == yaddr6ptr->sin6_port &&
	       !memcmp(&xaddr6ptr->sin6_addr.s6_addr,&yaddr6ptr->sin6_addr.s6_addr,sizeof(struct in6_addr) );
    }
}


std::string Address::str() const
{
    return ipstr(true);
}

std::string Address::ipstr(bool includeport) const
{
    char node[256];
    char service[256];

    if (addr_debug)
	fprintf(stderr,"Address::ipstr(includeport=%d): addr family %d\n", includeport, addr.ss_family );

    if (addr.ss_family == AF_UNSPEC)
	return "AF_UNSPEC";

    /*
    if (addr.ss_family == AF_INET) {
	struct sockaddr_in *addr4ptr = (struct sockaddr_in *)&addr;
	fprintf(stderr,"Address::ipstr:v4 OCTET %08lx\n", addr4ptr->sin_addr.s_addr );
    }
    else {
	struct sockaddr_in6 *addr6ptr = (struct sockaddr_in6 *)&addr;
	for (int i=0; i<16; i++)
	    fprintf(stderr,"Address::ipstr:v6 OCTET %02x\n", addr6ptr->sin6_addr.s6_addr[i] );

    }*/

    // See RFC3493
    // Arno, 2013-06-05: pass real IP sockaddr length
    int ret = getnameinfo((const struct sockaddr *)&addr, get_real_sockaddr_length(),
                           node, (socklen_t)sizeof(node),
                           service, (socklen_t)sizeof(service),
                           NI_NUMERICHOST | NI_NUMERICSERV);
    if (ret == 0)
    {
	// Strip off zone index e.g. 2001:610:110:6e1:7578:776f:e141:d2bb%3435973836
	std::string nodestr(node);
	int idx = nodestr.find("%");
	if (idx != std::string::npos)
	    nodestr = nodestr.substr(0,idx);

	if (includeport)
	    return nodestr+":"+std::string(service);
	else
	    return nodestr;
    }
    else
    {
	print_error("getnameinfo error");
	return "getnameinfo failed";
    }
}


bool Address::is_private() const
{
    if (addr.ss_family == AF_INET)
    {
	uint32_t no = ipv4(); uint8_t no0 = no>>24,no1 = (no>>16)&0xff;
	if (no0 == 10) return true;
	else if (no0 == 172 && no1 >= 16 && no1 <= 31) return true;
	else if (no0 == 192 && no1 == 168) return true;
	else return false;
    }
    else
    {
	// IPv6 Link-local address RFC4291
	struct in6_addr s6 = ipv6();
	return IN6_IS_ADDR_LINKLOCAL(&s6);
    }
}

void Address::set_ipv4 (const char* ip_str)
{
    set_ip(ip_str,AF_INET);
}

void Address::set_ipv6 (const char* ip_str)
{
    set_ip(ip_str,AF_INET6);
}


void Address::set_ip(const char* ip_str, int family)
{
    if (addr_debug)
	fprintf(stderr,"Address::set_ip: %s family %d\n", ip_str, family );

    struct addrinfo hint;
    hint.ai_flags = AI_PASSIVE;
    hint.ai_family = family;
    hint.ai_socktype = 0;
    hint.ai_protocol = 0;
    hint.ai_addrlen = 0;
    hint.ai_canonname = NULL;
    hint.ai_addr = NULL;
    hint.ai_next = NULL;

    struct addrinfo *results;
    int ret = getaddrinfo(ip_str, NULL,  &hint, &results);
    if (ret == 0)
    {
	// Copy sockaddr to sockaddr_storage
	memcpy(&addr,results->ai_addr,results->ai_addrlen);

	if (addr_debug)
	    fprintf(stderr,"Address::set_ip: result %s\n", this->str().c_str() );
    }
    if (results != NULL)
	freeaddrinfo(results);
}


socklen_t Address::get_real_sockaddr_length() const
{
    if (addr.ss_family == AF_INET)
    {
        return sizeof(struct sockaddr_in);
    }
    else if (addr.ss_family == AF_INET6)
    {
        return sizeof(struct sockaddr_in6);
    }
    else
    {
        return 0;
    }
}
