#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>
#include <sys/uio.h>

char gSc = 0;
int recv_msg(int from);
int readline(int fd,char *ptr,int maxlen);


int main(int argc,char *argv[])
{
	fd_set readfds;
	int    client_fd;
	struct sockaddr_in client_sin;
	char msg_buf[30000];
	int len;
	int SERVER_PORT;
	FILE *fp; 
	int end;
	struct hostent *he; 
	gSc = 0;
 if(argc == 3)
 	fp = stdin;

 else if(argc == 4)
 {	
 	fp = fopen(argv[3], "r");
 	if (fp == NULL) {
 		fprintf(stderr,"Error : '%s' doesn't exist\n", argv[3]);
 		exit(1);
 	}
 }
 else
 {
 	fprintf(stderr,"Usage : client <server ip> <port> <testfile>\n");
        exit(1);
 }    

 if((he=gethostbyname(argv[1])) == NULL)
 {
 	fprintf(stderr,"Usage : client <server ip> <port> <testfile>");
 	exit(1);
 }
                              
 SERVER_PORT = (u_short)atoi(argv[2]);
 
 client_fd = socket(AF_INET,SOCK_STREAM,0);
 bzero(&client_sin,sizeof(client_sin));
 client_sin.sin_family = AF_INET;
 client_sin.sin_addr = *((struct in_addr *)he->h_addr); 
 client_sin.sin_port = htons(SERVER_PORT);
 if(connect(client_fd,(struct sockaddr *)&client_sin,sizeof(client_sin)) == -1)
 {
  perror("");
  exit(1);
 }

 sleep(1);     //waiting for welcome messages
 
 end=0;
 while(1)
 { 
  FD_ZERO(&readfds);
  FD_SET(client_fd,&readfds);
  if(end==0)
  	FD_SET(fileno(fp),&readfds);
  if(select(client_fd+1,&readfds,NULL,NULL,NULL) < 0)
   exit(1);
  

  if( gSc ==1)
  {
  	//°emeesage
  	len = readline(fileno(fp),msg_buf,sizeof(msg_buf));
  	if(len < 0) exit(1);
  	
  	msg_buf[len-1] = 13;
  	msg_buf[len] = 10;
  	
  	msg_buf[len+1] = '\0';
 	printf("%s",msg_buf);
  	fflush(stdout);
  	if(write(client_fd,msg_buf,len+1) == -1) return -1;

	/*if(!strncmp(msg_buf,"exit",4))
	{
		sleep(2);	//waiting for server messages
		while(recv_msg(client_fd) >0 )
		{}
		shutdown(client_fd,2);
		close(client_fd);
		fclose(fp);
		end =1;
	}    */
	//usleep(1000);// delay 1000 microsecond
	gSc =0;
  }
  if(FD_ISSET(client_fd,&readfds))
  {
	  //±µ¦¬message
	  int errnum;
	  errnum =recv_msg(client_fd);
	  if(errnum <0)
	  {
		  shutdown(client_fd,2);
		  close(client_fd);
		  exit(1);
	  } else if (errnum ==0){
		  shutdown(client_fd,2);
		  close(client_fd);
		  exit(0);
	  }
  }

 } // end of while
}  // end of main

int recv_msg(int from)
{
	char buf[3000];
	int len;
	if((len=readline(from,buf,sizeof(buf)-1)) <0) return -1;
	buf[len] = 0; 
	printf("%s",buf);	//echo input
	fflush(stdout); 
	return len;
}

int readline(int fd,char *ptr,int maxlen)
{
	int n,rc;
	char c;
	*ptr = 0;
	for(n=1;n<maxlen;n++)
	{
		if((rc=read(fd,&c,1)) == 1)
		{
			*ptr++ = c;	
			if(c==' '&& *(ptr-2) =='%'){ gSc = 1; break; }
			if(c=='\n')  break;
		}
		else if(rc==0)
		{
			if(n==1)     return(0);
			else         break;
		}
		else
			return(-1);
	}
	return(n);
}      
  
