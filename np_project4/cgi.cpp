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
#include <arpa/inet.h>
#include <sys/select.h>
#include <assert.h>
#include <netdb.h>
#include <sys/stat.h>
#include <fcntl.h>
using namespace std;

#define STATUS_CONNECTING 0
#define STATUS_READING 1
#define STATUS_WRITING 2
#define STATUS_DONE 3
#define PROXY_CONNECTING 4
#define PROXY_WRITING 5
#define MAX_LEN 32768

char h[6][50];
char p[6][32];
char f[6][50];
char sh[6][50];
char sp[6][50];

int vis[6];
int buffer_sented[6];
char sent_buffer[6][MAX_LEN];
char* queryStr = NULL;

int printHead(){

	printf("Content-type: text/html\n");
	printf("\n");

	printf("<html>\n");
	printf("<head>\n");
	printf("<meta http-equiv=\"Content-Type\" content=\"text/html; charset=big5\" />\n");
	printf("<title>Network Programming Homework 4</title>\n");
	printf("<body bgcolor=#336699>\n");
	printf("<font face=\"Courier New\" size=2 color=#FFFF99>\n");
	printf("<table width=\"800\" border=\"1\">\n");
	fflush(stdout);
	return 1;
}


int printTail(){
	printf("</font>\n");	
	printf("</body>\n");	
	printf("</html>\n");	
	return 1;
}


int myPrint( char* str, int id, int bd ){

	printf( "<script>document.all[\'m%d\'].innerHTML += \"", id-1 );
	if ( bd ) printf("<b>");
	for ( int i = 0; i < strlen(str); ++i ){
		if ( str[i] == '"' ){
			printf( "&quot;" );
		}else if( str[i] == '&' ){
			printf( "&amp;" );
		}else if( str[i] == '<' ){
			printf( "&lt;" );
		}else if( str[i] == '>' ){
			printf( "&gt;" );
		}else if( str[i] == '\n' ){
			printf( "<br>" );
		}else if( str[i] == ' ' ){
			printf( "&nbsp;" );
		}else if(str[i] == '\r'){
			continue;
		}else{
			printf("%c", str[i] );
		}
	}

	if ( bd ) printf("</b>");
	printf( "\";</script>\n" );
	fflush(stdout);
	return 1;
}

int paserStr( char* str ){
	
	memset( h, 0, sizeof(h) );
	memset( p, 0, sizeof(p) );
	memset( f, 0, sizeof(f) );
	memset( sp, 0, sizeof(sp) );
	memset( sh, 0, sizeof(sh) );
	memset( vis, 0, sizeof(vis) );

	for ( int i = 0; i < strlen(str); ++i ){
		if ( str[i] == 'h' ){
			int index = str[i+1] - '0';
			if ( index >= 0 && index <= 5 ){
				i += 3;
				int pos = 0;
				while( str[i+pos] && str[i+pos] != '&' ){
					h[index][pos] = str[i+pos];
					pos++;
					vis[index] = 1;
				}
			}
		}else if ( str[i] == 'p' ){
			int index = str[i+1] - '0';
			if ( index >= 0 && index <= 5 ){
				i += 3;
				int pos = 0;
				while( str[i+pos] && str[i+pos] != '&' ){
					p[index][pos] = str[i+pos];
					pos++;
				}
			}
		}else if ( str[i] == 'f' ){
			int index = str[i+1] - '0';
			if ( index >= 0 && index <= 5 ){
				i += 3;
				int pos = 0;
				while( str[i+pos] && str[i+pos] != '&' ){
					f[index][pos] = str[i+pos];
					pos++;
				}
			}
		}else if ( str[i] == 's' && str[i+1] == 'h' ){
			++i;
			int index = str[i+1] - '0';
			if ( index >= 0 && index <= 5 ){
				i += 3;
				int pos = 0;
				while( str[i+pos] && str[i+pos] != '&' ){
					sh[index][pos] = str[i+pos];
					pos++;
				}
			}
		}else if ( str[i] == 's' && str[i+1] == 'p' ){
			++i;
			int index = str[i+1] - '0';
			if ( index >= 0 && index <= 5 ){
				i += 3;
				int pos = 0;
				while( str[i+pos] && str[i+pos] != '&' ){
					sp[index][pos] = str[i+pos];
					pos++;
				}
			}
		}
	}
	return 1;
}

int getPar(){

	queryStr = getenv( "QUERY_STRING" );
	if ( queryStr == NULL ){
		printf( "fuck error\n" );
	}

	paserStr( queryStr );

	printf( "<tr>\n" );
	for ( int i = 1; i <= 5; ++i ){
		if ( vis[i] ){
			printf("<td>%s</td>", h[i] );
		}
	}
	printf( "</tr>\n" );

	printf( "<tr>\n" );
	for ( int i = 1; i <= 5; ++i ){
		if ( vis[i] ){
			printf("<td valign=\"top\" id=\"m%d\"></td>", i-1 );
		}
	}
	printf( "</tr>\n" );

	//talbe
	printf( "</table>\n" );
	fflush(stdout);
	return 1;
}

int work(){

	fd_set rfds, wfds, rs, ws;

	int cfd[6];
	int cstatus[6];
	struct sockaddr_in cli_addr[6];
	FILE *filefd[6];
	int cnt = 0;
	FD_ZERO(&rfds); FD_ZERO(&wfds); FD_ZERO(&rs); FD_ZERO(&ws);

	// connect to server
	for ( int i = 1; i <= 5; ++i ){
		if ( vis[i] ){
			// create socket
			cfd[i] = socket( AF_INET, SOCK_STREAM, 0 );
			memset( &cli_addr[i], 0, sizeof(cli_addr[i]) );
			cli_addr[i].sin_family = AF_INET;
			cli_addr[i].sin_addr = *((in_addr*)(gethostbyname( sh[i] )->h_addr));
			cli_addr[i].sin_port = htons( atoi(sp[i]) );

			// non-blocking 
			int flags = fcntl(cfd[i], F_GETFL, 0);
			fcntl(cfd[i], F_SETFL, flags | O_NONBLOCK);

			// connect to server
			connect( cfd[i], (struct sockaddr *)&cli_addr[i], sizeof(cli_addr[i]));
			cnt++;
			//FD_SET( cfd[i], &rs );
			FD_SET( cfd[i], &ws );
			cstatus[i] = PROXY_CONNECTING;
			filefd[i] = fopen( f[i], "r" );
		}else{
			cstatus[i] = STATUS_DONE;
		}
	}
	// comunication with server

	int nfds = FD_SETSIZE;

	while( cnt > 0 ){
		memcpy( &rfds, &rs, sizeof(rfds) );
		memcpy( &wfds, &ws, sizeof(wfds) );

		if ( select( nfds, &rfds, &wfds, (fd_set*)0, (struct timeval*)0 ) < 0 ){
			printf( "select() fuck error\n" );
			exit(1);
		}

		for (int i = 1; i <= 5; ++i){

			if ( cstatus[i] == STATUS_CONNECTING && FD_ISSET( cfd[i], &rfds ) ){
				int optVal;  
				socklen_t optLen = sizeof(int);  
				if( getsockopt( cfd[i], SOL_SOCKET, SO_ERROR, (char*)&optVal, &optLen) < 0 || optVal != 0 ){
					cstatus[i] = STATUS_DONE;
					close( cfd[i] );
					FD_CLR( cfd[i], &rs );
					FD_CLR( cfd[i], &ws );
					cnt--;
				}else{
					cstatus[i] = STATUS_READING;
				}
			}else if ( cstatus[i] == STATUS_READING && FD_ISSET( cfd[i], &rfds ) ){
				char buffer[MAX_LEN] = {};
				int n = read( cfd[i], buffer, MAX_LEN );
				if(n == 0){
					FD_CLR( cfd[i], &rs );
					FD_CLR( cfd[i], &ws );
					cstatus[i] = STATUS_DONE;
					cnt--;
					continue;
				} else {

					myPrint( buffer, i, 0 );
					for(int j = 0 ; j < n ; j++){
						if(buffer[j] == '%'){
							cstatus[i] = STATUS_WRITING;
							FD_SET(cfd[i], &ws);
							FD_CLR(cfd[i], &rs);
						}
					}
				}
			}else if ( cstatus[i] == STATUS_WRITING && FD_ISSET( cfd[i], &wfds ) ){

				if( buffer_sented[i] == strlen(sent_buffer[i]) ){
					fgets(sent_buffer[i], MAX_LEN, filefd[i]);
					myPrint( sent_buffer[i], i, 1 );
					buffer_sented[i] = 0;
				}

				int n = write( cfd[i], sent_buffer[i]+buffer_sented[i], (int)strlen(sent_buffer[i])-buffer_sented[i] );
				buffer_sented[i] += n;
				if( buffer_sented[i] == (int)strlen(sent_buffer[i]) ){
					cstatus[i] = STATUS_READING;
					FD_SET( cfd[i], &rs );
					FD_CLR( cfd[i], &ws );
				}
			}else if ( cstatus[i] == PROXY_CONNECTING && FD_ISSET( cfd[i], &wfds ) ){
				int optVal;  
				socklen_t optLen = sizeof(int);  
				if( getsockopt( cfd[i], SOL_SOCKET, SO_ERROR, (char*)&optVal, &optLen) < 0 || optVal != 0 ){
					cstatus[i] = STATUS_DONE;
					close( cfd[i] );
					FD_CLR( cfd[i], &rs );
					FD_CLR( cfd[i], &ws );
					cnt--;
				}else {
					printf( "connect server.\n" );
					cstatus[i] = PROXY_WRITING;
					buffer_sented[i] = 0;
					sent_buffer[i][0] = 0x04;
					sent_buffer[i][1] = 0x01;
					sent_buffer[i][2] = atoi(p[i]) / 256;
					sent_buffer[i][3] = atoi(p[i]) % 256;
					sent_buffer[i][8] = 0;
					int ip[4];
					sscanf(inet_ntoa(*((in_addr*)gethostbyname(h[i])->h_addr)), "%d.%d.%d.%d", &ip[0], &ip[1], &ip[2], &ip[3]);
					sent_buffer[i][4] = ip[0];
					sent_buffer[i][5] = ip[1];
					sent_buffer[i][6] = ip[2];
					sent_buffer[i][7] = ip[3];
				}
			}else if ( cstatus[i] == PROXY_WRITING && FD_ISSET( cfd[i], &wfds ) ) {

				int n = write( cfd[i], sent_buffer[i]+buffer_sented[i], (int)strlen(sent_buffer[i])- buffer_sented[i]);
				buffer_sented[i] += n;
				if( buffer_sented[i] == (int)strlen(sent_buffer[i]) ){
					cstatus[i] = STATUS_READING;
					FD_SET(cfd[i], &rs);
					FD_CLR(cfd[i], &ws);
				}
			}
		}

	}
	
}
	

int main(){
	printHead();
	getPar();
	work();
	printTail();
	return 0;
}
