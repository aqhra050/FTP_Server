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

int find_user_index(struct user* users_database, char* username, int num_users) {
    printf("User to be searched: %s \n", username);
    printf("NUMBER OF USERS TO BE SEARCHED FROM %i\n", num_users);
    for (int i = 0; i < num_users; i++) {
        printf("User[%i] = %s\n", i, users_database[i].user_name);
        if (strcmp(username, users_database[i].user_name) == 0)
            return i;
    }
    return -1;
}

bool process_query(char* request, char* response, char* path, char** prev_user_ptr, struct user* users_database, bool* isAuthenticatedPtr, int* userIndexPtr, int num_users, struct sockaddr_in* client_address_ptr, bool* isSetClientDetails_ptr, int client_sd) {
    char TEMP_REQUEST[REQUEST_BUFFER_SIZE];
    strcpy(TEMP_REQUEST, request);
    char* request_parameters = NULL;
    int request_type = get_request_type(TEMP_REQUEST, &request_parameters);
    printf("Request Type:%i\n", request_type);
    //Zeroing out the response at the start
    bzero(response, RESPONSE_BUFFER_SIZE);

    if (request_type == 0) {
        strcpy(response, QUIT_RESPONSE);
        return false;
    } else if(request_type == -1) {
        strcpy(response, INVALID_COMMAND_RESPONSE);
    } else {
        //These can be done without authentication
        if (request_type < 20) {
            printf("REQUEST DOES NOT REQUIRE AUTHENTICATION\n");
            if (request_type == 1) {
                if (strcmp(request_parameters, *prev_user_ptr) != 0){
                    *isAuthenticatedPtr = false;
                }
                //Need to validate input
                *userIndexPtr = find_user_index(users_database, request_parameters, num_users);
                printf("userIndex = %i\n", *userIndexPtr);
                //-1 indicates that the user was not found
                if (*userIndexPtr != -1) {
                    strcpy(*prev_user_ptr, request_parameters);
                    strcpy(response, USER_FOUND_RESPONSE);
                } else {
                    strcpy(response, USER_NOT_FOUND_RESPONSE);
                }
            } else if (request_type == 2) {
                strcpy(response, NOT_AUTHENTICATED_RESPONSE);
                if ((*userIndexPtr < num_users) && (*userIndexPtr >= 0) && (strcmp(request_parameters, users_database[*userIndexPtr].password) == 0)) {
                        strcpy(response, AUTHENTICATED_RESPONSE);
                        *isAuthenticatedPtr = true;
                }
            }
        } else {
        //These require authentication
            printf("REQUEST DOES REQUIRE AUTHENTICATION\n");
            if (*isAuthenticatedPtr == false) {
                strcpy(response, NOT_AUTHENTICATED_RESPONSE);
            } else {
                //Handling PORT
                if (request_type == 20) {
                    unsigned int h_1, h_2, h_3, h_4, p_1, p_2;
                    sscanf(request, PORT_REQUEST_FORMAT, &h_1, &h_2, &h_3, &h_4, &p_1, &p_2);
                    printf("h_1=%u | h_2=%u | h_3=%u | h_4=%u | p_1=%u | p_2=%u\n", h_1, h_2, h_3, h_4, p_1, p_2);
                    unsigned int client_ip_address = (h_1<<24) + (h_2<<16) + (h_3<<8) + h_4;
                    unsigned short client_port = (p_1 * 256) + p_2;
                    printf("UNSTORED PORT: %d\n", client_port);
                    client_address_ptr->sin_family = AF_INET;
                    client_address_ptr->sin_port = htons(client_port);
                    client_address_ptr->sin_addr.s_addr = htonl(client_ip_address);
                    printf("CLIENT IP ADDRESS: %s | CLIENT PORT: %d \n", inet_ntoa(client_address_ptr->sin_addr), ntohs(client_address_ptr->sin_port));
                    strcpy(response, PORT_SUCCESS_RESPONSE);
                    *isSetClientDetails_ptr = true;
                }
                //Handling STOR, RECV, LS
                else if (request_type == 21 || request_type == 22 || request_type == 23) {
                    //Should check if the file is valid or not to be sent
                    if (request_type == 22 && get_file_length(request_parameters) == -1) {
                        strcpy(response, FILE_TRANSFER_NULL_RESPONSE);
                    } else {
                        //A recieving file or ls command has no such need to be validated
                        stpcpy(response, FILE_TRANSFER_BEGIN_RESPONSE);
                        if(send(client_sd, response, strlen(response), 0)<0) {
                            perror("Response Send from Server to Client Failed");
                            //ADD SOMETHING FOR EXIT WEGHARA
                        }
                        printf("SENT GO AHEAD FOR FILE TRANSFER\n");
                        if (*isSetClientDetails_ptr == true) {
                            //This is the data control socket
                            int server_data_sd = create_and_bind_socket(SERVER_DATA_PORT, SERVER_IP_ADDRESS);
                            printf("SEEKING CONNECTION\n");
                            printf("CLIENT IP ADDRESS: %s | CLIENT PORT: %d \n", inet_ntoa(client_address_ptr->sin_addr), ntohs(client_address_ptr->sin_port));
                            //Connecting to the Server
                            if (connect(server_data_sd, (struct sockaddr *) client_address_ptr, sizeof(*client_address_ptr)))
                            {
                                printf("Server | Socket for Data Stream to Server | Connection Creation Failed: ");
                                return false;
                            }
                            printf("MADE CONNECTION\n");

                            struct sockaddr_in present_addr;
                            socklen_t present_addr_size = sizeof(present_addr);

                            if (getsockname(server_data_sd, (struct sockaddr*) &present_addr, &present_addr_size) != 0) {
                                printf("Could not get socket information.");
                                exit(-1);
                            } else {
                                printf("OWN PORT: %u | ADDRESS: %s\n", ntohs(present_addr.sin_port), inet_ntoa(present_addr.sin_addr));
                            }


                            //Recieve the file at the specified file address
                            bool success;
                            if (request_type == 21) {
                                success = recv_file(request_parameters, server_data_sd);
                            } else if (request_type == 22) {
                                success = send_file(request_parameters, server_data_sd);
                            } else {
                                //Will execute the ls command and store its output in a pipe
                                FILE *ls_file = popen("ls", "r");
                                if (ls_file == NULL) {
                                    printf("LIST COMMAND COULD NOT BE EXECUTED\n");
                                    success = false;
                                } else {
                                    char FILE_BUFFER[FILE_CHUNK_SIZE];
                                    int bytes_read = 0, bytes_sent, total_bytes_sent = 0;
                                    printf("BEGIN SENDING LIST CONTENTS\n");
                                    while ((bytes_read = fread(FILE_BUFFER, 1, FILE_CHUNK_SIZE, ls_file)) > 0) {
                                        bytes_sent = 0;
                                        while (bytes_sent < bytes_read) {
                                            bytes_sent += send(server_data_sd, FILE_BUFFER, bytes_read, 0);
                                        }
                                        total_bytes_sent += bytes_sent;
                                        printf("BYTES SENT TOTAL: %d | ITERATION BYTES SENT %d\n", total_bytes_sent, bytes_sent);
                                    }
                                    printf("END SENDING LIST CONTENTS\n");
                                    success = true;
                                    close(server_data_sd);
                                }
                                pclose(ls_file);
                            }

                            close(server_data_sd);
                            //Prepare the responses
                            bzero(response, RESPONSE_BUFFER_SIZE);
                            if (success == true) {
                                strcpy(response, FILE_TRANSFER_END_RESPONSE);
                            } else {
                                strcpy(response, FILE_TRANSFER_FAIL_RESPONSE);
                            }

                            //This ensures that the client details maybe not be reused across iterations without being explicitly set by the port command
                            *isSetClientDetails_ptr = false;
                        }
                    }
                }
                //Handling CWD
                else if (request_type == 24) {
                    if (chdir(request_parameters) != 0) {
                        printf("chdir() to %s failed", request_parameters);
                        strcpy(response, CWD_FAILURE_RESPONSE);
                    } else {
                        snprintf(response, RESPONSE_BUFFER_SIZE, CWD_SUCCESS_RESPONSE_FORMAT, request_parameters);
                    }
                } else if (request_type == 25){
                    //Handling PWD
                    bzero(path, PATH_BUFFER_SIZE);
                    if (getcwd(path, PATH_BUFFER_SIZE) != NULL) {
                        printf("Current working dir: %s\n", path);
                        snprintf(response, RESPONSE_BUFFER_SIZE, PWD_SUCCESS_RESPONSE_FORMAT, path);
                    } else {
                        perror("getcwd() error");
                        strcpy(response, PWD_FAILURE_RESPONSE);
                    }
                } 
            }
        }
    }
    return true;
}

struct user* user_handler(char* filename, int* user_count_ptr) {
    struct user temp_user_records[MAXUSERCOUNT];
    char line[LINELENGTH];
    char *token;
    int user_count = 0;

    FILE *fp;
    fp = fopen(filename, "r");

    if (fp == NULL) {
        printf("Requested file <%s> containing user details could not be found\n", filename);
        return NULL;
    }

    printf("USERS Database\n");
    while (feof(fp) != true){
        fgets(line, LINELENGTH, fp);
        line[strcspn(line, "\n")] = 0;  //remove trailing newline char from the line, fgets does not remove it
        token = strtok(line, ", ");
        strcpy(temp_user_records[user_count].user_name, token);
        token = strtok(NULL, ", ");
        strcpy(temp_user_records[user_count].password, token);
        printf("\tUser[%i].user_name = %s | User[%i].password = %s\n", user_count, temp_user_records[user_count].user_name, user_count, temp_user_records[user_count].password);
        user_count++;
    }

    struct user* user_records = malloc(user_count * sizeof(struct user));
    for (int i = 0; i < user_count; i++) {
        strcpy(user_records[i].user_name, temp_user_records[i].user_name);
        strcpy(user_records[i].password, temp_user_records[i].password);
    }

    *user_count_ptr = user_count;
    printf("USER DATABASE CONTAINS %i RECORDS\n", *user_count_ptr);

    return user_records;
}

int main()
{
    int server_sd = create_and_bind_socket(SERVER_CONTROL_PORT, SERVER_IP_ADDRESS);

	//Listening on the Server Socket
	if(listen(server_sd,LISTEN_BACKLOG)<0)
	{
		perror("Listen Error:");
		return -1;
	}

    //Creating a structure to hold all the details to the given client with whom a connection is accepted
	struct sockaddr_in client_addr;
	socklen_t client_addr_size = sizeof(client_addr);

    //Reseting the structure containing the client address
    bzero(&client_addr, sizeof(client_addr));

    while (true) {
        printf("Master Process | Waiting for Accept\n");
        //Accepting an incoming client connection off of the Listen Queue
        int client_sd = accept(server_sd, (struct sockaddr*) &client_addr, &client_addr_size);
        printf("Master Process | End Waiting for Accept\n");
        //Spawn a Child Process to have it handle the new connection
        if (fork() == 0) {
            if (client_sd < 0) {
                perror("Accept Failed");
                close(server_sd);
                exit(-1);
            }

            //Indicating Success in being Connected to the particular client
            printf("Child Process | [%s:%d] CONNECTED WITH CLIENT\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

            //This dictates if there is yet ongoing communication between the client and the server
            bool isConnected = true;
            //These will be used to buffer the response before it is sent and the request from the client before it is processed, and to store the path temporarily
            char RESPONSE_BUFFER[RESPONSE_BUFFER_SIZE];
            char REQUEST_BUFFER[REQUEST_BUFFER_SIZE];
            char PATH_BUFFER[PATH_BUFFER_SIZE];
            char* PREV_USER = malloc(MAXLENGTH);

            //Initial Values
            bzero(RESPONSE_BUFFER, RESPONSE_BUFFER_SIZE);
            strcpy(RESPONSE_BUFFER, READY_RESPONSE);
            bzero(REQUEST_BUFFER, REQUEST_BUFFER_SIZE);
            strcpy(REQUEST_BUFFER, "");
            bzero(PREV_USER, MAXLENGTH);
            strcpy(PREV_USER, "");

            //To maintain state of whether one is authenticated or not
            bool isAuthenticated = false;
            //This is an index used to identify the user
            int userIndex = -1;
            //This is the number of users in the database
            int num_users = 0;
            //This is a database used to maintain all the users and their login details
            struct user* user_database = user_handler(USERFILENAME, &num_users);

            //Sending out the Server's Initial Response
            if(send(client_sd,RESPONSE_BUFFER,strlen(RESPONSE_BUFFER),0)<0) {
                perror("Response Send from Server to Client Failed");
                //ADD SOMETHING FOR EXIT WEGHARA
            }
            //This is used to maintain client address details for the data connection
            struct sockaddr_in client_address_data_connection;
            //This is used to maintain whether or not client address details have been set
            bool isSetClientDetails = false;

            while(isConnected){
                bzero(REQUEST_BUFFER, REQUEST_BUFFER_SIZE);
                int bytes_recieved = recv(client_sd,REQUEST_BUFFER,sizeof(REQUEST_BUFFER),0);
                if (bytes_recieved < 0) {
                    printf("Reciept of Request Failed");
                    //ADD SOMETHING FOR EXIT WEGHARA
                }
                //Getting a New Response based on Request
                isConnected = process_query(REQUEST_BUFFER, RESPONSE_BUFFER, PATH_BUFFER, &PREV_USER, user_database, &isAuthenticated, &userIndex, num_users, &client_address_data_connection, &isSetClientDetails, client_sd);
                printf("RESPONSE TO CLIENT: <%s>\n", RESPONSE_BUFFER);
                //Sending out the Server's Latest Response
                if(send(client_sd,RESPONSE_BUFFER,strlen(RESPONSE_BUFFER),0)<0) {
                    perror("Response Send from Server to Client Failed");
                    //ADD SOMETHING FOR EXIT WEGHARA
                }
            }

            free(PREV_USER);
            close(client_sd);
            close(server_sd);
        }
    }

    close(server_sd);

    return 0;
}