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

#define SERVER_PORT 8080
#define SERVER_ADDRESS "127.0.0.1"
//Number of clients that will be held in backlog at the listen stage
#define LISTEN_BACKLOG 10

//Size in Bytes of Buffer for a Line of Request from Client
#define REQUEST_BUFFER_SIZE 256

//Size in Bytes of Maximum Header for Responses
#define HEADER_BUFFER_SIZE 1024
//Maximum Number of Characters for Time
#define TIME_BUFFER_SIZE 64

#define OK_MSG "OK"
#define NOT_FOUND_MSG "Not Found"

#define SUCCESS_STATUS_CODE 200
#define NOT_FOUND_STATUS_CODE 404

#define ROOT_PATH "./"
#define	ROOT_WEB_PAGE "./index.html"
#define ROOT_WEB_PAGE_LEN 13
#define NOT_FOUND_WEB_PAGE "./redirect.html"
#define NOT_FOUND_WEB_PAGE_LEN 16

#define FILE_CHUNK_SIZE 1460

//Content Types for Various File Extensions
#define HTML_EXTENSION "html"
#define HTML_CONTENT_TYPE "text/html"
#define HTML_CONTENT_LEN 10
#define JS_EXTENSION "js"
#define JS_CONTENT_TYPE "application/javascript"
#define JS_CONTENT_LEN 23
#define CSS_EXTENSION "css"
#define CSS_CONTENT_TYPE "text/css"
#define CSS_CONTENT_LEN 9
#define PNG_EXTENSION "png"
#define PNG_CONTENT_TYPE "image/png"
#define PNG_CONTENT_LEN 10
#define CONTENT_TYPE_MAX_LEN 32

//Time formats
#define LOG_TIME_FORMAT "%H:%M:%S %d/%m/%Y"
#define RESPONSE_TIME_FORMAT "%H:%M:%S %a %b %d %Y"

struct parameters {
	bool is_first;
	int client_sd;
	struct sockaddr_in client_address;
	pthread_t previous_thread;
};

//Thread Level Function will handle a single TCP connect following the client's acceptance by the server
void* processClient(void* void_parameters_ptr);
//Checks whether or not the request file is present or not
bool isFilePresent(char* fileName);
//Will get the file length of the file with name, fileName, if said file is present and -1 otherwise
int getFileLength(char* fileName);
//Generates the header per the given parameters
char* generateHeader(int status_code, char* msg, char* content_type, int content_length);
//Gets the file path
char* getPath(const char* line);
//Gets the extension from file path
char* getExtension(const char* path);
//Generates the Content-Type
char* getContentType(const char* extension);
//Used to generate a string of the present per the given format
void setTime(char* time_str, char* format_str);

//This mutex will be used to guard entry into thread-creation, for time sufficient that the arguments to the thread function are copied over locally before another thread is created with a different set of arguements such that there is no error in the arguements passed
//This has not been shown to be needed in testing, but represents a logical need
sem_t mutex;

int main()
{
	//Creation of Server Socket
	int server_sd = socket(AF_INET,SOCK_STREAM,0);
	if(server_sd<0)
	{
		perror("Socket Failed: ");
		return -1;
	}

	//Delineating Port and Address
	struct sockaddr_in server_address;	//structure to save IP address and port
	memset(&server_address,0,sizeof(server_address)); //Initialize/fill the server_address to 0
	server_address.sin_family = AF_INET;	//address family
	server_address.sin_port = htons(SERVER_PORT);	//port
	server_address.sin_addr.s_addr = INADDR_ANY;

	//Permitting Re-Use of the Port, if previous Server instances still occupies it
	setsockopt(server_sd,SOL_SOCKET,SO_REUSEADDR,&(int){1},sizeof(int));

	//Checking if Bind Succeeded or Failed
	if(bind(server_sd,(struct sockaddr *)&server_address,sizeof(server_address))<0) 
	{
		perror("Bind failed..:");
		return -1;
	}

	//Listening on the Server Socket
	if(listen(server_sd,LISTEN_BACKLOG)<0)
	{
		perror("Listen Error:");
		return -1;
	}

	//Displaying the thread id of the root process so that it becomes evident that there is threading occuring
	pthread_t prev_tid = pthread_self();
	printf("ROOT PROCESS HAS THREAD ID %ld\n", prev_tid);

	//A variable that shall be shared acrosss all threads by reference for their function invocation, hence its value must immediately be stored
	struct parameters thread_parameters;
	//This parameter will be used to delineate the first invocationn of the thread function for only it does not need to wait for any previous threads to be concluded with their sending before it may send
	thread_parameters.is_first = true;

	//This is a mutex to prevent spawning of another thread while the arguements continue to be unread in the present invocation of the thread function
	if (sem_init(&mutex, 0, 0) < -1) {
		printf("MUTEX INITIALIZATION FAILED\n");
		exit(1);
	}

	//4. accept
	while(true)
	{
		int client_len = sizeof(thread_parameters.client_address);
		//Inserting Values into the thread_parameters and accepting the client
		thread_parameters.client_sd = accept(server_sd,(struct sockaddr*)&thread_parameters.client_address,(socklen_t *)(&client_len));
		
		//client_sd will need to be given to the thread function, if valid and if not an accept error will be raised
		if(thread_parameters.client_sd < 1)
		{
			perror("Accept Error:");
			return -1;
		}
		else
		{
			//Each thread except the will wait for the previous thread to be concluded from its sending, before it sends, the previus thread is maintained in this variable 
			thread_parameters.previous_thread = prev_tid;
			pthread_create(&prev_tid, NULL, processClient, (void*) &thread_parameters);
			//the mutex is used to ensure sufficient time for the parameters to be read by the thread,  before we spawn a new one and alter the parameters
			printf("MAIN THREAD | BEGIN WAIT FOR PARAMETERS READ\n");
			sem_wait(&mutex);
			printf("MAIN THREAD | END   WAIT FOR PARAMETERS READ\n");
			thread_parameters.is_first = false;
		}
	}
	//Closing and Destroying the Mutex	
	sem_destroy(&mutex);
	//Close the server socket
	close(server_sd);										
}

//Thread Level Function will handle a single TCP connect following the client's acceptance by the server
void* processClient(void* void_parameters_ptr) {
	//All the parameters from the thread shared pointer are now stored locally, and the mutex is posted, the posting of the mutex permits the creation of a new thread
	struct parameters* parameters_ptr = (struct parameters*) void_parameters_ptr;
	bool is_first = parameters_ptr->is_first;
	int client_sd = parameters_ptr->client_sd;
	pthread_t prev_tid = parameters_ptr->previous_thread;
	pthread_t curr_tid = pthread_self();
	struct sockaddr_in client_address = parameters_ptr->client_address;
	printf("NEW CLIENT THREAD | THREAD-ID: %ld | ENTRY\n", curr_tid);
	sem_post(&mutex);
	printf("NEW CLIENT THREAD | THREAD-ID: %ld | PARAMETER READ COMPLETE\n", curr_tid);

	char *ip_address_str = inet_ntoa(client_address.sin_addr);

	//Procesing the GET Request performing adjustments to it if it is the root request or an unpresent resources has been requested
	FILE *sock_stream = fdopen(client_sd,"r+");		//open the socket as a file stream
	if(sock_stream== NULL)
	{
		perror("CAN NOT OPEN SOCKET AS FILE STREAM FOR RECV");
		exit(-1);
	}

	char request_buffer[REQUEST_BUFFER_SIZE];
	bzero(request_buffer, sizeof(request_buffer));
	//Read the first line of the request
	fgets(request_buffer, sizeof(request_buffer), sock_stream);
	//Strip the New-Line from the End of the line of the request read in
	request_buffer[strcspn(request_buffer, "\r\n")] = '\0';

	//Getting the Time as a String
	char time_str[TIME_BUFFER_SIZE];
	setTime(time_str, LOG_TIME_FORMAT);

	printf("THREAD-ID: %ld | CLIENT:[%s] | %s | \"%s\" | REQUEST RECIEVED\n", curr_tid, ip_address_str, time_str, request_buffer);

	char* file_name = getPath(request_buffer);
	printf("\tCURRENT THREAD: %ld", curr_tid);
	printf("\tREQUEST PATH: <%s>\n", file_name);

	//If the requested file path is "/" we swap it out out for the index.html page
	//It is somewhat messy due to the nature of strings in C
	if (strcmp(file_name, ROOT_PATH) == 0) {
		printf("\tCURRENT THREAD: %ld", curr_tid);
		printf("\tROOT_PATH REQUEST\n");
		free(file_name);
		file_name = malloc(ROOT_WEB_PAGE_LEN);
		strcpy(file_name, ROOT_WEB_PAGE);
		file_name[ROOT_WEB_PAGE_LEN] = '\0';
		printf("\tCURRENT THREAD: %ld", curr_tid);
		printf("\tMODIFIED REQUEST PATH: <%s>\n", file_name);
	}
	bool is_requested_file_present = isFilePresent(file_name);

	//For the header
	int statusCode;
	char* header;

	//If the requested file is not present we display the 404 Error Page and set the status code 404, otherwise the status code is 200
	if (!is_requested_file_present) {
		printf("\tCURRENT THREAD: %ld", curr_tid);
		printf("\tNOT_FOUND REDIRECT\n");
		free(file_name);
		file_name = malloc(NOT_FOUND_WEB_PAGE_LEN);
		strcpy(file_name, NOT_FOUND_WEB_PAGE);
		file_name[NOT_FOUND_WEB_PAGE_LEN] = '\0';
		printf("\tCURRENT THREAD: %ld", curr_tid);
		printf("\tMODIFIED REQUEST PATH: <%s>\n", file_name);
		statusCode = NOT_FOUND_STATUS_CODE;
	} else {
		statusCode = SUCCESS_STATUS_CODE;
	}

	printf("\tCURRENT THREAD: %ld", curr_tid);
	//We get the Content-Type
	char* content_type = getContentType(file_name);
	printf("\tCURRENT THREAD: %ld", curr_tid);
	printf("\tCONTENT-TYPE: <%s>\n", content_type);

	//Getting the File-Size
	int file_size = getFileLength(file_name);
	//Generate the header
	header = generateHeader(statusCode, statusCode == 200 ? OK_MSG : NOT_FOUND_MSG, content_type, file_size);
	int header_file_size = strlen(header);

	//Generating the Complete Response in its entirety, and appending the header informaation before adding the rest (response body)
	int complete_file_size = header_file_size + file_size;
	char *file_contents = malloc(complete_file_size);
	bzero(file_contents, complete_file_size);
	strcat(file_contents, header);
	//Open the 'file_name'
	FILE *fptr = fopen(file_name,"r");
	//Create a buffer to store 'file_name'
	//Read the 'file_name' and store in file_contents buffer
	int body_bytes_read = fread(file_contents + header_file_size, 1, file_size, fptr);
	if (body_bytes_read != file_size) {
		printf("PROBLEM IN FILE READ: MEMORY LOAD\n");
	} else {
		printf("\tCURRENT THREAD: %ld", curr_tid);
		printf("\tFILE \"%s\" LOADED INTO MEMORY\n", file_name);
	}

	//Pausing for the previous thread to complete its sending should there be one
	if (is_first == true) {
		printf("\tCURRENT THREAD: %ld", curr_tid);
		printf("\tWILL NOT WAIT FOR PREVIOUS | NO PREVIOUS\n");
	} else {
		sleep(1);
		printf("\tCURRENT THREAD: %ld", curr_tid);
		printf("\tWILL WAIT FOR PREVIOUS\n");
		pthread_join(prev_tid, NULL);
		printf("\tWAIT FOR PREVIOUS COMPLETE\n");
	}

	//This sends out the file in chunks, and represents a fix to the problem I had been having with the original webserver.c
	unsigned int bytes_sent = 0;
	int send_bytes = 0;
	while(bytes_sent < complete_file_size)
	{
		if(complete_file_size - bytes_sent >= FILE_CHUNK_SIZE)
			send_bytes = FILE_CHUNK_SIZE;
		else
			send_bytes = complete_file_size - bytes_sent;
		send(client_sd, file_contents + bytes_sent, send_bytes, 0);
		bytes_sent += send_bytes;
	}

	//For Logging Purposes of Response
	setTime(time_str, LOG_TIME_FORMAT);
	printf("THREAD-ID: %ld | CLIENT:[%s] | %s | \"%s\" HAS BEEN RESPONDED WITH %d\n", curr_tid, ip_address_str, time_str, request_buffer, statusCode);


	//Remove the dynamic memory
	free(content_type);
	free(file_name);
	free(file_contents);
	free(header);

	//Flush and close the socket stream
	fflush(sock_stream);
	fclose(sock_stream);	

	//Close the client/secondary socket
	close(client_sd);

	//Exit the thread
	pthread_exit(NULL);
}

//Will get the file length of the file with name, fileName, if said file is present and -1 otherwise
int getFileLength(char* fileName) {
	//No need for any checks on whether the file is present or not, following the isFilePresent check
	FILE *fptr = fopen(fileName,"r");
	fseek(fptr,0,SEEK_END);						//move the file pointer to the end of the file
	int file_size = ftell(fptr);				//read the position of the filepointer which will be equal to the size of the file
	fclose(fptr);
	return file_size;
}

//Will generate a string that represents the header for the response
char* generateHeader(int status_code, char* msg, char* content_type, int content_length) {
	char* header = malloc(HEADER_BUFFER_SIZE);

	//Getting the Time as a String
	char time_str[TIME_BUFFER_SIZE];
	time_t present_time;
	struct tm result;
	struct tm* present_time_TM = NULL;

	time(&present_time);
	present_time_TM = localtime_r(&present_time, &result);
	strftime(time_str, TIME_BUFFER_SIZE, "%H:%M:%S %a %b %d %Y", present_time_TM);

	snprintf(header, HEADER_BUFFER_SIZE, "HTTP/1.0 %3d %s\r\nServer: SimpleHTTPServer\r\nDate: %s\r\nContent-Type: %s\r\nContent-Length: %d\r\n\r\n", status_code, msg, time_str, content_type, content_length);

	return header;
}

//Checks whether or not the request file is present or not, return a boolean
bool isFilePresent(char* fileName) {
	FILE *fptr = fopen(fileName,"r");
	if(fptr == NULL)
	{
		printf("\tREQUESTED <%s> ABSENT\n", fileName);
		return false;
	} else {
		return true;
	}
}

//An adaptation from the below link
//https://stackoverflow.com/questions/41286260/parse-http-request-line-in-c
//Parses the file path from the first line of request 
char* getPath(const char* line)
{
	//Boundary Indexes
    const char *start_of_path = strchr(line, ' ') + 1;
	// printf("start_of_path: <%s>\n", start_of_path);
    const char *end_of_path = strchr(start_of_path, ' ');
	// printf("end_of_path: <%s>\n", end_of_path);

    //Allocating memory
    char* path = malloc(end_of_path - start_of_path + 2);
	// printf("size: %ld\n", end_of_path - start_of_path + 2);

    //Storing into memory and adding the . for relative access
	path[0] = '.';
    strncpy(path + 1, start_of_path,  end_of_path - start_of_path);

    //Null terminator
    path[end_of_path - start_of_path + 1] = '\0';

    return path;
}

//Gets the extension from file path string
char* getExtension(const char* path) {
	//Boundary Indexes
    const char *first_dot = strchr(path, '.') + 1;
    const char *second_dot = strchr(first_dot, '.') + 1;
	int path_len = strlen(path);

    //Allocating memory
	int extension_len = (path + path_len) - second_dot + 1;
    char* extension = malloc(extension_len);

	//Storing into Memory
    strncpy(extension, second_dot,  extension_len);

    //Null terminator
    extension[extension_len] = '\0';

    return extension;
}

//Generates the Content-Type from the file path string
char* getContentType(const char* path) {
	char* extension = getExtension(path);
	printf("\tEXTENSION: <%s>\n", extension);

	char* content_type = malloc(CONTENT_TYPE_MAX_LEN);

	if (strcmp(extension, HTML_EXTENSION) == 0) {
		strcpy(content_type, HTML_CONTENT_TYPE);
		content_type[HTML_CONTENT_LEN] = '\0';
	} else if (strcmp(extension, JS_EXTENSION) == 0) {
		strcpy(content_type, JS_CONTENT_TYPE);
		content_type[JS_CONTENT_LEN] = '\0';
	} else if (strcmp(extension, CSS_EXTENSION) == 0) {
		strcpy(content_type, CSS_CONTENT_TYPE);
		content_type[CSS_CONTENT_LEN] = '\0';
	} else if (strcmp(extension, PNG_EXTENSION) == 0) {
		strcpy(content_type, PNG_CONTENT_TYPE);
		content_type[PNG_CONTENT_LEN] = '\0';
	} else {
		printf("INVALID EXTENSION");
		free(content_type);
		content_type = NULL;
	}

	free(extension);

	return content_type;
}

//Used to generate a string of the present per the given format string
void setTime(char* time_str, char* format_str) {
	time_t present_time;
	struct tm result;
	struct tm* present_time_TM = NULL;

	time(&present_time);
	present_time_TM = localtime_r(&present_time, &result);
	strftime(time_str, TIME_BUFFER_SIZE, format_str, present_time_TM);
} 