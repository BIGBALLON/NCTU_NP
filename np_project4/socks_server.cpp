#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <assert.h>
#include <netdb.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <fstream>
#include <stack>
#include <queue>
#include <algorithm>
#include <vector>
using namespace std;

#define UC unsigned char 
#define UI unsigned int 
#define CONNECT 0x01
#define BIND 0x02
#define MAX_LEN 32768

void work( int confd, sockaddr_in cli_addr );
void transData( int confd, int refd );
void printInfo();
bool checkFirewall( int CD, int SRC_IP, UC &ok );
void printIP( int SRC_IP );

int main(int argc, char const *argv[]){

	int listenfd, confd;
	int clilen, serlen;
	int childpid;
	struct sockaddr_in serv_addr;
	struct sockaddr_in cli_addr;

	if( argc != 2 ){
		printf( "Usage: %s <PORT>\n", argv[0] );
		exit( -1 );
	}

	// Open a TCP socket( an Internet stream socket )
	if( ( listenfd = socket( AF_INET, SOCK_STREAM, 0 ) ) < 0 ){
		perror( "ERROR: create socket error" );
		exit( -1 );
	}

	// Initialize
	memset( &serv_addr, 0, sizeof( serv_addr ) );
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl( INADDR_ANY );
	serv_addr.sin_port = htons( atoi( argv[1] ) );


	int opt = 1;
	setsockopt( listenfd, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt));

	// Bind loacl address
	if( bind( listenfd,( struct sockaddr* )&serv_addr, sizeof( serv_addr ) ) < 0 ) {
		perror( "ERROR: bind socket error" );
		exit( -1 );
	}

	if( listen( listenfd, 5 ) < 0 ){
		perror( "ERROR: listen socket error" );
		exit( -1 );
	}
	serlen = sizeof(serv_addr);
	while( 1 ){
		clilen = sizeof(cli_addr);
		confd = accept( listenfd, (struct sockaddr*)&cli_addr, (socklen_t*)&clilen );
		if ( confd < 0 ){
			printf("ERROR: accept error\n");
			exit(1);
		}
		
		if ( (childpid = fork()) < 0 ){
			exit(1);
		}else if( childpid == 0 ){
			
			work( confd, cli_addr );
			exit(0);

		}else{
			close( confd );
		}
	}
	close( listenfd );
	return 0;
}


void work( int confd, sockaddr_in cli_addr ){

	printf("=============\n");
	UC buf[ 512 ];
	read( confd, buf, sizeof(buf) );

	UC VN, CD;
	UI DST_PORT, DST_IP, SRC_PORT, SRC_IP;
	UC* USERID = buf + 8;
	VN = buf[0];
	CD = buf[1];
	DST_PORT = (UC)(buf[2]) << 8  | (UC)(buf[3]);
	DST_IP  =  (UC)(buf[7]) << 24 | (UC)(buf[6]) << 16 | (UC)(buf[5]) << 8  | (UC)(buf[4]);

	socklen_t client_len;
	struct sockaddr_in client_addr;
	client_len = sizeof(client_addr);  
	getpeername(confd, (struct sockaddr*)&client_addr, &client_len);
	SRC_PORT = ntohs(client_addr.sin_port);
	SRC_IP = ntohl(client_addr.sin_addr.s_addr);

	printf("VN: %d, CD: %d, " ,VN,CD);
	printf("DST IP : %d.%d.%d.%d, ", (DST_IP >> 24) & 255, (DST_IP >> 16) & 255, (DST_IP >> 8) & 255, (DST_IP & 255));
	printf("DST PORT: %d, ", DST_PORT);
	printf("USERID: %s\n", USERID);
	
	fflush(stdout);
	if( VN != 0x04 ){
		printf("ERROR: VN error\n");
		return;
	}

	buf[0] = 0;
	buf[1] = 0x5A;
	
	if( checkFirewall( CD, SRC_IP, buf[1] ) == false ){
		UC reply[8];
		reply[0] = VN;
		reply[1] = 0x5B;
		reply[2] = DST_PORT / 256;
		reply[3] = DST_PORT % 256;
		reply[4] = (DST_IP >> 24) & 0xFF;
		reply[5] = (DST_IP >> 16) & 0xFF;
		reply[6] = (DST_IP >> 8)  & 0xFF;
		reply[7] = DST_IP & 0xFF;
		write( confd, reply, 8 );
		printf("Deny Src = %d.%d.%d.%d(%d), Dst = %d.%d.%d.%d(%d)\n",
				(SRC_IP >> 24 ) & 0xFF, (SRC_IP >> 16) & 0xFF, (SRC_IP >> 8) & 0xFF, SRC_IP & 0xFF, SRC_PORT, 
				(DST_IP >> 24) & 0xFF, (DST_IP >> 16) & 0xFF, (DST_IP >> 8) & 0xFF, DST_IP & 0xFF, DST_PORT);
		printf("Reject\n");
		fflush(stdout);
		exit(0);
	}

	int refd;
	if( CD == CONNECT ){
		printf("Permit Src =  %d.%d.%d.%d(%d), Dst = %d.%d.%d.%d(%d)\n", 
				  (SRC_IP >> 24) & 0xFF, (SRC_IP >> 16) & 0xFF, (SRC_IP >> 8) & 0xFF, SRC_IP & 0xFF, SRC_PORT, 
				  (DST_IP >> 24) & 0xFF, (DST_IP >> 16) & 0xFF, (DST_IP >> 8) & 0xFF, DST_IP & 0xFF, DST_PORT);
		refd = socket( AF_INET, SOCK_STREAM, 0 );
		sockaddr_in remote;
		remote.sin_family = AF_INET;
		remote.sin_addr.s_addr = DST_IP;
		remote.sin_port = htons(DST_PORT);

		if( connect( refd, (struct sockaddr*)&remote, sizeof(remote) ) < 0 ){
			printf("ERROR: connect error\n");
			return;
		}

		buf[0] = 0;
		buf[1] = 0x5A;
		buf[2] = DST_PORT / 256;
		buf[3] = DST_PORT % 256;
		buf[4] = (DST_IP >> 24) & 0xFF;
		buf[5] = (DST_IP >> 16) & 0xFF;
		buf[6] = (DST_IP >> 8)  & 0xFF;
		buf[7] = DST_IP & 0xFF;
		write( confd, buf, 8 );


		printf("SOCKS_CONNECT GRANTED ....\n");
		printf("Accept\n");
		fflush(stdout);
		transData( confd, refd );
		close(confd);
		close(refd);

	}else if( CD == BIND ){
		int bindfd = socket(AF_INET, SOCK_STREAM, 0);

		sockaddr_in remote;
		sockaddr_in bind_add;
		bind_add.sin_family = AF_INET;
		bind_add.sin_addr.s_addr = htonl(INADDR_ANY);
		bind_add.sin_port = htons(INADDR_ANY);

		int opt = 1;
		if( setsockopt(bindfd, SOL_SOCKET, SO_REUSEPORT, (char *)&opt, sizeof(opt)) < 0){
			printf("ERROR: set sock error\n");
		}

		if( bind(bindfd, (sockaddr*)&bind_add, sizeof(bind_add)) < 0 ){
			printf("ERROR: Bind Failed.\n");
		}	
		sockaddr_in get_port;
		int get_port_len = sizeof(get_port);
		if( getsockname(bindfd, (sockaddr*)&get_port, (socklen_t*)&get_port_len) < 0 ){
			printf("ERROR: Get error\n");
		}
		if( listen( bindfd, 5 ) < 0 ){
			printf("ERROR: Listen error\n");
		}
		buf[2] = (UC)(ntohs(get_port.sin_port)/256);
		buf[3] = (UC)(ntohs(get_port.sin_port)%256);
		buf[4] = 0;
		buf[5] = 0;
		buf[6] = 0;
		buf[7] = 0;
		write(confd, buf, 8);
		int remote_len = sizeof(remote);
		refd = accept(bindfd, (sockaddr*)&remote, (socklen_t*)&remote_len);
		write(confd, buf, 8);
		printf("Permit Src =  %d.%d.%d.%d(%d), Dst = %d.%d.%d.%d(%d)\n", 
		  (SRC_IP >> 24) & 0xFF, (SRC_IP >> 16) & 0xFF, (SRC_IP >> 8) & 0xFF, SRC_IP & 0xFF, SRC_PORT, 
		  (DST_IP >> 24) & 0xFF, (DST_IP >> 16) & 0xFF, (DST_IP >> 8) & 0xFF, DST_IP & 0xFF, DST_PORT);
		printf("SOCKS_BIND GRANTED ....\n");
		printf("Accept\n");
		fflush(stdout);


		transData( confd, refd );
		close(confd);
		close(refd);
		
	}else{
		exit(0);
	}
}

void transData( int confd, int refd ){
	
	fd_set rfds, rs;
	FD_ZERO( &rs );
	FD_SET( refd, &rs );
	FD_SET( confd, &rs );
	char buffer[MAX_LEN];
	int nfds = max( refd, confd ) + 1;

	while( true ){
		rfds = rs;
		select( nfds, &rfds, NULL, NULL,(struct timeval*)0 );

		if( FD_ISSET( refd, &rfds ) ){
			int n = read( refd, buffer, sizeof( buffer ) );
			if( n == 0 || n == -1 ){
				exit(0);
			}else{
				n = write( confd, buffer, n );
			}
		}

		if( FD_ISSET( confd, &rfds ) ){
			int n = read( confd, buffer, sizeof( buffer ) );
			if( n == 0 || n == -1 ){
				exit(0);
			}else{
				n = write( refd, buffer, n );
			}
		}
	}
}


bool checkFirewall( int CD, int SRC_IP, UC &ok ){

	vector<int> v1, v2, v3, v4;
	v1.clear();
	v2.clear();
	v3.clear();
	v4.clear();
	FILE* fp = fopen( "socks.conf", "r" );
	if( fp == NULL ){
		ok = 0x5A;
		return true;
	}
	int x1, x2, x3, x4;
	char str[128];
	char star = '*';
	while( fgets( str, 100, fp ) != NULL ){
		sscanf( str, "%d.%d.%d.%d", &x1, &x2, &x3, &x4 );
		if( x1  ) v1.push_back(x1);
		if( x2  ) v2.push_back(x2);
		if( x3  ) v3.push_back(x3);
		if( x4  ) v4.push_back(x4);
	}


	if( v1.size() != 0 ){
		if( std::find( v1.begin(), v1.end(), (SRC_IP >> 24 ) & 0xFF ) ==  v1.end() ){
			printIP( SRC_IP );
			ok = 0x5B;
			return false;
		}
	}
	if( v2.size() != 0 ){
		if( std::find( v2.begin(), v2.end(), (SRC_IP >> 16 ) & 0xFF ) ==  v2.end() ){
			printIP( SRC_IP );
			ok = 0x5B;
			return false;
		}
	}
	if( v3.size() != 0 ){
		if( std::find( v3.begin(), v3.end(), (SRC_IP >> 8 ) & 0xFF ) ==  v3.end() ){
			printIP( SRC_IP );
			ok = 0x5B;
			return false;
		}
	}
	if( v4.size() != 0  ){
		if( std::find( v4.begin(), v4.end(), SRC_IP & 0xFF ) ==  v4.end() ){
			printIP( SRC_IP );
			ok = 0x5B;
			return false;
		}
	}

	ok = 0x5A;
	return true;
}

void printIP( int SRC_IP ){
	printf("IP %d.%d.%d.%d is disabled\n", (SRC_IP >> 24) & 255, (SRC_IP >> 16) & 255, (SRC_IP >> 8) & 255, SRC_IP & 255 );
}