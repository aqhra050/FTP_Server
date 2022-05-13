#ifndef COMMON
#define COMMON

#define SERVER_CONTROL_PORT 6000                                    // should be 21
#define SERVER_DATA_PORT 10000                                       // should be 20, will change later
#define SERVER_IP_ADDRESS "127.0.0.2"                               // should be 127.0.0.1
#define RESPONSE_BUFFER_SIZE 288
#define REQUEST_BUFFER_SIZE 128
#define PATH_BUFFER_SIZE 256
//These are the response strings from the server
#define READY_RESPONSE "220 Service ready for new user."
#define QUIT_RESPONSE "221 Service closing control connection."
#define USER_FOUND_RESPONSE "331 Username OK, need password."
#define USER_NOT_FOUND_RESPONSE "420 Username NOT OK."              //self-made
#define AUTHENTICATED_RESPONSE "230 User logged in, proceed."
#define NOT_AUTHENTICATED_RESPONSE "530 Not logged in."
#define INVALID_COMMAND_RESPONSE "202 Command not implemented."
#define PWD_SUCCESS_RESPONSE_FORMAT "257 %s."
#define PWD_FAILURE_RESPONSE "PWD Command Failed."
#define CWD_SUCCESS_RESPONSE_FORMAT "200 directory changed to %s."
#define CWD_FAILURE_RESPONSE "CWD Command Failed."
#define PORT_REQUEST_FORMAT "PORT %u,%u,%u,%u,%u,%u"
#define PORT_SUCCESS_RESPONSE "200 PORT command successful."
#define FILE_TRANSFER_BEGIN_RESPONSE "150 File status okay; about to open data connection."
#define FILE_TRANSFER_NULL_RESPONSE "550 No such file or directory."
#define FILE_TRANSFER_FAIL_RESPONSE "222 File Transfer Failed."      //self-made
#define FILE_TRANSFER_END_RESPONSE "226 Transfer completed."
#define FILE_CHUNK_SIZE 1500
#define MAXLENGTH 128
#define LINELENGTH 257
#define MAXUSERCOUNT 50
#define USERFILENAME "users.txt"

struct user
{
    char user_name[MAXLENGTH];
    char password[MAXLENGTH];
};

int get_request_type(char* request, char** request_parameters_ptr);
int get_file_length(char* fileName);
bool send_file(char* file_name_str, int connection_sd);
bool recv_file(char* file_name_str, int connection_sd);
int create_and_bind_socket(int port_number, char* ip_addr_str);

//Number of clients that will be held in backlog at the listen stage
#define LISTEN_BACKLOG 10

#endif