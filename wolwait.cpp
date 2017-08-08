/* wolwait.cpp - Start a WOL system and wait for it to boot
 * By Daniel Collins (2009 - 2017)
 * Released to public domain
 *
 * System status is checked by attempting to connect to a TCP port, if the
 * connection succeeds the program exits with status 0, otherwise it will send
 * another WOL packet and retry the connection. If the timeout is reached
 * before a successful connection occurs, the program will exit with status 2.
 * Other status codes will be returned upon error.
 *
 * This is only tested on Linux, but it will probably work fine on other UNIXes
 * like BSD.
 *
 * Compile with: g++ -o wolwait wolwait.cpp -lresolv
*/

#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <arpa/nameser.h>
#include <resolv.h>
#include <sysexits.h>

#define valid_port(port) (atoi(port) >= 1 && atoi(port) <= 65535)

static void usage(char const *argv0) {
	fprintf(stderr, "Usage: %s [options] <-D|MAC address> <Hostname/IP> <Port>\n\n", argv0);
	fprintf(stderr, "-w <timeout>  Specify timeout in seconds (Default: 300)\n");
	fprintf(stderr, "-f            Wait forever\n");
	fprintf(stderr, "-u            Send WOL packets directly to the host\n");
	fprintf(stderr, "              (Usually requires a static ARP table entry)\n");
	fprintf(stderr, "-A <address>  Send WOL packets to this address\n");
	fprintf(stderr, "-P <port>     Send WOL packets to this port\n");
	fprintf(stderr, "-d <delay>    Time to wait between connection attempts\n");
	fprintf(stderr, "-D            Get MAC address from DNS TXT records\n");
}

/* Attempt to parse a MAC address string and write the binary form to addr.
 * Returns true if the address is well-formed, false otherwise.
*/
static bool parse_mac(unsigned char *addr, char const *string) {
	for(int i = 0; i < 6; i++)
	{
		if(strchr(":-.", string[0]) && isxdigit(string[1]))
		{
			string++;
		}
		
		if(isxdigit(string[0]) && isxdigit(string[1]))
		{
			std::string byte(string, 0, 2);
			addr[i] = strtoul(byte.c_str(), NULL, 16);
			
			string += 2;
		}
		else if(isxdigit(string[0]))
		{
			std::string byte(string, 0, 1);
			addr[i] = strtoul(byte.c_str(), NULL, 16);
			
			string++;
		}
		else{
			return false;
		}
	}
	
	if(*string)
	{
		return false;
	}
	
	return true;
}

#define WOL_PACKET_SIZE 102

static void build_wol_packet(unsigned char *buf, unsigned char *mac) {
	memset(buf, 0xFF, 6);
	
	for(int i = 1; i <= 16; i++)
	{
		memcpy(buf + i * 6, mac, 6);
	}
}

/* Lookup a DNS host, search for any TXT records that resemble a MAC address and
 * write the binary form of the first match to addr.
 *
 * Dies on errors or if none are found.
*/
static int mac_from_dns(unsigned char *addr, const char *hostname) {
	unsigned char buf[PACKETSZ];
	
	int len = res_search(hostname, C_IN, T_TXT, (u_char*)buf, sizeof(buf));
	if(len == -1)
	{
		if(h_errno == NO_ADDRESS)
		{
			fprintf(stderr, "No TXT records found for %s in DNS\n", hostname);
			return EX_NOHOST;
		}
		else{
			fprintf(stderr, "%s: %s\n", hostname, hstrerror(h_errno));
			return EX_UNAVAILABLE;
		}
	}
	
	ns_msg response;
	
	if(ns_initparse(buf, len, &response) == -1)
	{
		fprintf(stderr, "ns_initparse: %s\n", hstrerror(h_errno));
		return EX_PROTOCOL;
	}
	
	int rcount = ns_msg_count(response, ns_s_an);
	
	for(int i = 0; i < rcount; i++)
	{
		ns_rr rec;
		
		if(ns_parserr(&response, ns_s_an, i, &rec) == -1)
		{
			fprintf(stderr, "ns_parserr: %s\n", hstrerror(h_errno));
			return EX_PROTOCOL;
		}
		
		char *rec_buf = ((char*)ns_rr_rdata(rec)) + 1;
		int rec_len   = ns_rr_rdlen(rec) - 1;
		
		std::string rec_str(rec_buf, rec_len);
		
		if(parse_mac(addr, rec_str.c_str()))
		{
			return EX_OK;
		}
	}
	
	fprintf(stderr, "None of the TXT records for %s look like a MAC address\n", hostname);
	return EX_NOHOST;
}

int main(int argc, char **argv) {
	int arg;
	
	int timeout = 300;
	unsigned char dest_mac[6];
	int loop_wait = 5;
	bool use_dns = false;
	
	opterr = 0;
	
	/* Address to send WOL packets to */
	const char *wol_host = "255.255.255.255";
	const char *wol_port = "9";
	bool wol_direct      = false;
	
	while((arg = getopt(argc, argv, "w:fuA:P:d:D")) != -1)
	{
		if(arg == '?')
		{
			usage(argv[0]);
			return 0;
		}
		else if(arg == 'w')
		{
			timeout = atoi(optarg);
			
			if(timeout <= 0) {
				fprintf(stderr, "Invalid timeout value\n");
				return EX_USAGE;
			}
		}
		else if(arg == 'f')
		{
			timeout = 0;
		}
		else if(arg == 'u')
		{
			wol_direct = true;
		}
		else if(arg == 'A')
		{
			wol_host = optarg;
		}
		else if(arg == 'P')
		{
			if(!valid_port(optarg)) {
				fprintf(stderr, "Invalid broadcast port\n");
				return 1;
			}
			
			wol_port = optarg;
		}
		else if(arg == 'd')
		{
			loop_wait = atoi(optarg);
			
			if(loop_wait <= 0) {
				fprintf(stderr, "Invalid reconnect delay\n");
				return EX_USAGE;
			}
		}
		else if(arg == 'D')
		{
			use_dns = true;
		}
	}
	
	if(optind + (use_dns ? 2 : 3) != argc)
	{
		usage(argv[0]);
		return EX_USAGE;
	}
	
	char *mac_a  = argv[optind];
	char *host_a = argv[optind + (use_dns ? 0 : 1)];
	char *port_a = argv[optind + (use_dns ? 1 : 2)];
	
	if(wol_direct)
	{
		wol_host = host_a;
	}
	
	struct addrinfo *wol_ai;
	
	{
		int err = getaddrinfo(wol_host, wol_port, NULL, &wol_ai);
		if(err != 0)
		{
			fprintf(stderr, "%s: %s\n", wol_host, gai_strerror(err));
			return EX_UNAVAILABLE;
		}
	}
	
	if(use_dns)
	{
		int status = mac_from_dns(dest_mac, host_a);
		if(status != EX_OK)
		{
			return status;
		}
	}
	else if(!parse_mac(dest_mac, mac_a))
	{
		fprintf(stderr, "Invalid MAC address supplied\n");
		return EX_USAGE;
	}
	
	if(!valid_port(port_a))
	{
		fprintf(stderr, "Invalid host port: %s\n", port_a);
		return EX_USAGE;
	}
	
	struct addrinfo *test_ai;
	
	{
		int err = getaddrinfo(host_a, port_a, NULL, &test_ai);
		if(err != 0)
		{
			fprintf(stderr, "%s: %s\n", host_a, gai_strerror(err));
			return EX_UNAVAILABLE;
		}
	}
	
	int wol_socket = socket(wol_ai->ai_family, SOCK_DGRAM, 0);
	if(wol_socket == -1)
	{
		fprintf(stderr, "Could not create UDP socket: %s\n", strerror(errno));
		return EX_OSERR;
	}
	
	int bc_enable = 1;
	
	if(setsockopt(wol_socket, SOL_SOCKET, SO_BROADCAST, &bc_enable, sizeof(bc_enable)) == -1)
	{
		fprintf(stderr, "Could not enable broadcast on UDP socket: %s\n", strerror(errno));
		return EX_OSERR;
	}
	
	int tcp_socket = socket(test_ai->ai_family, SOCK_STREAM, 0);
	if(tcp_socket == -1)
	{
		fprintf(stderr, "Could not create TCP socket: %s\n", strerror(errno));
		return EX_OSERR;
	}
	
	time_t begin = time(NULL);
	int status = 2;
	
	unsigned char packet[WOL_PACKET_SIZE];
	build_wol_packet(packet, dest_mac);
	
	while(!timeout || begin + timeout > time(NULL))
	{
		if(sendto(wol_socket, packet, WOL_PACKET_SIZE, 0, wol_ai->ai_addr, wol_ai->ai_addrlen) == -1)
		{
			fprintf(stderr, "Could not send WOL packet: %s\n", strerror(errno));
			
			status = EX_TEMPFAIL;
			break;
		}
		
		if(connect(tcp_socket, test_ai->ai_addr, test_ai->ai_addrlen) == 0)
		{
			status = 0;
			break;
		}
		
		sleep(loop_wait);
	}
	
	close(tcp_socket);
	close(wol_socket);
	
	freeaddrinfo(test_ai);
	freeaddrinfo(wol_ai);
	
	return status;
}
