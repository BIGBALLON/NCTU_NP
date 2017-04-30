#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <unistd.h>
#include <algorithm>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
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

// share memory 
int uid = 0;
int connfd = 0;


// pipe information in & out & pipe count
struct pipeInfo{
	int fdIn;
	int fdOut;
	int cnt;
};

struct userInfo
{
	int pid;
	int uid;
	int port;
	char ip[20];
	char nickName[20];
	//char path[50];
	//char tellBuff[MAX_LINE_NUM];
	//char yellBuff[MAX_LINE_NUM];
	char msgBuff[MAX_LINE_NUM];
	int sendInfo[31];
	int recvInfo[31];
	//char sendBuff[MAX_LINE_NUM];
	//vector<pipeInfo> pipeList;

};

userInfo* userList;

// pipe list : used to save pipe
vector<pipeInfo> pipeList;

void initUserList();
int readLine( int fd, char* ptr );
int isPipeExist( int number, int& index );
int checkPipeNum( char* line );
void setInPipe();
void saveFds( int *stdfd );
void restoreFds( int *stdfd );
int parseCmds( char* line, char** cmds );
int cmd2argv( char* cmd, char** parameter, int &fileflag, char* filename  );
void executeLine( char** cmds, int cmdNum, int &exitFlag, int n, char* line2 );
void cutPipeNum();
int checkLine( char* line );
void clearCmds( int cmdNum, char **cmds );
void initPath();
void changeSockfd( int sockfd );
void closePipe();
void runRAS( int sockfd );

void initUserList(){
	memset(userList,0,sizeof(userList));
	for (int i = 0; i < MAX_USER_NUM+5; ++i){
		userList[i].pid = -1;
		userList[i].uid = -1;
		strcpy( userList[i].nickName, "(no name)" );
	}	
}

void yell( char* out ){
	for (int i = 1; i <= MAX_USER_NUM; ++i){
		if ( userList[i].pid != -1 ){
			strcpy( userList[i].msgBuff, out );
			kill( userList[i].pid, SIGALRM );
		}
	}
}

void initUser( int n ){
	userList[n].uid = -1;
	userList[n].pid = -1;
	strcpy( userList[n].nickName, "(no name)" );
	for (int i = 1; i <= MAX_USER_NUM; ++i){
		userList[n].sendInfo[i] = 0;
		userList[n].recvInfo[i] = 0;
	}
}

void tell( char *out, int to ){
    if ( userList[to].pid != -1 ){
        strcpy( userList[to].msgBuff, out );
        kill( userList[to].pid, SIGALRM );
    }
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
				if( setenv( parameter[1], parameter[2], 1 ) < 0 )
					fprintf( stderr, "ERROR: setenv() error\n" );
			}
		}else if( strcmp( parameter[0], "exit") == 0 ){
			exitFlag = 0;
		}
		else if( strcmp( parameter[0], "who") == 0 ){
			char out[MAX_LINE_NUM];
			char tmp[MAX_LINE_NUM];
			strcpy( out, "<ID>	<nickname>	<IP/port>	<indicate me>\n" );
			for ( int id = 1; id <= MAX_USER_NUM; ++id ){
				if ( userList[id].uid != -1 ){
					if( id == n ) sprintf( tmp, "%d	%s	%s/%d	<-me\n", userList[id].uid, userList[id].nickName, userList[id].ip, userList[id].port );
					else sprintf( tmp, "%d	%s	%s/%d\n", userList[id].uid, userList[id].nickName, userList[id].ip, userList[id].port );
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
				if ( userList[number].uid != number ){
					sprintf( out, "*** Error: user #%d does not exist yet. ***\n", number );
					write( 1, out, strlen(out) );
				}else{
					sprintf( tmp, "*** %s told you ***: ", userList[n].nickName );
					strcpy( out, tmp );
					strcat( out, parameter[2] );
					strcat( out, "\n" );
					tell( out, number );
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
					write( 1, out, strlen(out) );
				}
			}
		}else{
			int isSend = 0;
			int isRecv = 0;
			int sendIndex = 0;
			int recvIndex = 0;
			int sendFlag = 0;
			int outfd = 0;
			int infd = 0;

			if ( paraNum >= 2 ){
				if( parameter[1][0] == '<' && isdigit(parameter[1][1]) ){
					sendFlag += 1;
					isRecv = 1;
					int cnt = atoi(parameter[1]+1);
					recvIndex = cnt;
					if ( userList[n].recvInfo[recvIndex] == 0 ){
						char out[MAX_LINE_NUM];
						sprintf( out, "*** Error: the pipe #%d->#%d does not exist yet. ***\n", cnt, n );
						write( 1, out, strlen(out) );
						continue;
					}else{
						userList[n].recvInfo[recvIndex] = 0;
						userList[recvIndex].sendInfo[n] = 0;
					}
				}else if ( parameter[1][0] == '>' && isdigit(parameter[1][1]) ){
					sendFlag += 1;
					isSend = 1;
					int cnt = atoi(parameter[1]+1);
					sendIndex = cnt;
					if ( userList[cnt].uid == -1 ){
						char out[MAX_LINE_NUM];
						sprintf( out, "*** Error: user #%d does not exist yet. ***\n", cnt );
						write( 1, out, strlen(out) );
						continue;
					}
					if ( userList[n].sendInfo[sendIndex] == 1 ){
						char out[MAX_LINE_NUM];
						sprintf( out, "*** Error: the pipe #%d->#%d already exists. ***\n", n, cnt );
						write( 1, out, strlen(out) );
						continue;
					}else{
						userList[n].sendInfo[sendIndex] = 1;
						userList[sendIndex].recvInfo[n] = 1;
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
					if ( userList[n].recvInfo[recvIndex] == 0 ){
						char out[MAX_LINE_NUM];
						sprintf( out, "*** Error: the pipe #%d->#%d does not exist yet. ***\n", cnt, n );
						write( 1, out, strlen(out) );
						continue;
					}else{
						userList[n].recvInfo[recvIndex] = 0;
						userList[recvIndex].sendInfo[n] = 0;
					}
				}else if ( parameter[2][0] == '>' && isdigit(parameter[2][1]) ){
					sendFlag += 2;
					isSend = 1;
					int cnt = atoi(parameter[2]+1);
					sendIndex = cnt;
					if ( userList[cnt].uid == -1 ){
						char out[MAX_LINE_NUM];
						sprintf( out, "*** Error: user #%d does not exist yet. ***\n", cnt );
						write( 1, out, strlen(out) );

						continue;
					}
					if ( userList[n].sendInfo[sendIndex] == 1 ){
						char out[MAX_LINE_NUM];
						sprintf( out, "*** Error: the pipe #%d->#%d already exists. ***\n", n, cnt );
						write( 1, out, strlen(out) );

						continue;
					}else{
						userList[n].sendInfo[sendIndex] = 1;
						userList[sendIndex].recvInfo[n] = 1;
					}
				}else{

				}
			}

			if ( isRecv ){
				char fifo[MAX_LINE_NUM];
				sprintf( fifo, "/tmp/%dto%d", recvIndex, n );
				if( ( infd = open( fifo , O_RDONLY ) ) == -1 ){
					printf("infd error\n");
					exit(-1);
				}
				
				dup2( infd, 0 );
				close( infd );
			}
			if( isSend ){
				char fifo[MAX_LINE_NUM];
				sprintf( fifo, "/tmp/%dto%d", n, sendIndex );

				if( ( outfd = open( fifo , O_CREAT | O_WRONLY | O_TRUNC , S_IWUSR | S_IRUSR ) ) == -1 ){
					printf("outfd error\n");
					exit(-1);
				}

				dup2( outfd, 1 );
				close( outfd );
			}
			if ( isRecv ){
				char out[MAX_LINE_NUM];
				sprintf( out, "*** %s (#%d) just received from %s (#%d) by '%s' ***\n", userList[n].nickName, n, userList[recvIndex].nickName, recvIndex, line2 );
				yell( out );
				sprintf( out, "/tmp/%dto%d", recvIndex, n );
				unlink( out );
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
				//break;
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
					if( WEXITSTATUS(status) == 0 ){
						i = cmdNum;
					}
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
	char line2[MAX_LINE_NUM];
	char* cmds[MAX_LINE_NUM];
	int stdfd[3];
	int exitFlag = 1;

	changeSockfd( sockfd );

	initPath();
	// print welcomeText
	//write( 1, welcomeText, strlen(welcomeText) );

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

			strcpy( line2, line );
			saveFds( stdfd );

			exitFlag = checkPipeNum( line );

			setInPipe();

			int cmdNum = parseCmds( line, cmds );

			executeLine( cmds, cmdNum, exitFlag, uid, line2 );

			restoreFds( stdfd );

			clearCmds( cmdNum, cmds );
			
			cutPipeNum();	
		}
	}

}

void shmHandler( int sig ){
	write( connfd, userList[uid].msgBuff, strlen(userList[uid].msgBuff) );
	strcpy( userList[uid].msgBuff, " " );

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

int main( int argc, char const *argv[] )
{
	// init share memory
	int shmIndex = shmget( 999, sizeof(userInfo)*35, IPC_CREAT | 0660 );
	if(  shmIndex < 0 ){
		exit(-1);
	}
	if ( (userList = (userInfo*)shmat(shmIndex, NULL, 0 ) ) < 0 ){
		exit(-1);
	}

	// init userlist
	initUserList();

	//init signal
	signal( SIGALRM, shmHandler );


	int					listenfd;
	int					childpid;
	socklen_t			  clilen;
	struct sockaddr_in	 servaddr;
	struct sockaddr_in	 childaddr;

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
	servaddr.sin_family	  = AF_INET;
	servaddr.sin_addr.s_addr = htonl( INADDR_ANY );
	servaddr.sin_port		= htons( atoi( argv[1] ) );

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
			int pos = 0;
			for ( pos = 1; pos <= MAX_USER_NUM; ++pos ){
				if ( userList[pos].pid == -1 ){
					initUser( pos );
					uid = pos;
					userList[pos].pid = getpid();
					userList[pos].uid = pos;
					strcpy( userList[pos].ip, "CGILAB" );
					userList[pos].port = 511;
					strcpy( userList[pos].nickName, "(no name)" );
					break;
				}
			}
			write( connfd, welcomeText, strlen(welcomeText) );

			userLogin( pos );

			runRAS( connfd );

			userLogout( pos );

			initUser( pos );

			shmdt( userList );
			exit( 0 );
		}else{
			close( connfd );
		}
	}

	close( listenfd );
	exit( 0 );
}