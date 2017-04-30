#include <stdio.h> 
#include <unistd.h>
#include <stdlib.h>
#define time 1     //delayed time (sec) 

int main(int argc,char **argv){
	FILE *fp;
	char c;
	int inTag=0;

	if(argc == 1)
		fp = stdin;
	else if (argc == 2) 
		fp = fopen(argv[1],"r");
	else{
		fprintf(stderr,"Usage:%s <file>\n",argv[1]);
		exit(1);
	}

	while((c = fgetc(fp))!=EOF){
		if(c == '<'){
			inTag = 1;
			continue;
		}
		if(c == '>'){
			inTag = 0;
			continue;
		}
		if(!inTag)
			fputc(c, stdout);
		if(c == '\n') {
			fflush(stdout);
			sleep(time);
		}
	}
	fputc('\n', stdout);
	fflush(stdout);
	fclose(fp);
	return(0);
} 
