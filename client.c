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

//A global variable for the number of file transfers so far
unsigned int transfer_count = 0;

//A structure to be used to transfer the parameters to the thread
struct client_parameters {
    struct sockaddr_in client_address;
    unsigned int count;
};

//This function will connect to the server as given by the port and address arguements and return a file descriptor for this new connection, and -1 should an error occur
int server_connect(int server_port, char* server_ip_address_str) {
    //Creation of Client Socket for Control Stream
    printf("Connection to %d and %s\n", server_port, server_ip_address_str);
	int server_control_sd = socket(AF_INET,SOCK_STREAM,0);
	if(server_control_sd < 0)
	{
		perror("Client | Socket for Control Stream to Server | Socket Creation Failed: ");
		return -1;
	}
    //Allowing the sock to be reused
	int value  = 1;
	setsockopt(server_control_sd,SOL_SOCKET,SO_REUSEADDR,&value,sizeof(value));
    printf("Client Socket Created.\n");

    //A temprorary variable to be used to store the details of the server
    struct sockaddr_in serv_addr;
    bzero(&serv_addr,sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(server_ip_address_str);
    serv_addr.sin_port = htons(server_port);

    //Connecting to the Server
    if (connect(server_control_sd, (struct sockaddr *)& serv_addr, sizeof(serv_addr)))
    {
        printf("Client | Socket for Control Stream to Server | Connection Creation Failed: ");
        return -1;
    }

    return server_control_sd;
}

void port_request_generate(char* request, struct sockaddr_in personal_address) {
    unsigned int h_1, h_2, h_3, h_4, p_1, p_2;
    int personal_port = ntohs(personal_address.sin_port) + (++transfer_count);
    char* personal_ip = inet_ntoa(personal_address.sin_addr);
    sscanf(personal_ip, "%u.%u.%u.%u", &h_1, &h_2, &h_3, &h_4);
    p_1 = personal_port/256;
    p_2 = personal_port%256;

    snprintf(request, REQUEST_BUFFER_SIZE, PORT_REQUEST_FORMAT, h_1, h_2, h_3, h_4, p_1, p_2);
};

bool port_request_handler(int server_control_sd, struct sockaddr_in personal_address) {
    char temp_request[REQUEST_BUFFER_SIZE];
    char temp_response[RESPONSE_BUFFER_SIZE]; 
    bzero(temp_request, REQUEST_BUFFER_SIZE);
    bzero(temp_response, RESPONSE_BUFFER_SIZE);
    port_request_generate(temp_request, personal_address);
    //Sending out the Port Command to the Server
    if(send(server_control_sd,temp_request,strlen(temp_request),0)<0)
    {
        perror("Port Command Send to Server Failed");
        return false;
    }
    int bytes_recieved = recv(server_control_sd, temp_response, RESPONSE_BUFFER_SIZE,0);
    if (bytes_recieved < 0) {
        printf("Response Reciept from Server Failed After Port\n");
        return false;
    }
    printf("SERVER RESPONSE FOLLOWING PORT COMMAND: <%s>\n", temp_response);
    if (strcmp(temp_response, PORT_SUCCESS_RESPONSE) != 0) {
        printf("Server did not give expected response to Port Command\n");
        return false;
    }
    return true;
}

//The following function takes and handles the input from the user into the client
//It returns True or False depending on whether the quit command has been invoked or not
bool user_input_handler(int server_control_sd, char RESPONSE_BUFFER[], char REQUEST_BUFFER[], char TEMP_RESPONSE_BUFFER[], char TEMP_REQUEST_BUFFER[],  struct sockaddr_in client_addr, bool* isSkip_ptr) {
    printf("*isSkip_ptr = %d\n", *isSkip_ptr);   
    if (*isSkip_ptr == false) {
        bzero(RESPONSE_BUFFER, RESPONSE_BUFFER_SIZE);
        bzero(TEMP_RESPONSE_BUFFER, RESPONSE_BUFFER_SIZE);
        //Receiving Reponse from Server
        printf("\nWAITING FOR RESPONSE FROM SERVER\n");

        int bytes_recieved = recv(server_control_sd, RESPONSE_BUFFER, RESPONSE_BUFFER_SIZE,0);
        if (bytes_recieved < 0) {
            printf("Response Reciept from Server Failed\n");
            return false;
        }
        printf("SERVER RESPONSE: %s\n", RESPONSE_BUFFER);
        //If the exit message is recieved, we set the isConnected flag variable to false and exit
        if(strcmp(RESPONSE_BUFFER, QUIT_RESPONSE) == 0)
            return false;
    }

    *isSkip_ptr = false;

    printf("ftp> ");
    bzero(REQUEST_BUFFER, REQUEST_BUFFER_SIZE);
    //Getting Input from the User to be sent to Server
    fgets(REQUEST_BUFFER,REQUEST_BUFFER_SIZE,stdin);
    REQUEST_BUFFER[strcspn(REQUEST_BUFFER, "\n")] = 0;  //remove trailing newline char from REQUEST_BUFFER, fgets does not remove it

    char* request_parameters = NULL;
    strcpy(TEMP_REQUEST_BUFFER, REQUEST_BUFFER);
    int request_type = get_request_type(TEMP_REQUEST_BUFFER, &request_parameters);

    //If a stor, retr, or list command, port must be sent out before it
    if (request_type == 21 || request_type == 22 || request_type == 23) {
        printf("REQUEST TO BE SENT: <%s>\n", REQUEST_BUFFER);
        bool isPortRequestSuccessful = port_request_handler(server_control_sd, client_addr);
        //The PORT command did not succeed
        if (isPortRequestSuccessful == false) {
            *isSkip_ptr = true;
            return true;
        }
        printf("\tPARAMETERS: <%s>\n", request_parameters);
        //A check is made whether or not the specfied file per the request_parameters is present or not
        if (request_type == 21 && get_file_length(request_parameters) == -1) {
            //Request should not be sent and a response should not be waited for
            printf(FILE_TRANSFER_NULL_RESPONSE);
            *isSkip_ptr = true;
            return true;
        }
        
        //Creating a new socket and binding it at the present_port + transfer_count port number
        int client_sd = create_and_bind_socket(ntohs(client_addr.sin_port) + transfer_count, inet_ntoa(client_addr.sin_addr));
        printf("BINDING TO %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port) + transfer_count);


        //Beging Listening on the Socket for a Connection from the Server (asynchrnously)
        if(listen(client_sd,LISTEN_BACKLOG)<0)
        {
            perror("Listen Error:");
            return -1;
        }

        //Sending out the user's actual request to the server
        if(send(server_control_sd,REQUEST_BUFFER,strlen(REQUEST_BUFFER),0)<0)
        {
            perror("STOR, RETR, LIST Command Send to Server Failed");
            return false;
        }

        bzero(RESPONSE_BUFFER, RESPONSE_BUFFER_SIZE);
        //Receiving Reponse from Server
        printf("WAITING FOR RESPONSE FROM SERVER TO STOR, RETR, LIST COMMAND FROM SERVER\n");
        int bytes_recieved = recv(server_control_sd, RESPONSE_BUFFER, RESPONSE_BUFFER_SIZE,0);
        if (bytes_recieved < 0) {
            printf("Response Reciept from Server Failed\n");
            return false;
        }
        printf("SERVER RESPONSE: %s\n", RESPONSE_BUFFER);

        //If the go ahead message is not recieved, we set the isConnected flag variable to false and exit
        if(strcmp(RESPONSE_BUFFER, FILE_TRANSFER_BEGIN_RESPONSE) != 0) {
            printf("Server is not ready for file transfer operation. Closing down connection from Client's side");
            close(client_sd);
            return false;
        } else {
            //Creating a structure to hold all the details to the server from which a connection is accepted
            struct sockaddr_in server_addr;
            socklen_t server_addr_size = sizeof(server_addr);
            //Reseting the structure containing the client address
            bzero(&server_addr, sizeof(server_addr));

            printf("Waiting for an Accept from the Server\n");
            //Accepting an incoming client connection off of the Listen Queue
            int server_sd = accept(client_sd, (struct sockaddr*) &server_addr, &server_addr_size);
            printf("Done Waiting for an Accept from the Server\n");

            //Something has gone wrong and this present request is to be neglected
            if (server_sd < 0) {
                perror("Accept Failed");
                close(client_sd);
                return false;
            }

            printf("CLIENT DATA CONNECTION TO %s | %d\n", inet_ntoa(server_addr.sin_addr), ntohs(server_addr.sin_port));

            //Now we have a connection to the server which we will use to send and recieve a file as needed
            //If the file is to be stored at the server, it is to be sent by the client and recieved by the server
            if (request_type == 21) {
                bool isFileSent = send_file(request_parameters, server_sd);
                if (isFileSent == false) {
                    printf("FILE SENDING FAILED.\n");
                }
                close(server_sd);
            } else if (request_type == 22) {
                bool isFileRecieved = recv_file(request_parameters, server_sd);
                if (isFileRecieved == false) {
                    printf("FILE RECIEVING FAILED.\n");
                }
            } else {
                char FILE_BUFFER[FILE_CHUNK_SIZE];
                printf("LIST OUTPUT RECIEPT BEGIN\n");
                int recieved_bytes = recv(server_sd, FILE_BUFFER, sizeof(FILE_BUFFER), 0);
                if (recieved_bytes == 0)
                    printf("LIST OUTPUT RECIEVING FAILED.\n");
                while (recieved_bytes > 0) {
                    recieved_bytes = recv(server_sd, FILE_BUFFER, sizeof(FILE_BUFFER), 0);
                    printf("%.*s", (int) strlen(FILE_BUFFER), FILE_BUFFER);
                }
                printf("LIST OUTPUT RECIEVING COMPLETE\n");
            }            
        }
    } else if (request_type == 10 || request_type == 11 || request_type == 12){
        //Handling !CWD
        if (request_type == 11) {
            if (chdir(request_parameters) != 0) {
                printf("chdir() to %s failed", request_parameters);
            } else {
                printf("Directory changed to %s\n", request_parameters);
            }
            printf("%s\n", TEMP_RESPONSE_BUFFER);
        } else if (request_type == 12){
            //Handling !PWD
            char path[PATH_BUFFER_SIZE];
            bzero(path, PATH_BUFFER_SIZE);
            if (getcwd(path, PATH_BUFFER_SIZE) != NULL) {
                printf("Current working dir: %s\n", path);
            } else {
                perror("getcwd() error");
                strcpy(TEMP_RESPONSE_BUFFER, PWD_FAILURE_RESPONSE);
            }
        } else {
            //Will execute the !ls command and store its output in a pipe
            FILE *ls_file = popen("ls", "r");
            if (ls_file == NULL) {
                printf("LIST COMMAND COULD NOT BE EXECUTED\n");
                // success = false;
            } else {
                char FILE_BUFFER[FILE_CHUNK_SIZE];
                printf("BEGIN DISPLAYING LIST CONTENTS\n");
                while ((fread(FILE_BUFFER, 1, FILE_CHUNK_SIZE, ls_file)) > 0) {
                    printf("%.*s",(int) strlen(FILE_BUFFER), FILE_BUFFER);
                }
                printf("END SENDING LIST CONTENTS\n");
            }
            pclose(ls_file);
        }
        *isSkip_ptr=true;
    } else {
        printf("REQUEST TO BE SENT: <%s>\n", REQUEST_BUFFER);
        //Sending out the user's messsage to the server
        if(send(server_control_sd,REQUEST_BUFFER,strlen(REQUEST_BUFFER),0)<0)
        {
            perror("Control Command Send to Server Failed");
            return false;
        }
    }

 
    //Thread Function that sets up a socket and binds itself on the required parameters (client_addr)

    
    return true;
}

int main(int argc, char* argv[]) {
    printf("CLIENT SEEKING CONNECTION TO SERVER\n");
    //Connecting to the Server for Control
    int server_control_sd = server_connect(SERVER_CONTROL_PORT, SERVER_IP_ADDRESS);
    if (server_control_sd == -1) {
        exit(-1);
    }
    printf("CLIENT CONNECTED TO SERVER\n");

    struct sockaddr_in client_addr;
    socklen_t client_addr_size = sizeof(client_addr);

    if (getsockname(server_control_sd, (struct sockaddr*) &client_addr, &client_addr_size) != 0) {
        printf("Could not get socket information.");
        exit(-1);
    } else {
        printf("OWN PORT: %u | ADDRESS: %s\n", ntohs(client_addr.sin_port), inet_ntoa(client_addr.sin_addr));
    }


    //Will determine whether or not, we continue to remain connected to the Server and Parsing Input
    int isContinue = true;
    //These are buffers used to maintain responses from the client and server
    char response_buffer[RESPONSE_BUFFER_SIZE];
    char request_buffer[REQUEST_BUFFER_SIZE];
    char temp_response_buffer[RESPONSE_BUFFER_SIZE];
    char temp_request_buffer[REQUEST_BUFFER_SIZE];
    //Will dileneate whether or not a skip with regards to waiting for the response should be done
    bool isSkip = false;

    while (isContinue) {
        isContinue = user_input_handler(server_control_sd, response_buffer, request_buffer, temp_response_buffer, temp_request_buffer, client_addr, &isSkip);
    }

    close(server_control_sd);
}