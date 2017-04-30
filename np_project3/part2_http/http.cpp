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
using namespace std;

#define MAX_LEN 32768

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
		confd = accept( listenfd, (struct sockaddr*)&serv_addr, (socklen_t*)&serlen );
		if ( confd < 0 ){
			printf("accept error\n");
			exit(1);
		}
		
		if ( (childpid = fork()) < 0 ){
			exit(1);
		}else if( childpid == 0 ){
            dup2(confd, STDIN_FILENO);
            dup2(confd, STDOUT_FILENO);
            close(confd);

			char tmp[MAX_LEN];
			char path[MAX_LEN];
			char queryStr[MAX_LEN];

            scanf( "%s", tmp );
            memset( tmp, 0, sizeof(tmp) );
            scanf( "%s", tmp );

            if( strlen( tmp ) > 0 ){
            	int pos = 0, ct = 0;
            	for ( pos = 1; tmp[pos] && tmp[pos] != '?'; ++pos ){
            		path[ct++] = tmp[pos];
            	}
            	if( tmp[pos] == '?' ) pos++;
            	ct = 0;
            	for ( ; tmp[pos]; ++pos ){
            		queryStr[ct++] = tmp[pos];
            	}
            }

            cout << "HTTP/1.1 200 OK" << endl;
            if( strstr( path, ".cgi") != NULL ){
                clearenv();
                setenv("QUERY_STRING", queryStr, 0);
                setenv("SCRIPT_NAME", path, 0);
                setenv("REQUEST_METHOD", "GET", 0);
                setenv("REMOTE_ADDR", "127.0.0.1", 0);
                setenv("REMOTE_HOST", "", 0);
                setenv("CONTENT_LENGTH", "", 0);
                setenv("AUTH_TYPE", "", 0);
                setenv("REMOTE_USER", "", 0);
                setenv("REMOTE_IDENT", "", 0);
                execl( path, path, NULL );
                exit(0);
            } else {
                cout << "Content-type: text/html" << endl << endl;
                ifstream f( path );
                string str;
                while(getline(f, str)){
                    cout << str << endl;
                }
            }
            exit(0);

		}else{
			close( confd );
		}
	}
	close( listenfd );
	return 0;
}