#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <unistd.h>
#include <algorithm>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <vector>
#include <fcntl.h>
using namespace std;

#define MAX_USER_NUM 30
#define MAX_LINE_NUM 15005
#define MAX_CMD_NUM  256 
const char* originalPath = "PATH=bin:.";
const char* welcomeText =   "****************************************\n** Welcome to the information server. **\n****************************************\n";
const char* headText = "% ";

// pipe information in & out & pipe count
struct pipeInfo{
	int fdIn;
	int fdOut;
	int cnt;
};

// pipe list : used to save pipe
vector<pipeInfo> pipeList;

int readLine( int fd, char* ptr );
int isPipeExist( int number, int& index );
int checkPipeNum( char* line );
void setInPipe();
void saveFds( int *stdfd );
void restoreFds( int *stdfd );
int parseCmds( char* line, char** cmds );
int cmd2argv( char* cmd, char** parameter, int &fileflag, char* filename  );
void executeLine( char** cmds, int cmdNum, int &exitFlag );
void cutPipeNum();
int checkLine( char* line );
void clearCmds( int cmdNum, char **cmds );
void initPath();
void changeSockfd( int sockfd );
void closePipe();
void runRAS( int sockfd );

// read one line from socket( client )
int readLine( int fd, char* ptr ){
	int rc, n;
	char c;
	for (n = 0; n < MAX_LINE_NUM; ++n ){
		if ( rc = read( fd, &c, sizeof(c) ) == 1 ) {
			*ptr++ = c;
			if( c == '\n' ) break;
		}else if( rc == 0 ){
			if( n == 1 ) return 0;
			else break;
		}else{
			return -1;
		}
	}
	*ptr = 0;
	return n;
}

// check if a pipe is in the vector or not
int isPipeExist( int number, int& index ){
	for (int i = 0; i < pipeList.size(); ++i){
		if( pipeList[i].cnt == number ){
			index = i;
			return 1;
		}
	}
	return 0;
}

// check if there is a "|2" or "!2" in read line
// then set output pipe
int checkPipeNum( char* line ){
	int number = 0;
	int index = 0;
	int pipeTemp[2];
	bool flag = false;
	for (int i = 0; line[i]; ++i){
		if ( ( line[i] == '|' || line[i] == '!' ) && ( line[i+1] > '0' && line[i+1] <= '9' ) ){
			if ( line[i] == '!' ){
				flag = true;
			}
			line[i] = '\0';
			i++;
			while( isdigit(line[i]) ){
				number = number*10 + line[i] - '0';
				++i;
			}

			if ( number && !isPipeExist( number, index ) ){
				if( pipe( pipeTemp ) < 0 ){
					fprintf( stderr, "ERROR: pipe error" );
					return 0;
				}else{
					pipeInfo pi;
					pi.fdIn = pipeTemp[0];
					pi.fdOut = pipeTemp[1];
					pi.cnt = number;
					pipeList.push_back(pi);
				}
				dup2( pipeTemp[1], 1 );
				if ( flag ){
					dup2( pipeTemp[1], 2 );
				}
				break;
			}
			dup2( pipeList[index].fdOut, 1 );
			if ( flag ){
				dup2( pipeList[index].fdOut, 2 );
			}
			break;
		}
	}
	return 1;
}

// set input pipe for each read line
void setInPipe(){
	vector<pipeInfo>::iterator it = pipeList.begin();
	for (  ; it != pipeList.end(); ++it ){
		if ( it->cnt == 0 ){
			dup2( it->fdIn, 0 );
			close( it->fdOut );
			close( it->fdIn );
			pipeList.erase(it);
			break;
		}
	}
}

// save original value of file descriptor
void saveFds( int *stdfd ){
    stdfd[0] = dup( 0 );
    stdfd[1] = dup( 1 );
    stdfd[2] = dup( 2 );
}

// restore original value of file descriptor
void restoreFds( int *stdfd ){
    dup2( stdfd[0], 0 );
    close( stdfd[0] );
    dup2( stdfd[1], 1 );
    close( stdfd[1] );
    dup2( stdfd[2], 2 );
    close( stdfd[2] );
}

// split line -> cmds
int parseCmds( char* line, char** cmds ){
	int cmdNum = 0;
	char* pch = strtok( line,"|\r\n" );
	while( pch != NULL ){
        cmds[cmdNum] = ( char* )malloc( strlen( pch ) + 1 );
		strcpy( cmds[cmdNum++], pch );
		pch = strtok( NULL, "|\r\n" );
	}
	return cmdNum;
}

// split cmd -> parameters
int cmd2argv( char* cmd, char** parameter, int &fileflag, char* filename  ){	
	int parNum = 0;
	char* pch = strtok( cmd," \r\n" );
	while( pch != NULL ){
		if( strcmp( pch, ">" ) == 0 ) {
            pch = strtok( NULL, " \r\n" );
            strcpy( filename, pch );
            fileflag = 1;
        }else{
        	parameter[parNum] =( char* )malloc( strlen( pch ) + 1 );
			strcpy( parameter[parNum++], pch );
        } 
		pch = strtok( NULL, " \r\n" );
	}
	return parNum;
}

// execute one line 
void executeLine( char** cmds, int cmdNum, int &exitFlag ){

	int fileflag = 0;
    int pipeTemp[2];
    int pipeStd[3];
    int childpid;
	char* parameter[MAX_CMD_NUM+1];
    char filename[MAX_CMD_NUM] = {0};

 	saveFds( pipeStd );

	for (int i = 0; i < cmdNum; ++i){

        if( pipe( pipeTemp ) < 0 ) {
            fprintf( stderr, "ERROR: create pipes error\n" );
            exitFlag = 0;
            return;
        }

		int paraNum = cmd2argv( cmds[i], parameter, fileflag, filename );
		if( strcmp( parameter[0], "printenv") == 0 ){
			char out[MAX_LINE_NUM];
			strcpy( out, "PATH=" );
			strcat( out, getenv("PATH") );
			strcat( out, "\n" );
			write( 1, out, strlen(out) );
		}else if( strcmp( parameter[0], "setenv") == 0 ){
			 if( paraNum != 3 ) {
                fprintf( stderr, "Usage: setenv <name> <value>\n" );
            }else{
                if( setenv( parameter[1], parameter[2], 1 ) < 0 )
                    fprintf( stderr, "ERROR: setenv() error\n" );
            }
		}else if( strcmp( parameter[0], "exit") == 0 ){
			exitFlag = 0;
		}else{
            if( ( childpid = fork() ) < 0 ) {
                fprintf( stderr , "ERROR: fork error\n" );
            } else if( childpid == 0 ) {	// child process
        	    if( i != cmdNum - 1 ) {
			        dup2( pipeTemp[1], 1 );
			    } else {
			        dup2( pipeStd[1], 1 );
			    }
			    close( pipeTemp[0] );
			    close( pipeTemp[1] );
                if( fileflag ){
                	//write( 1, filename, strlen(filename) );
        	        int filefd = open( filename, O_RDWR | O_CREAT | O_TRUNC, S_IRWXU | S_IRWXG | S_IRWXO );
			        if( filefd < 0 ) {
			            fprintf( stderr, "ERROR: cannot open file %s\n", filename );
			            break;
			        }
			        dup2( filefd, 1 );
			        close( filefd );
                }
                execvp( parameter[0], parameter );
                exitFlag = 0;
                fprintf( stderr, "Unknown command: [%s].\n", parameter[0] );
                break;
            } else { // parent process
                if( i != cmdNum - 1 ) {
			        dup2( pipeTemp[0], 0 );
			    } else {
			        dup2( pipeStd[0], 0 );
			    }
			    close( pipeTemp[0] );
			    close( pipeTemp[1] );
                int status = 0;
                while( waitpid( childpid, &status, 0 ) > 0 );
                if( status != 0 ){
                	break;
                }
            }

		}
		while( --paraNum >= 0 ) {
        	free( parameter[paraNum] );
        	parameter[paraNum] = NULL;
    	}
	}
	restoreFds( pipeStd );
}

// curdown pipe's number
void cutPipeNum(){
	for ( int i = 0; i < pipeList.size(); ++i ){
		(pipeList[i].cnt)--;
	}
}

// check if a line is vaild or not
int checkLine( char* line ){
	for ( int i = 0; line[i]; ++i ){
		if( ( line[i] == '|' || line[i] == '!' ) && !isdigit( line[i+1] ) && !isspace( line[i+1] ) ){
			return 0;
		}else if( line[i] == '\r' || line[i] == '\n' ){
            line[i]   = '\0';
            line[i+1] = '\0';
            return 1;
        }
	}
	return 1;
}

// recycle memory
void clearCmds( int cmdNum, char **cmds ){
    while( --cmdNum >= 0 ) {
        free( cmds[cmdNum] );
        cmds[cmdNum] = NULL;
    }
}

// initialize path
void initPath(){     
    clearenv();
    putenv( ( char* )originalPath );
}
// change stdin, stdout & stderr -> sockfd
void changeSockfd( int sockfd ){
	dup2( sockfd, 0 );
	dup2( sockfd, 1 );
	dup2( sockfd, 2 );
}

// close pipe if one line is ivaild
void closePipe(){
	vector<pipeInfo>::iterator it = pipeList.begin();
	for (  ; it != pipeList.end(); ++it ){
		if ( it->cnt == 0 ){
			close( it->fdOut );
			close( it->fdIn );
			pipeList.erase(it);
			return;
		}
	}	
}

// run shell
void runRAS( int sockfd ){

	char line[MAX_LINE_NUM];
	char* cmds[MAX_LINE_NUM];
	int stdfd[3];
	int exitFlag = 1;
	changeSockfd( sockfd );

	initPath();
	// print welcomeText
	write( 1, welcomeText, strlen(welcomeText) );

	while( exitFlag ){
		// print headText

		write( 1, headText, strlen(headText) );

		// read one line
		if( readLine( 0, line ) == -1 ){
			fprintf( stderr, "ERROR: readline error\n" );
			return;
		}
		if( checkLine( line ) == 0 ){
			fprintf( stderr, "ERROR: readline is illegal\n" );
			closePipe();
		}else{

			saveFds( stdfd );

			exitFlag = checkPipeNum( line );

			setInPipe();

			int cmdNum = parseCmds( line, cmds );

			executeLine( cmds, cmdNum, exitFlag );

	        restoreFds( stdfd );

			clearCmds( cmdNum, cmds );
			
			cutPipeNum();	
		}
	}

}

int main( int argc, char const *argv[] )
{
    int                    listenfd;
    int                    connfd;
    int                    childpid;
    socklen_t              clilen;
    struct sockaddr_in     servaddr;
    struct sockaddr_in     childaddr;

	if( chdir("./ras") != 0 ){
		printf( "ERROR: chdir failed" );
		exit(0);
	}

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
    memset( &servaddr, 0, sizeof( servaddr ) );
    servaddr.sin_family      = AF_INET;
    servaddr.sin_addr.s_addr = htonl( INADDR_ANY );
    servaddr.sin_port        = htons( atoi( argv[1] ) );

    // Bind loacl address
    if( bind( listenfd,( struct sockaddr* )&servaddr, sizeof( servaddr ) ) < 0 ) {
        perror( "ERROR: bind socket error" );
        exit( -1 );
    }

    if( listen( listenfd, MAX_USER_NUM ) < 0 ){
        perror( "ERROR: listen socket error" );
        exit( -1 );
    }
    // server running
    printf( "========== Server is running ==========\n" );
    while( true ){
        clilen = sizeof( childaddr );
        connfd = accept( listenfd,( struct sockaddr* )&childaddr, &clilen );
        if( connfd < 0 ){
            perror( "accept socket error" );
        }
        if( ( childpid = fork() ) < 0 ) {
            perror( "ERROR: fork error" );
        }else if( childpid == 0 ){
            close( listenfd );
            runRAS( connfd );
            exit( 0 );
        }else{
            close( connfd );
        }
    }

    close( listenfd );
    exit( 0 );
}