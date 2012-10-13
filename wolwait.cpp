/* wolwait.cpp - Start a WOL system and wait for it to boot
 * By Daniel Collins (2009)
 * Released to public domain
 *
 * System status is checked by attempting to connect to a TCP port, if the
 * connection succeeds the program exits with status 0, otherwise it will send
 * another WOL packet and retry the connection. If the timeout is reached
 * before a successful connection occurs, the program will exit with status 2.
 * Status 1 is returned upon error.
 *
 * This is only tested on Linux, but it will probably work fine on other UNIXes
 * like BSD.
 *
 * Compile with: g++ -o wolwait wolwait.cpp
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

using std::cout;
using std::cerr;
using std::endl;

#define call_fail(name, error) \
	cerr << name << ": " << strerror(error) << endl; \
	exit(1);

#define valid_port(port) (atoi(port) >= 0 && atoi(port) <= 65535)

#define close_socket(s) if(s >= 0) { close(s); }

static int wol_socket = -1;
static struct sockaddr_in wol_addr;

static void usage(char const *argv0) {
	cerr << "Usage: " << argv0 << " [options] <MAC address> <Hostname/IP> <Port>" << endl << endl;
	cerr << "-w <timeout>\tSpecify timeout in seconds (Default: 300)" << endl;
	cerr << "-f\t\tWait forever" << endl;
	cerr << "-l <max>\tLimit maximum number of WOL packets" << endl;
	cerr << "-A <address>\tSet broadcast address" << endl;
	cerr << "-P <port>\tSet broadcast port" << endl;
	cerr << "-d <delay>\tTime to wait between connection attempts (Default: 5)" << endl;
}

static void parse_mac(unsigned char *addr, char const *string) {
	for(int i = 0; i < 6; i++) {
		if(strchr(":-.", string[0]) && isxdigit(string[1])) {
			string++;
		}
		
		if(isxdigit(string[0]) && isxdigit(string[1])) {
			std::string byte(string, 0, 2);
			addr[i] = strtoul(byte.c_str(), NULL, 16);
			
			string += 2;
		}else{
			cerr << "Invalid MAC address supplied" << endl;
			exit(1);
		}
	}
	
	if(*addr) {
		cerr << "Invalid MAC address supplied" << endl;
		exit(1);
	}
}

static int init_socket(int domain, int type, int proto, int bcast) {
	int sock = socket(domain, type, proto);
	if(sock == -1) {
		call_fail("socket", errno);
	}
	
	if(setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &bcast, sizeof(bcast)) == -1) {
		call_fail("setsockopt", errno);
	}
	
	return sock;
}

static void send_wol_packet(unsigned char *wake_mac) {
	std::string packet(0xFF, 6);
	
	for(int i = 0; i < 16; i++) {
		for(int c = 0; c < 6; c++) {
			packet.push_back(wake_mac[c]);
		}
	}
	
	int r = sendto(
		wol_socket,
		packet.data(),
		packet.size(),
		0,
		(struct sockaddr*)&wol_addr,
		sizeof(wol_addr)
	);
	if(r == -1) {
		call_fail("sendto", errno);
	}
}

int main(int argc, char **argv) {
	int arg;
	
	int timeout = 300;
	int wol_limit = 0;
	unsigned char dest_mac[6];
	struct sockaddr_in host_addr;
	int loop_wait = 5;
	
	opterr = 0;
	wol_addr.sin_family = AF_INET;
	wol_addr.sin_addr.s_addr = INADDR_BROADCAST;
	wol_addr.sin_port = htons(9);
	host_addr.sin_family = AF_INET;
	
	while((arg = getopt(argc, argv, "w:fl:A:P:d:")) != -1) {
		if(arg == '?') {
			usage(argv[0]);
			return 1;
		}
		if(arg == 'w') {
			timeout = atoi(optarg);
			
			if(timeout <= 0) {
				cerr << "Invalid timeout value" << endl;
				return 1;
			}
		}
		if(arg == 'f') {
			timeout = 0;
		}
		if(arg == 'l') {
			wol_limit = atoi(optarg);
			
			if(wol_limit <= 0) {
				cerr << "Invalid WOL packet limit" << endl;
				return 1;
			}
		}
		if(arg == 'A') {
			if(!inet_pton(AF_INET, optarg, &wol_addr.sin_addr)) {
				cerr << "Invalid broadcast address" << endl;
				return 1;
			}
		}
		if(arg == 'P') {
			if(!valid_port(optarg)) {
				cerr << "Invalid broadcast port" << endl;
				return 1;
			}
			
			wol_addr.sin_port = htons(atoi(optarg));
		}
		if(arg == 'd') {
			loop_wait = atoi(optarg);
			
			if(loop_wait <= 0) {
				cerr << "Invalid reconnect delay" << endl;
				return 1;
			}
		}
	}
	
	if(optind + 3 > argc) {
		usage(argv[0]);
		return 1;
	}
	
	if(!valid_port(argv[optind+2])) {
		cerr << "Invalid host port" << endl;
		return 1;
	}
	
	struct hostent *he = gethostbyname(argv[optind+1]);
	if(!he) {
		cerr << "Unable to resolve host: " << hstrerror(h_errno) << endl;
		return 1;
	}
	
	parse_mac(dest_mac, argv[optind]);
	host_addr.sin_addr.s_addr = *(uint32_t*)he->h_addr;
	host_addr.sin_port = htons(atoi(argv[optind+2]));
	
	wol_socket = init_socket(PF_INET, SOCK_DGRAM, 0, 1);
	int tcp_socket = init_socket(PF_INET, SOCK_STREAM, 0, 0);
	
	time_t begin = time(NULL);
	int wol_packets = 0;
	int status = 2;
	
	while(!timeout || begin+timeout > time(NULL)) {
		if(!wol_limit || wol_packets++ < wol_limit) {
			send_wol_packet(dest_mac);
		}
		
		if(connect(tcp_socket, (struct sockaddr*)&host_addr, sizeof(host_addr)) == 0) {
			status = 0;
			break;
		}
		
		sleep(loop_wait);
	}
	
	close_socket(wol_socket);
	close_socket(tcp_socket);
	
	return status;
}
