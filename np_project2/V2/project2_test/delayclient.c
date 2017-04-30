#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <sys/uio.h>

#define MAXUSER 500
#define EXIT_STR "exit\r\n"

int write_enable[MAXUSER];

int contain_prompt ( char* line )
{
  int i, prompt = 0 ;
  for (i=0; line[i]; ++i) {
    switch ( line[i] ) {
      case '%' : prompt = 1 ; break;
      case ' ' : if ( prompt ) return 1;
      default: prompt = 0;
    }
  }
  return 0;
} 

int recv_msg(int userno,int from)
{
  char buf[3000],*tmp;
  int len,i;
  
  len=read(from,buf,sizeof(buf)-1);
  if(len < 0) return -1;

  buf[len] = 0;
  if(len>0)
  {
    for(tmp=strtok(buf,"\n"); tmp; tmp=strtok(NULL,"\n"))
    {
      if ( contain_prompt(tmp) ) write_enable[userno] = 1 ;
      printf("%d| %s\n",userno,tmp);  // echo input
    }
  }
  fflush(stdout); 
  return len;
}

int readline(int fd,char *ptr,int maxlen)
{
  int n, rc;
  char c;
  *ptr = 0;
  for(n=1; n<maxlen; n++)
  {
    rc=read(fd,&c,1);
    if(rc== 1)
    {
      *ptr++ = c;
      if(c=='\n')  break;
    }
    else if(rc==0)
    {
      if(n==1)     return 0;
      else         break;
    }
    else return(-1);
  }
  return n;
}      


int main(int argc,char *argv[])
{
  fd_set              rfds, afds;
  char                buf[3000], msg_buf[3000], msg_buf1[3000];
  int                 unsend, len, SERVER_PORT, i;
  int                 client_fd[MAXUSER];
  struct sockaddr_in  client_sin;
  struct hostent      *he;
  FILE                *fd; 

  
  // handle args
  if(argc == 3)
    fd = stdin;
  else if(argc == 4)
    fd = fopen(argv[3], "r");
  else {
    fprintf(stderr, "Usage : client <server ip> <port> <testfile>\n");
    exit(1);
  }    
  if((he=gethostbyname(argv[1])) == NULL) {
    fprintf(stderr, "Usage : client <server ip> <port> <testfile>\n");
    exit(1);
  }
  SERVER_PORT = atoi(argv[2]);
  
  // handle socket
  for(i=0; i<MAXUSER; i++) client_fd[i] = socket(AF_INET,SOCK_STREAM,0);
  memset(&client_sin, 0, sizeof(client_sin)); 
  client_sin.sin_family = AF_INET;
  client_sin.sin_addr = *((struct in_addr *)he->h_addr); 
  client_sin.sin_port = htons(SERVER_PORT);
  
  // handle fds
  FD_ZERO(&rfds);
  FD_ZERO(&afds);
  FD_SET(fileno(fd),&afds); 
  
  unsend = 0;
  memset (write_enable, 0, sizeof(write_enable));

  while(1)
  { 
    // receive message from server
    memcpy(&rfds, &afds, sizeof(fd_set));
    if(select(MAXUSER+5,&rfds,NULL,NULL,NULL) < 0) return 0;
    for(i=0; i<MAXUSER; i++)
    {  
      if(FD_ISSET(client_fd[i], &rfds))
      {
        if(recv_msg(i, client_fd[i]) < 0)
        {
          close(client_fd[i]);
          exit(1);
        }
      }
    }

    if(unsend || FD_ISSET(fileno(fd), &rfds)) {
      // printf ( "unsend: %d, msg_buf: %s\n", unsend, msg_buf ) ;
      if(!unsend) {
        //送meesage
        len = readline(fileno(fd), msg_buf, sizeof(msg_buf));
        if(len < 0) exit(1);
        msg_buf[len] = 0;
        fflush(stdout);
      }
      unsend = 0;
      if(!strncmp(msg_buf, "exit",4))  // exit all
      {
        printf("\n%s", msg_buf);
        
        //waiting for server messages
        sleep(2); 
        
        for(i=0; i<MAXUSER; i++)
        {
          if (FD_ISSET(client_fd[i],&afds))//modify by sapp
          {
            if(write(client_fd[i],EXIT_STR ,6) == -1) return -1;
            while(recv_msg(i, client_fd[i]) >0);
            FD_CLR(client_fd[i], &afds);
            close(client_fd[i]);
          }
        }
        FD_CLR(fileno(fd), &afds);
        fclose(fd);
        exit(0);
      }
      else if(!strncmp(msg_buf,"login",5)) { // login
        printf("\n%s", msg_buf);
        sscanf(msg_buf, "login%d", &i);
        
        if(i<MAXUSER && i>=0) {
          if(connect(client_fd[i],(struct sockaddr *)&client_sin,sizeof(client_sin)) == -1) {
            printf("connect fail\n");
          }
          else FD_SET(client_fd[i], &afds);
        }
      }
      else if (!strncmp(msg_buf,"logout",6)) // logout
      {
        sscanf(msg_buf, "logout%d", &i);
        
        if ( write_enable[i] ) {
          printf("\n%s",msg_buf);
          
          if(i<MAXUSER && i>=0) {
            
            if(FD_ISSET(client_fd[i], &afds)) {
              if(write(client_fd[i], EXIT_STR,6) == -1) return -1;
              while(recv_msg(i,client_fd[i]) > 0);
              FD_CLR(client_fd[i], &afds);
              FD_CLR(client_fd[i],&rfds);
              close(client_fd[i]);
              client_fd[i] = socket(AF_INET,SOCK_STREAM,0);
            } 
            else {
              // printf ( "pass logout(fd) %s\n", msg_buf ) ;
              unsend = 1 ;
            }
            
          }
        } 
        else {
          // printf ( "pass logout(we) %s\n", msg_buf ) ;
          unsend = 1;
        }
      }  
      else  // send command
      {
        char tmpArr[20];
        sscanf(msg_buf, "%d", &i);
        sprintf(tmpArr, "%d", i);
        strcpy(msg_buf1,&msg_buf[strlen(tmpArr)+1]);

        if ( write_enable[i] ) {
          printf("\n%d %% %s",i,msg_buf1);
          if(i<MAXUSER){
            //write(client_fd[i],msg_buf1,len-1,0);
            write(client_fd[i], msg_buf1, strlen(msg_buf1));
            write_enable[i] = 0; 
          }
        } else {
          unsend = 1 ;
        }
      }
      usleep(300000); //sleep 1 sec before next commandlready at oldest change 
    }
  } // end of while
  
  return 0;
}  // end of main
