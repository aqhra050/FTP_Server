#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include "common.h"

//This is mapping betwene a request type and a numeric value, a pattern of sorts has been choosen to easily delineate priviliged and non-priviliged requests
int get_request_type(char* request, char** request_parameters_ptr) {
    char* request_label = strtok_r(request, " ", &request);
    *request_parameters_ptr = strtok_r(NULL, " ", &request);
    printf("REQUEST: <%s> | PARAMETER: <%s>\n", request_label, *request_parameters_ptr);

    //User 1, 2 <10
    //Local Functions >= 10 < 20
    //Unauthenticated || 1-20
    //Authenticated 21-30

    if (strcmp(request_label, "USER") == 0)
        return 1;
    else if (strcmp(request_label, "PASS") == 0)
        return 2;
    else if (strcmp(request_label, "!LIST") == 0)
        return 10;
    else if (strcmp(request_label, "!CWD") == 0)
        return 11;
    else if (strcmp(request_label, "!PWD") == 0)
        return 12;
    else if (strcmp(request_label, "PORT") == 0)
        return 20;
    else if (strcmp(request_label, "STOR") == 0)
        return 21;
    else if (strcmp(request_label, "RETR") == 0)
        return 22;
    else if (strcmp(request_label, "LIST") == 0)
        return 23;
    else if (strcmp(request_label, "CWD") == 0)
        return 24;
    else if (strcmp(request_label, "PWD") == 0)
        return 25;
    //This is subject to change
    else if (strcmp(request_label, "QUIT") == 0)
        return 0;
    else
        return -1;
}

//Will get the file length of the file with name, fileName, if said file is present and -1 otherwise
int get_file_length(char* fileName) {
    printf("FILENAME: %s\n", fileName);
	FILE *fptr;
    fptr = fopen(fileName,"r");
    if (fptr == NULL) {
        printf("Get File Length Failed.");
        return -1;
    }
	fseek(fptr,0,SEEK_END);						//move the file pointer to the end of the file
	int file_size = ftell(fptr);				//read the position of the filepointer which will be equal to the size of the file
	fclose(fptr);
	return file_size;
}

//Will send the file as given by the file_name_str along the given connection_sd, it checks whether or not the file is present at the source
bool send_file(char* file_name_str, int connection_sd) {
    //Getting the File-Size
    struct sockaddr_in present_addr;
    socklen_t present_addr_size = sizeof(present_addr);

    if (getsockname(connection_sd, (struct sockaddr*) &present_addr, &present_addr_size) != 0) {
        printf("Could not get socket information.");
        exit(-1);
    } else {
        printf("OWN PORT: %u | ADDRESS: %s\n", ntohs(present_addr.sin_port), inet_ntoa(present_addr.sin_addr));
    }

	int file_size = get_file_length(file_name_str);
    if (file_size != -1) {
        FILE* sending_file = fopen(file_name_str, "r");
        char *file_contents = malloc(file_size);
        fread(file_contents, 1, file_size, sending_file);
        //This sends out the file in chunks
        //The count of how many bytes have been sent
        unsigned int bytes_sent = 0;
        //The count of how many bytes are to be sent in the given iteration
        int send_bytes = 0;
        while(bytes_sent < file_size)
        {
            if(file_size - bytes_sent >= FILE_CHUNK_SIZE)
                send_bytes = FILE_CHUNK_SIZE;
            else
                send_bytes = file_size - bytes_sent;
            send(connection_sd, file_contents + bytes_sent, send_bytes, 0);
            bytes_sent += send_bytes;
            printf("Bytes Sent Total: %d | Bytes Sent Iteration: %d\n", bytes_sent, send_bytes);
        }
        return true;
    } else {
        printf("Requested file <%s> is absent.\n", file_name_str);
        return false;
    }
}

//Will receive a file along the connection_sd, and stores it at file_name_str (replacing it if already present)
bool recv_file(char* file_name_str, int connection_sd) {
    char FILE_BUFFER[FILE_CHUNK_SIZE];
    FILE* recv_file_ptr = fopen(file_name_str, "w");
    if (recv_file_ptr == NULL) {
        return false;
    }
    fclose(recv_file_ptr);
    recv_file_ptr = fopen(file_name_str, "a+");
    printf("Begining Reciept\n");
    int recieved_bytes = recv(connection_sd, FILE_BUFFER, sizeof(FILE_BUFFER), 0);
    if (recieved_bytes == 0)
        return false;
    printf("\tRecieved_Bytes: %d", recieved_bytes);
    while (recieved_bytes > 0) {
        fwrite(FILE_BUFFER, recieved_bytes, 1, recv_file_ptr);
        recieved_bytes = recv(connection_sd, FILE_BUFFER, sizeof(FILE_BUFFER), 0);
        printf("\tRecieved_Bytes: %d\n", recieved_bytes);
    }
    fclose(recv_file_ptr);
    return true;
}

// Will create a socket and bind it to the specified port number and IP addr, returning the socket disc if all goes well. Otherwise will return -1
int create_and_bind_socket(int port_number, char* ip_addr_str) {
    //Creation of Server Socket for Control Connection
	int sd = socket(AF_INET,SOCK_STREAM,0);
	if(sd < 0)
	{
		perror("Socket Creation Failed: ");
		return -1;
	}

    //Permitting Re-Use of the Port, if previous Server instances still occupies it
	setsockopt(sd,SOL_SOCKET,SO_REUSEADDR,&(int){1},sizeof(int));

	//Delineating the Port and Address for the Control Connection
	struct sockaddr_in server_address;	                //structure to save IP address and port
	memset(&server_address,0,sizeof(server_address));   //Initialize/fill the server_address to 0
	server_address.sin_family = AF_INET;	            //address family
	server_address.sin_port = htons(port_number);//port
	server_address.sin_addr.s_addr = inet_addr(ip_addr_str);

	//Checking if Bind Succeeded or Failed
	if(bind(sd,(struct sockaddr *)&server_address,sizeof(server_address))<0) 
	{
		perror("Bind failed..:");
		return -1;
	}
    printf("Bind Suceeded\n");

    return sd;
}

// Will create a socket that is identical to the argument, with the only 

