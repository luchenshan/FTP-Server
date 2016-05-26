#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include "cl.h"
#include "hmlinkedlist.h"




static sig_atomic_t doneflag=0;
int gI_socketFd;
pid_t gPid_client;
hmlist_t *fileList=NULL;
int gPipe_listenSocket[2]; // socketten gelenler buradan aktarilacak




int main(int argc,char *argv[]){

	if(argc!=3){
		fprintf(stderr, "USAGE : %s ipnum portnum\n",argv[0]);
		return 0;
	}


	signal(SIGINT,sigHandler);
	char line[MAX_LINE_LEN];
	int portnum = atoi(argv[2]);
	pid_t pidServer=-1;
	gI_socketFd = connectServer(argv[1],portnum);

	if(gI_socketFd == -1){
		perror("socket connect");
		return 1;
	}

	gPid_client = getpid();
	// send pid and take server pid
	write(gI_socketFd,&gPid_client,sizeof(pid_t));
	read(gI_socketFd,&pidServer,sizeof(pid_t));

	printf("[%ld] connected to [%ld]server\n",(long)gPid_client,(long)pidServer);

	pipe(gPipe_listenSocket);
	pthread_t th_sockListener;
	pthread_create(&th_sockListener,NULL,socketListener,NULL);

	Command_e command; // command value
	while(!doneflag){
		memset(line,0,MAX_LINE_LEN);
		printf("Enter your command : ");
		if(fgets(line,MAX_LINE_LEN,stdin)!=NULL && !doneflag){
			line[strlen(line)-1]='\0';
			if(strcmp(line,"help")==0){
				showHelpManual();
			}else if(strcmp(line,"listLocal")==0){
				fileList = readDirectory("./");
				printList(fileList);
				deleteList(fileList);
				free(fileList);
			}else if(strcmp(line,"lsClient")==0){
				pid_t pidOnlineClient;
				command = LS_CLIENT;
				// get onlien clients pid request
				write(gI_socketFd,&command,sizeof(Command_e));
				// thread socketi okuyup sonuclari gonderince alacak
				printf("Waiting for list of online clients\n");
				int size;
				read(gPipe_listenSocket[0],&size,sizeof(int));
				printf("There are %d online client\n",size);
				int i=0;
				for(i=0;i<size;++i){
					read(gPipe_listenSocket[0],&pidOnlineClient,sizeof(pid_t));
					printf("%d. %ld\n",i,(long)pidOnlineClient);
				}
				
			}else{
				printf("#Entered invalid command\n");
				printf("#Please check 'help' page\n");
			}

		}else{
			command = DIE;
			write(gI_socketFd,&command,sizeof(Command_e));
		}
	}


	pthread_join(th_sockListener,NULL);
	printf("\n\nCLIENT CLOSED SUCCUSSFULLY\n\n");
	return 0;
}


void sigHandler(int signum){
	printf("%s handled\n",strsignal(signum));
	doneflag=1;
	close(0);
}


void showHelpManual(){
	printf("\n#################################\n");
	printf("Arguments...\n");
	printf("1. help : show help manual\n");
	printf("2. listLocal : to list the local files in the directory client program started\n");
	printf("3. listServer : to list the files in the current scope of the server-client\n");
	printf("4. lsClient pid : lists the clients currently connected to the server with their respective clientids\n");
	printf("5. sendFile <filename> <clientid>  : send the file <filename> (if file exists) from \
		local directory to the client with client id clientid. If no client id is given the  \
		file is send to the servers local directory.\n");
	printf("\n#################################\n");
	printf("\n");
}


void sigAlarmHandler(int signum){
	doneflag=1;
	close(gI_socketFd);
}



void *socketListener(void *args){

	Command_e command;

	while(read(gI_socketFd,&command,sizeof(Command_e))>0){
		if(command == LS_CLIENT){ 

			int size;
			// kac tane pdi okunacak
			read(gI_socketFd,&size,sizeof(int));

			write(gPipe_listenSocket[1],&size,sizeof(int));
			int i=0;
			pid_t pidTemp;
			for(i=0;i<size;++i){
				// pid leri oku ve main threade yolla
				read(gI_socketFd,&pidTemp,sizeof(pid_t));
				write(gPipe_listenSocket[1],&pidTemp,sizeof(pid_t));
			}

		}else if(command==DIE){
			break;


		}
	}



	return NULL;
}



int connectServer(const char *ipnum,int portnum){

	struct sockaddr_in serverAddr;
	socklen_t serverArrLen;
	signal(SIGALRM,sigAlarmHandler);
	
	if((gI_socketFd=socket(AF_INET,SOCK_STREAM,0))<0){
		perror("->ConnecServer : socket error");
		return -1;
	}

	serverArrLen = sizeof(struct sockaddr_in);
	memset(&serverAddr,0,serverArrLen);
	serverAddr.sin_family=AF_INET;
	serverAddr.sin_addr.s_addr=inet_addr(ipnum);
	serverAddr.sin_port=htons(portnum);

	alarm(2); // wait 2second to connect
	if(connect(gI_socketFd,(struct sockaddr*)&serverAddr,serverArrLen)<0){
		perror("->ConnectServer : connect error");
		return -1;
	}
	alarm(0);

	return gI_socketFd;
}

hmlist_t* readDirectory(const char *dirpath){

	DIR *pDir;
	struct dirent *pDirent=NULL;
	hmlist_t *files=NULL;

	pDir = opendir(dirpath);
	if(pDir==NULL){
		perror("Dir open error : ");
		return NULL;
	}

	files = (hmlist_t *)malloc(sizeof(hmlist_t));
	memset(files,0,sizeof(hmlist_t));
	char strPath[MAX_FILE_NAME];
	while((pDirent = readdir(pDir))!=NULL){
		memset(strPath,0,MAX_FILE_NAME);
		sprintf(strPath,"%s/%s",dirpath,pDirent->d_name);
		if(strcmp(strPath,".")!=0 && strcmp(strPath,"..")!=0){
			int filesize;
			if((filesize = isRegFile(strPath))!=-1){
				hmFile_t file;
				memset(&file,0,sizeof(hmFile_t));
				strcpy(file.filename,strPath);
				file.filesize = filesize;
				addList(files,&file);
			}
		}
	}
	closedir(pDir);
	return files;

}

// if regular file return file size
int isRegFile(const char * fileName){
  struct stat statbuf;

  if(stat(fileName,&statbuf) == -1){
    return -1;
  }else{
    return S_ISREG(statbuf.st_mode) ? statbuf.st_size : -1;
  }
}

