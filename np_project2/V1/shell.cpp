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
#define MAX_LINE_NUM 1025
#define MAX_CMD_NUM  256 
const char* originalPath = "PATH=bin:.";
const char* welcomeText =   "****************************************\n** Welcome to the information server. **\n****************************************\n";
const char* headText = "% ";

int msgPipe[31][31][2];
int sendList[31][31];

// pipe information in & out & pipe count
struct pipeInfo{
	int fdIn;
	int fdOut;
	int cnt;
};



struct userInfo
{
	int userFd;
	int userID;
	int port;
	char ip[20];
	char nickName[20];
	char path[50];
	int stdfd[3];
	vector<pipeInfo> pipeList;

};

userInfo userList[MAX_USER_NUM+2];
// pipe list : used to save pipe

void initUserList();
int readLine( int fd, char* ptr );
int isPipeExist( int number, int& index );
int checkPipeNum( char* line, int n );
void setInPipe( int n );
void saveFds( int *stdfd );
void restoreFds( int *stdfd );
int parseCmds( char* line, char** cmds );
int cmd2argv( char* cmd, char** parameter, int &fileflag, char* filename  );
void executeLine( char** cmds, int cmdNum, int &exitFlag, int n, char* line2 );
void cutPipeNum( int n );
int checkLine( char* line );
void clearCmds( int cmdNum, char **cmds );
void initPath( char* originalPath );
void changeSockfd( int sockfd );
void closePipe( int n );
int runRAS( int n, char* line );

void yell( char* out ){
	for (int i = 1; i <= MAX_USER_NUM; ++i){
		if ( userList[i].userID != -1 ){
			write( userList[i].userFd, out, strlen(out) );
		}
	}
}

void initUserList(){
	memset(userList,0,sizeof(userList));
	memset(msgPipe,0,sizeof(msgPipe));
	memset(sendList,0,sizeof(sendList));
	for (int i = 0; i < MAX_USER_NUM+5; ++i){
		userList[i].userID = -1;
		userList[i].userFd = -1;
	}
}

void initUser( int n ){
	userList[n].userID = -1;
	userList[n].userFd = -1;
	userList[n].pipeList.clear();
	for (int i = 1; i <= MAX_USER_NUM; ++i){
		if ( sendList[n][i] ){
			if ( msgPipe[n][i][0] ){
				msgPipe[n][i][0] = 0;
				close( msgPipe[n][i][0] );
			}

			if ( msgPipe[n][i][1] ){
				close( msgPipe[n][i][1] );
				msgPipe[n][i][1] = 0;
			}
			
			sendList[n][i] = 0;
		}
		if ( sendList[i][n] ){
			if ( msgPipe[i][n][0] ){
				close( msgPipe[i][n][0] );
				msgPipe[i][n][0] = 0;
			}

			if ( msgPipe[i][n][1] ){
				close( msgPipe[i][n][1] );
				msgPipe[i][n][1] = 0;
			}
			sendList[i][n] = 0;
		}
	}
}

void userLogin( int n ){
	char out[MAX_LINE_NUM];
	sprintf( out, "*** User '%s' entered from %s/%d. ***\n", userList[n].nickName, userList[n].ip, userList[n].port );
	yell( out );	
}

void userLogout( int n ){
	char out[MAX_LINE_NUM];
	sprintf( out, "*** User '%s' left. ***\n", userList[n].nickName );
	yell( out );	
}

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
int isPipeExist( int number, int& index, int n ){
	for (int i = 0; i < userList[n].pipeList.size(); ++i){
		if( userList[n].pipeList[i].cnt == number ){
			index = i;
			return 1;
		}
	}
	return 0;
}

// check if there is a "|2" or "!2" in read line
// then set output pipe
int checkPipeNum( char* line, int n ){
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

			if ( number && !isPipeExist( number, index, n ) ){
				if( pipe( pipeTemp ) < 0 ){
					fprintf( stderr, "ERROR: pipe error" );
					return 0;
				}else{
					pipeInfo pi;
					pi.fdIn = pipeTemp[0];
					pi.fdOut = pipeTemp[1];
					pi.cnt = number;
					userList[n].pipeList.push_back(pi);
				}
				dup2( pipeTemp[1], 1 );
				if ( flag ){
					dup2( pipeTemp[1], 2 );
				}
				break;
			}
			dup2( userList[n].pipeList[index].fdOut, 1 );
			if ( flag ){
				dup2( userList[n].pipeList[index].fdOut, 2 );
			}
			break;
		}
	}
	return 1;
}

// set input pipe for each read line
void setInPipe( int n ){
	vector<pipeInfo>::iterator it = userList[n].pipeList.begin();
	for (  ; it != userList[n].pipeList.end(); ++it ){
		if ( it->cnt == 0 ){
			dup2( it->fdIn, 0 );
			close( it->fdOut );
			close( it->fdIn );
			userList[n].pipeList.erase(it);
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
	if( strncmp( cmd, "yell", 4 ) == 0 ){
		parameter[0] =( char* )malloc( 5 );
		strcpy( parameter[0], "yell" );
		int i = 4;
		while( cmd[i] == ' ' || cmd[i] == '\t' ) i++;
		parameter[1] =( char* )malloc( strlen( cmd + i ) + 1 );
		strcpy( parameter[1], cmd + i );
		return 2;
	}else if( strncmp( cmd, "tell", 4 ) == 0 ){
		parameter[0] =( char* )malloc( 5 );
		strcpy( parameter[0], "tell" );
		int i = 4;
		while( cmd[i] == ' ' || cmd[i] == '\t' ) i++;
		int c = 0;
		while( cmd[i+c] != ' ' && cmd[i+c] != '\t' ) c++;
		parameter[1] =( char* )malloc( c + 1 );
		strncpy( parameter[1], cmd + i, c );
		i = i + c;
		while( cmd[i] == ' ' || cmd[i] == '\t' ) i++;
		parameter[2] =( char* )malloc( strlen( cmd + i ) + 1 );
		strcpy( parameter[2], cmd + i );
		return 3;
	}else{
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
}

// execute one line 
void executeLine( char** cmds, int cmdNum, int &exitFlag, int n, char* line2 ){

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
				if( setenv( parameter[1], parameter[2], 1 ) < 0 ){
					fprintf( stderr, "ERROR: setenv() error\n" );
				}
				char out[MAX_LINE_NUM];
				strcpy( out, "PATH=" );
				strcat( out, parameter[2] );
				strcpy( userList[n].path, out );
			}
		}else if( strcmp( parameter[0], "exit") == 0 ){
			exitFlag = 0;
		}else if( strcmp( parameter[0], "who") == 0 ){
			char out[MAX_LINE_NUM];
			char tmp[MAX_LINE_NUM];
			strcpy( out, "<ID>	<nickname>	<IP/port>	<indicate me>\n" );
			for ( int id = 1; id <= MAX_USER_NUM; ++id ){
				if ( userList[id].userID != -1 ){
					if( id == n ) sprintf( tmp, "%d	%s	%s/%d	<-me\n", userList[id].userID, userList[id].nickName, userList[id].ip, userList[id].port );
					else sprintf( tmp, "%d	%s	%s/%d\n", userList[id].userID, userList[id].nickName, userList[id].ip, userList[id].port );
					strcat( out, tmp );
				}
			}
			write( 1, out, strlen(out) );
		}else if( strcmp( parameter[0], "tell") == 0 ){
			if( paraNum != 3 ) {
				fprintf( stderr, "Usage: tell <sockd> <message>\n" );
			}else{
				char out[MAX_LINE_NUM];
				char tmp[MAX_LINE_NUM];
				int i = 0;
				int number = 0;
				while( isdigit(parameter[1][i]) ){
					number = number*10 + parameter[1][i] - '0';
					++i;
				}
				if ( userList[number].userID != number ){
					sprintf( out, "*** Error: user #%d does not exist yet. ***\n", number );
					write( userList[n].userFd, out, strlen(out) );
				}else{
					sprintf( tmp, "*** %s told you ***: ", userList[n].nickName );
					strcpy( out, tmp );
					strcat( out, parameter[2] );
					strcat( out, "\n" );
					write( userList[number].userFd, out, strlen(out) );
				}
			}
		}else if( strcmp( parameter[0], "yell") == 0 ){
			if( paraNum != 2 ) {
				fprintf( stderr, "Usage: yell <message>\n" );
			}else{
				char out[MAX_LINE_NUM];
				char tmp[MAX_LINE_NUM];
				sprintf( tmp, "*** %s yelled ***: ", userList[n].nickName );
				strcpy( out, tmp );
				strcat( out, parameter[1] );
				strcat( out, "\n" );
				yell( out );
			}
		}else if( strcmp( parameter[0], "name") == 0 ){
			if( paraNum != 2 ) {
				fprintf( stderr, "Usage: name <message>\n" );
			}else{
				char out[MAX_LINE_NUM];
				// check username is exists
				int ok = 1;
				for (int j = 1; j <= MAX_USER_NUM; ++j){
					if ( strcmp( userList[j].nickName, parameter[1] ) == 0 ){
						ok = 0;
						break;
					}
				}
				if( ok ){
					strcpy( userList[n].nickName, parameter[1] );
					sprintf( out, "*** User from %s/%d is named '%s'. ***\n", userList[n].ip, userList[n].port, userList[n].nickName );
					yell( out );
				}else{
					sprintf( out, "*** User '%s' already exists. ***\n", parameter[1] );
					write( userList[n].userFd, out, strlen(out) );
				}
			}
		}
		else{
			int isSend = 0;
			int isRecv = 0;
			int sendIndex = 0;
			int recvIndex = 0;
			int sendFlag = 0;
			if ( paraNum >= 2 ){
				if( parameter[1][0] == '<' && isdigit(parameter[1][1]) ){
					sendFlag += 1;
					isRecv = 1;
					int cnt = atoi(parameter[1]+1);
					recvIndex = cnt;
					if ( sendList[cnt][n] == 0 ){
						char out[MAX_LINE_NUM];
						sprintf( out, "*** Error: the pipe #%d->#%d does not exist yet. ***\n", cnt, n );
						write( userList[n].userFd, out, strlen(out) );
						continue;
					}else{
						sendList[cnt][n] = 0;
						dup2( msgPipe[cnt][n][0], 0 );
						close( msgPipe[cnt][n][0] );
						close( msgPipe[cnt][n][1] );
					}
				}else if ( parameter[1][0] == '>' && isdigit(parameter[1][1]) ){
					sendFlag += 1;
					isSend = 1;
					int cnt = atoi(parameter[1]+1);
					sendIndex = cnt;
					if ( userList[cnt].userID == -1 ){
						char out[MAX_LINE_NUM];
						sprintf( out, "*** Error: user #%d does not exist yet. ***\n", cnt );
						write( userList[n].userFd, out, strlen(out) );
						continue;
					}
					if ( sendList[n][cnt] ){
						char out[MAX_LINE_NUM];
						sprintf( out, "*** Error: the pipe #%d->#%d already exists. ***\n", n, cnt );
						write( userList[n].userFd, out, strlen(out) );
						continue;
					}else{
						sendList[n][cnt] = 1;
						pipe( msgPipe[n][cnt] );
						dup2( msgPipe[n][cnt][1], 1 );
					}
				}else{

				}
			}
			if ( paraNum >= 3 ){
				if( parameter[2][0] == '<' && isdigit(parameter[2][1]) ){
					sendFlag += 2;
					isRecv = 1;
					int cnt = atoi(parameter[2]+1);
					recvIndex = cnt;
					if ( sendList[cnt][n] == 0 ){
						char out[MAX_LINE_NUM];
						sprintf( out, "*** Error: the pipe #%d->#%d does not exist yet. ***\n", cnt, n );
						write( userList[n].userFd, out, strlen(out) );
						continue;
					}else{
						sendList[cnt][n] = 0;
						dup2( msgPipe[cnt][n][0], 0 );
						close( msgPipe[cnt][n][0] );
						close( msgPipe[cnt][n][1] );
					}
				}else if ( parameter[2][0] == '>' && isdigit(parameter[2][1]) ){
					sendFlag += 2;
					isSend = 1;
					int cnt = atoi(parameter[2]+1);
					sendIndex = cnt;
					if ( userList[cnt].userID == -1 ){
						char out[MAX_LINE_NUM];
						sprintf( out, "*** Error: user #%d does not exist yet. ***\n", cnt );
						write( userList[n].userFd, out, strlen(out) );
						continue;
					}
					if ( sendList[n][cnt] ){
						char out[MAX_LINE_NUM];
						sprintf( out, "*** Error: the pipe #%d->#%d already exists. ***\n", n, cnt );
						write( userList[n].userFd, out, strlen(out) );
						continue;
					}else{

						sendList[n][cnt] = 1;
						pipe( msgPipe[n][cnt] );
						dup2( msgPipe[n][cnt][1], 1 );
						//close( msgPipe[n][cnt][1] ); error
					}
				}else{

				}
			}
			if ( isRecv ){
				char out[MAX_LINE_NUM];
				sprintf( out, "*** %s (#%d) just received from %s (#%d) by '%s' ***\n", userList[n].nickName, n, userList[recvIndex].nickName, recvIndex, line2 );
				yell( out );
			}
			if( ( childpid = fork() ) < 0 ) {
				fprintf( stderr , "ERROR: fork error\n" );
			} else if( childpid == 0 ) {	// child process
				if( i != cmdNum - 1 ) {
					dup2( pipeTemp[1], 1 );
					
				} else {
					if ( !isSend ){
						dup2( pipeStd[1], 1 );
					}
				}


				close( pipeTemp[0] );
				close( pipeTemp[1] );

				if( fileflag ){
					int filefd = open( filename, O_RDWR | O_CREAT | O_TRUNC, S_IRWXU | S_IRWXG | S_IRWXO );
					if( filefd < 0 ) {
						fprintf( stderr, "ERROR: cannot open file %s\n", filename );
						break;
					}
					dup2( filefd, 1 );
					close( filefd );
				}
				if ( sendFlag == 1 ){
					free( parameter[1] );
					parameter[1] = NULL;
				}else if ( sendFlag == 2 ){
					free( parameter[2] );
					parameter[2] = NULL;
				}else if (  sendFlag == 3 ){
					free( parameter[1] );
					parameter[1] = NULL;
					free( parameter[2] );
					parameter[2] = NULL;
				}
				execvp( parameter[0], parameter );
				exitFlag = 0;
				fprintf( stderr, "Unknown command: [%s].\n", parameter[0] );
				exit(-1);
			} else { // parent process
				if( i != cmdNum - 1 ) {
					dup2( pipeTemp[0], 0 );
				} else {
					if ( !isRecv ){
						dup2( pipeStd[0], 0 );
					}
				}
				close( pipeTemp[0] );
				close( pipeTemp[1] );
				int status = 0;
				while( waitpid( childpid, &status, 0 ) > 0 );
				if( status != 0 ){
					break;
				}

				if ( isSend ){
					char out[MAX_LINE_NUM];
					sprintf( out, "*** %s (#%d) just piped '%s' to %s (#%d) ***\n", userList[n].nickName, n, line2, userList[sendIndex].nickName, sendIndex );
					yell( out );
				}
			}
		}
		while( --paraNum >= 0 ) {
			if ( parameter[paraNum] != NULL ){
				free( parameter[paraNum] );
				parameter[paraNum] = NULL;
			}
		}
	}
	restoreFds( pipeStd );
}

// curdown pipe's number
void cutPipeNum( int n ){
	for ( int i = 0; i < userList[n].pipeList.size(); ++i ){
		(userList[n].pipeList[i].cnt)--;
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
void initPath( char* originalPath ){	 
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
void closePipe( int n ){
	vector<pipeInfo>::iterator it = userList[n].pipeList.begin();
	for (  ; it != userList[n].pipeList.end(); ++it ){
		if ( it->cnt == 0 ){
			close( it->fdOut );
			close( it->fdIn );
			userList[n].pipeList.erase(it);
			return;
		}
	}	
}

// run shell
int runRAS( int n, char* line ){

	char* cmds[MAX_LINE_NUM];
	char line2[MAX_LINE_NUM];
	int stdfd[3];
	int exitFlag = 1;

	initPath( userList[n].path );

	if( checkLine( line ) == 0 ){
		fprintf( stderr, "ERROR: readline is illegal\n" );
		closePipe( n );
		return 0;
	}else{
		strcpy( line2, line );
		saveFds( stdfd );
		changeSockfd( userList[n].userFd );
		exitFlag = checkPipeNum( line, n );

		setInPipe( n );

		int cmdNum = parseCmds( line, cmds );

		executeLine( cmds, cmdNum, exitFlag, n, line2 );

		restoreFds( stdfd );

		clearCmds( cmdNum, cmds );
		
		cutPipeNum( n );	
	}


	// print headText
	if( exitFlag ){
		write( userList[n].userFd, headText, strlen(headText) );
	}
	return exitFlag;
}

int main( int argc, char const *argv[] )
{
	int listenfd;
	int connfd;
	int childpid;
	socklen_t clilen;
	struct sockaddr_in servaddr;
	struct sockaddr_in childaddr;
	// define 
	fd_set asets;	
	fd_set rfds;

	int max_fd;
	char buffer[1024];

	if( chdir("../ras") != 0 ){
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
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl( INADDR_ANY );
	servaddr.sin_port = htons( atoi( argv[1] ) );

	// Bind loacl address
	if( bind( listenfd,( struct sockaddr* )&servaddr, sizeof( servaddr ) ) < 0 ) {
		perror( "ERROR: bind socket error" );
		exit( -1 );
	}

	if( listen( listenfd, MAX_USER_NUM ) < 0 ){
		perror( "ERROR: listen socket error" );
		exit( -1 );
	}


	// init 
	FD_ZERO( &asets );
	FD_SET( listenfd, &asets );
	max_fd = listenfd;
	initUserList();

	// server running
	printf( "========== Server is running ==========\n" );
	while( true ){
		int ret;

		rfds = asets;

		struct timeval tv;
		tv.tv_sec = 1;
		tv.tv_usec = 0;

		ret = select( max_fd + 1, &rfds, NULL, NULL,  &tv );
		if( ret == -1 ){
			perror( "ERROR: select()" );
			return -1;
		}else if( ret == 0 ){
			//printf(" select timeout\n");
			continue;
		}
		// find a bug !!!!! 1 <= max_fd + 1!!!!!
		// !!!!!!!!!!!!!!!
		for (int i = 0; i <= max_fd + 1; i++) {
			if (FD_ISSET(i, &rfds)) {
				if (i == listenfd) {
					clilen = sizeof( childaddr );
					connfd = accept( listenfd,( struct sockaddr* )&childaddr, &clilen );
					if( connfd < 0 ){
						perror( "accept socket error" );
						continue;
					} else {

						/* Add to fd set */
						int n = 1;
						for ( n = 1; n <= MAX_USER_NUM; ++n ){
							if ( userList[n].userID == -1 ){
								//printf("n = %d\n", n);
								initUser( n );
								userList[n].userFd = connfd;
								userList[n].userID = n;
								strcpy( userList[n].ip, "CGILAB" );
								strcpy( userList[n].nickName, "(no name)" );
								userList[n].port = 511;
								strcpy( userList[n].path, originalPath );
								// print welcomeText
								write( connfd, welcomeText, strlen(welcomeText) );
								// print headText

								userLogin( n );
								write( connfd, headText, strlen(headText) );

								break;
							}
						}
						FD_SET(connfd, &asets);
						if (connfd > max_fd){
							max_fd = connfd;	
						}
					}
				} else {

					memset(buffer, 0, sizeof(buffer));
					int recv_len = recv(i, buffer, sizeof(buffer), 0);
					int n;
					for ( n = 1; n <= MAX_CMD_NUM; ++n ){
						if ( userList[n].userFd == i ){
							break;
						}
					}
					if ( recv_len <= 0 ) {
						printf("client[%d] close\n", i);  
						userLogout( n );
						initUser( n );
						close(i);
						FD_CLR(i, &asets);
					} else {
						int isExit = runRAS( n, buffer );
						if( isExit == 0 ){

							userLogout( n );
							initUser( n );
							close(i);
							FD_CLR(i, &asets);
						}
						//printf("i = %d n = %d hah\n", i, n );
					}
				}
			} 
		}
	}

	close( listenfd );
	exit( 0 );
}