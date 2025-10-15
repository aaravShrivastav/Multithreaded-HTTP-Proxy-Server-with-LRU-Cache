/*
 * HTTP Proxy Server with Caching
 * Features:
 * - Multi-threaded request handling
 * - LRU cache implementation
 * - HTTP/1.0 and HTTP/1.1 support
 * - Error handling for various HTTP status codes
 */

#include "proxy_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>

// Configuration constants
#define MAX_BYTES 4096              // Maximum allowed size of request/response
#define MAX_CLIENTS 400             // Maximum number of concurrent client requests
#define MAX_SIZE 200*(1<<20)        // Total cache size (200MB)
#define MAX_ELEMENT_SIZE 10*(1<<20) // Maximum size of a single cache element (10MB)

// Cache element structure for linked list implementation
typedef struct cache_element cache_element;

struct cache_element{
    char* data;                     // Stores the HTTP response data
    int len;                        // Length of the response data
    char* url;                      // Stores the request URL (cache key)
    time_t lru_time_track;          // Timestamp for LRU replacement policy
    cache_element* next;            // Pointer to next element in linked list
};

// Function declarations
cache_element* find(char* url);
int add_cache_element(char* data,int size,char* url);
void remove_cache_element();

// Global variables
int port_number = 8080;             // Default proxy server port
int proxy_socketId;                 // Socket descriptor for proxy server
pthread_t tid[MAX_CLIENTS];         // Array to store thread IDs of client handlers
sem_t seamaphore;                   // Semaphore to limit concurrent connections
pthread_mutex_t lock;               // Mutex for thread-safe cache operations

cache_element* head = NULL;         // Head pointer for cache linked list (initialized)
int cache_size = 0;                 // Current total size of cache (initialized)

/*
 * Function: sendErrorMessage
 * Purpose: Send appropriate HTTP error responses to client
 * Parameters: 
 *   - socket: client socket descriptor
 *   - status_code: HTTP status code to send
 * Returns: 1 on success, -1 on invalid status code
 */
int sendErrorMessage(int socket, int status_code)
{
    char str[1024];                 // Buffer for HTTP response
    char currentTime[50];           // Buffer for timestamp
    time_t now = time(0);           // Get current time

    // Format current time for HTTP Date header
    struct tm data = *gmtime(&now);
    strftime(currentTime,sizeof(currentTime),"%a, %d %b %Y %H:%M:%S %Z", &data);

    // Generate appropriate HTTP error response based on status code
    switch(status_code)
    {
        case 400: // Bad Request
            snprintf(str, sizeof(str), 
                "HTTP/1.1 400 Bad Request\r\n"
                "Content-Length: 95\r\n"
                "Connection: keep-alive\r\n"
                "Content-Type: text/html\r\n"
                "Date: %s\r\n"
                "Server: VaibhavN/14785\r\n\r\n"
                "<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\n"
                "<BODY><H1>400 Bad Request</H1>\n</BODY></HTML>", 
                currentTime);
            printf("400 Bad Request\n");
            send(socket, str, strlen(str), 0);
            break;

        case 403: // Forbidden
            snprintf(str, sizeof(str), 
                "HTTP/1.1 403 Forbidden\r\n"
                "Content-Length: 112\r\n"
                "Content-Type: text/html\r\n"
                "Connection: keep-alive\r\n"
                "Date: %s\r\n"
                "Server: VaibhavN/14785\r\n\r\n"
                "<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\n"
                "<BODY><H1>403 Forbidden</H1><br>Permission Denied\n</BODY></HTML>", 
                currentTime);
            printf("403 Forbidden\n");
            send(socket, str, strlen(str), 0);
            break;

        case 404: // Not Found
            snprintf(str, sizeof(str), 
                "HTTP/1.1 404 Not Found\r\n"
                "Content-Length: 91\r\n"
                "Content-Type: text/html\r\n"
                "Connection: keep-alive\r\n"
                "Date: %s\r\n"
                "Server: VaibhavN/14785\r\n\r\n"
                "<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\n"
                "<BODY><H1>404 Not Found</H1>\n</BODY></HTML>", 
                currentTime);
            printf("404 Not Found\n");
            send(socket, str, strlen(str), 0);
            break;

        case 500: // Internal Server Error
            snprintf(str, sizeof(str), 
                "HTTP/1.1 500 Internal Server Error\r\n"
                "Content-Length: 115\r\n"
                "Connection: keep-alive\r\n"
                "Content-Type: text/html\r\n"
                "Date: %s\r\n"
                "Server: VaibhavN/14785\r\n\r\n"
                "<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\n"
                "<BODY><H1>500 Internal Server Error</H1>\n</BODY></HTML>", 
                currentTime);
            printf("500 Internal Server Error\n");
            send(socket, str, strlen(str), 0);
            break;

        case 501: // Not Implemented
            snprintf(str, sizeof(str), 
                "HTTP/1.1 501 Not Implemented\r\n"
                "Content-Length: 103\r\n"
                "Connection: keep-alive\r\n"
                "Content-Type: text/html\r\n"
                "Date: %s\r\n"
                "Server: VaibhavN/14785\r\n\r\n"
                "<HTML><HEAD><TITLE>501 Not Implemented</TITLE></HEAD>\n"
                "<BODY><H1>501 Not Implemented</H1>\n</BODY></HTML>", 
                currentTime);
            printf("501 Not Implemented\n");
            send(socket, str, strlen(str), 0);
            break;

        case 505: // HTTP Version Not Supported
            snprintf(str, sizeof(str), 
                "HTTP/1.1 505 HTTP Version Not Supported\r\n"
                "Content-Length: 125\r\n"
                "Connection: keep-alive\r\n"
                "Content-Type: text/html\r\n"
                "Date: %s\r\n"
                "Server: VaibhavN/14785\r\n\r\n"
                "<HTML><HEAD><TITLE>505 HTTP Version Not Supported</TITLE></HEAD>\n"
                "<BODY><H1>505 HTTP Version Not Supported</H1>\n</BODY></HTML>", 
                currentTime);
            printf("505 HTTP Version Not Supported\n");
            send(socket, str, strlen(str), 0);
            break;

        default:  
            return -1; // Invalid status code
    }
    return 1;
}

/*
 * Function: connectRemoteServer
 * Purpose: Establish connection to the target web server
 * Parameters:
 *   - host_addr: hostname or IP address of target server
 *   - port_num: port number of target server
 * Returns: socket descriptor on success, -1 on failure
 */
int connectRemoteServer(char* host_addr, int port_num)
{
    // Create TCP socket for connecting to remote server
    int remoteSocket = socket(AF_INET, SOCK_STREAM, 0);

    if( remoteSocket < 0)
    {
        printf("Error in Creating Socket.\n");
        return -1;
    }
    
    // Resolve hostname to IP address
    struct hostent *host = gethostbyname(host_addr);    
    if(host == NULL)
    {
        fprintf(stderr, "No such host exists.\n");    
        return -1;
    }

    // Setup server address structure
    struct sockaddr_in server_addr;
    bzero((char*)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;                               // IPv4
    server_addr.sin_port = htons(port_num);                         // Convert port to network byte order

    // Copy IP address from hostent structure
    bcopy((char *)host->h_addr,(char *)&server_addr.sin_addr.s_addr,host->h_length);

    // Establish connection to remote server
    if( connect(remoteSocket, (struct sockaddr*)&server_addr, (socklen_t)sizeof(server_addr)) < 0 )
    {
        fprintf(stderr, "Error in connecting !\n"); 
        return -1;
    }
    
    return remoteSocket;
}

/*
 * Function: handle_request
 * Purpose: Forward client request to target server and relay response back
 * Parameters:
 *   - clientSocket: socket connected to client
 *   - request: parsed HTTP request structure
 *   - tempReq: original request string for caching
 * Returns: 0 on success, -1 on failure
 */
int handle_request(int clientSocket, ParsedRequest *request, char *tempReq)
{
    // Allocate buffer for constructing HTTP request
    char *buf = (char*)malloc(sizeof(char)*MAX_BYTES);
    
    // Construct HTTP request line (GET /path HTTP/1.1)
    strcpy(buf, "GET ");
    strcat(buf, request->path);
    strcat(buf, " ");
    strcat(buf, request->version);
    strcat(buf, "\r\n");

    size_t len = strlen(buf);

    // Set Connection header to close (HTTP/1.0 style)
    if (ParsedHeader_set(request, "Connection", "close") < 0){
        printf("set header key not work\n");
    }

    // Ensure Host header is present (required for HTTP/1.1)
    if(ParsedHeader_get(request, "Host") == NULL)
    {
        if(ParsedHeader_set(request, "Host", request->host) < 0){
            printf("Set \"Host\" header key not working\n");
        }
    }

    // Add all headers to the request buffer
    if (ParsedRequest_unparse_headers(request, buf + len, (size_t)MAX_BYTES - len) < 0) {
        printf("unparse failed\n");
    }

    // Determine target server port (default HTTP port is 80)
    int server_port = 80;
    if(request->port != NULL)
        server_port = atoi(request->port);

    // Connect to target server
    int remoteSocketID = connectRemoteServer(request->host, server_port);

    if(remoteSocketID < 0)
    {
        free(buf);
        return -1;
    }

    // Send HTTP request to target server
    int bytes_send = send(remoteSocketID, buf, strlen(buf), 0);
    if(bytes_send < 0)
    {
        free(buf);
        close(remoteSocketID);
        return -1;
    }

    bzero(buf, MAX_BYTES); // Clear buffer for receiving response

    // Receive response from target server
    bytes_send = recv(remoteSocketID, buf, MAX_BYTES-1, 0);
    
    // Allocate buffer to store complete response for caching
    char *temp_buffer = (char*)malloc(sizeof(char)*MAX_BYTES);
    int temp_buffer_size = MAX_BYTES;
    int temp_buffer_index = 0;

    // Relay response from server to client while storing in cache buffer
    while(bytes_send > 0)
    {
        // Forward data to client
        int client_send_result = send(clientSocket, buf, bytes_send, 0);
        
        // Copy data to cache buffer with bounds checking
        for(int i=0; i<bytes_send && temp_buffer_index < temp_buffer_size-1; i++){
            temp_buffer[temp_buffer_index] = buf[i];
            temp_buffer_index++;
        }
        
        // Expand cache buffer if needed
        if(temp_buffer_index >= temp_buffer_size - MAX_BYTES)
        {
            temp_buffer_size += MAX_BYTES;
            temp_buffer = (char*)realloc(temp_buffer, temp_buffer_size);
            if(temp_buffer == NULL)
            {
                printf("Memory allocation failed\n");
                break;
            }
        }

        if(client_send_result < 0)
        {
            perror("Error in sending data to client socket.\n");
            break;
        }
        
        bzero(buf, MAX_BYTES);
        bytes_send = recv(remoteSocketID, buf, MAX_BYTES-1, 0); // Continue receiving
    } 
    
    // Null-terminate the cached response
    temp_buffer[temp_buffer_index] = '\0';
    free(buf);
    
    // Add response to cache if we received data
    if(temp_buffer_index > 0)
    {
        add_cache_element(temp_buffer, temp_buffer_index, tempReq);
    }
    
    printf("Done\n");
    free(temp_buffer);
    
    close(remoteSocketID);
    return 0;
}

/*
 * Function: checkHTTPversion
 * Purpose: Validate HTTP version in request
 * Parameters: msg - HTTP version string
 * Returns: 1 for supported versions, -1 for unsupported
 */
int checkHTTPversion(char *msg)
{
    int version = -1;

    if(strncmp(msg, "HTTP/1.1", 8) == 0)
    {
        version = 1; // HTTP/1.1 supported
    }
    else if(strncmp(msg, "HTTP/1.0", 8) == 0)            
    {
        version = 1; // HTTP/1.0 treated same as 1.1
    }
    else
        version = -1; // Unsupported version

    return version;
}

/*
 * Function: thread_fn
 * Purpose: Main thread function to handle individual client requests
 * Parameters: socketNew - pointer to client socket descriptor
 * Returns: NULL (thread return value)
 */
void* thread_fn(void* socketNew)
{
    // Wait for semaphore (connection slot)
    sem_wait(&seamaphore); 
    int p;
    sem_getvalue(&seamaphore,&p);
    printf("semaphore value:%d\n",p);
    
    // Extract socket descriptor and free the allocated memory
    int* t = (int*)(socketNew);
    int socket = *t;
    free(t); // FIXED: Free the malloc'd socket pointer to prevent memory leak
    
    int bytes_send_client, len;

    // Allocate buffer for client request
    char *buffer = (char*)calloc(MAX_BYTES, sizeof(char));
    
    bzero(buffer, MAX_BYTES);
    // Receive initial data from client
    bytes_send_client = recv(socket, buffer, MAX_BYTES-1, 0); // FIXED: Leave space for null terminator
    
    // Continue receiving until we get complete HTTP request (ends with \r\n\r\n)
    while(bytes_send_client > 0)
    {
        len = strlen(buffer);
        if(strstr(buffer, "\r\n\r\n") == NULL) // Request not complete
        {    
            bytes_send_client = recv(socket, buffer + len, MAX_BYTES - len - 1, 0); // FIXED: Bounds checking
        }
        else{
            break; // Complete request received
        }
    }

    // Create copy of request for cache operations
    char *tempReq = (char*)malloc(strlen(buffer)*sizeof(char)+1);
    for (int i = 0; i < strlen(buffer); i++)
    {
        tempReq[i] = buffer[i];
    }
    tempReq[strlen(buffer)] = '\0'; // FIXED: Proper null termination
    
    // Check if request exists in cache
    struct cache_element* temp = find(tempReq);

    if(temp != NULL){
        // Cache hit - send cached response
        printf("Data retrieved from the Cache\n");
        
        // FIXED: Proper chunked sending of cached data
        int total_sent = 0;
        int remaining = temp->len;
        
        while(remaining > 0 && total_sent < temp->len)
        {
            int chunk_size = (remaining > MAX_BYTES) ? MAX_BYTES : remaining;
            int sent = send(socket, temp->data + total_sent, chunk_size, 0);
            
            if(sent <= 0) break; // Error or connection closed
            
            total_sent += sent;
            remaining -= sent;
        }
    }
    else if(bytes_send_client > 0) // Cache miss - process request normally
    {
        len = strlen(buffer); 
        
        // Parse the HTTP request
        ParsedRequest* request = ParsedRequest_create();
        
        if (ParsedRequest_parse(request, buffer, len) < 0) 
        {
            printf("Parsing failed\n");
            sendErrorMessage(socket, 400); // FIXED: Send error response for bad requests
        }
        else
        {    
            // Only handle GET requests
            if(!strcmp(request->method,"GET"))                            
            {
                // Validate required components and HTTP version
                if(request->host && request->path && (checkHTTPversion(request->version) == 1))
                {
                    bytes_send_client = handle_request(socket, request, tempReq);
                    if(bytes_send_client == -1)
                    {    
                        sendErrorMessage(socket, 500); // Internal server error
                    }
                }
                else
                {
                    sendErrorMessage(socket, 400); // FIXED: Bad request instead of 500
                }
            }
            else
            {
                printf("This code doesn't support any method other than GET\n");
                sendErrorMessage(socket, 501); // FIXED: Not implemented for non-GET methods
            }
        }
        
        // Clean up parsed request structure
        ParsedRequest_destroy(request);
    }
    else if(bytes_send_client < 0)
    {
        perror("Error in receiving from client.\n");
    }
    else if(bytes_send_client == 0)
    {
        printf("Client disconnected!\n");
    }

    // Clean up and close connection
    shutdown(socket, SHUT_RDWR);
    close(socket);
    free(buffer);
    free(tempReq);
    
    // Release semaphore slot
    sem_post(&seamaphore);    
    sem_getvalue(&seamaphore,&p);
    printf("Semaphore post value:%d\n",p);
    
    return NULL;
}

/*
 * Function: main
 * Purpose: Initialize proxy server and handle incoming connections
 */
int main(int argc, char * argv[]) {
    int client_socketId, client_len;
    struct sockaddr_in server_addr, client_addr;
    
    // Initialize semaphore for connection limiting
    if(sem_init(&seamaphore, 0, MAX_CLIENTS) != 0)
    {
        perror("Semaphore initialization failed");
        exit(1);
    }
    
    // Initialize mutex for cache thread safety
    if(pthread_mutex_init(&lock, NULL) != 0)
    {
        perror("Mutex initialization failed");
        exit(1);
    }

    // Parse command line arguments
    if(argc == 2) {
        port_number = atoi(argv[1]);
    } else {
        printf("Usage: %s <port_number>\n", argv[0]); // FIXED: Better usage message
        exit(1);
    }

    printf("Setting Proxy Server Port : %d\n", port_number);

    // Create proxy server socket
    proxy_socketId = socket(AF_INET, SOCK_STREAM, 0);
    if(proxy_socketId < 0) {
        perror("Failed to create socket.\n");
        exit(1);
    }

    // Enable socket reuse to avoid "Address already in use" errors
    int reuse = 1;
    if (setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0)
        perror("setsockopt(SO_REUSEADDR) failed\n");

    // Setup server address structure
    bzero((char*)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;           // IPv4
    server_addr.sin_port = htons(port_number);  // Convert port to network byte order
    server_addr.sin_addr.s_addr = INADDR_ANY;  // Accept connections from any interface

    // Bind socket to address and port
    if(bind(proxy_socketId, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Port is not free\n");
        exit(1);
    }
    printf("Binding on port: %d\n", port_number);

    // Start listening for connections
    int listen_status = listen(proxy_socketId, MAX_CLIENTS);
    if(listen_status < 0) {
        perror("Error while Listening !\n");
        exit(1);
    }

    int i = 0;
    // Main server loop - accept and handle connections
    while(1) {
        bzero((char*)&client_addr, sizeof(client_addr));
        client_len = sizeof(client_addr);

        // Accept incoming connection
        client_socketId = accept(proxy_socketId, (struct sockaddr*)&client_addr, (socklen_t*)&client_len);
        if(client_socketId < 0) {
            fprintf(stderr, "Error in Accepting connection !\n");
            continue; // Continue accepting other connections
        }

        // Extract and display client information
        struct sockaddr_in* client_pt = (struct sockaddr_in*)&client_addr;
        struct in_addr ip_addr = client_pt->sin_addr;
        char str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &ip_addr, str, INET_ADDRSTRLEN);
        printf("Client is connected with port number: %d and ip address: %s \n", 
               ntohs(client_addr.sin_port), str);

        // Allocate memory for socket descriptor to pass to thread
        int* sock_ptr = malloc(sizeof(int));
        *sock_ptr = client_socketId;
        
        // Create thread to handle client request
        if(pthread_create(&tid[i % MAX_CLIENTS], NULL, thread_fn, sock_ptr) != 0) { // FIXED: Prevent array overflow
            perror("Thread creation failed\n");
            close(client_socketId);
            free(sock_ptr);
        } else {
            i++;
        }
    }
    
    // FIXED: Added proper cleanup (though this code is never reached)
    close(proxy_socketId);
    sem_destroy(&seamaphore);
    pthread_mutex_destroy(&lock);
    return 0;
}

/*
 * Function: find
 * Purpose: Search for URL in cache and update LRU timestamp if found
 * Parameters: url - request URL to search for
 * Returns: pointer to cache element if found, NULL otherwise
 */
cache_element* find(char* url){
    cache_element* site = NULL;
    
    // FIXED: Proper mutex error handling
    int temp_lock_val = pthread_mutex_lock(&lock);
    if(temp_lock_val != 0)
    {
        printf("Mutex lock failed in find: %d\n", temp_lock_val);
        return NULL;
    }
    
    // Search through cache linked list
    if(head != NULL){
        site = head;
        while (site != NULL)
        {
            if(!strcmp(site->url, url)){ // URL match found
                printf("LRU Time Track Before : %ld\n", site->lru_time_track);
                printf("url found\n");
                // Update LRU timestamp
                site->lru_time_track = time(NULL);
                printf("LRU Time Track After : %ld\n", site->lru_time_track);
                break;
            }
            site = site->next;
        }       
    }
    else {
        printf("url not found\n");
    }
    
    // FIXED: Check unlock return value
    temp_lock_val = pthread_mutex_unlock(&lock);
    if(temp_lock_val != 0)
    {
        printf("Mutex unlock failed in find: %d\n", temp_lock_val);
    }
    
    return site;
}

/*
 * Function: remove_cache_element
 * Purpose: Remove least recently used cache element to free space
 */
void remove_cache_element(){
    cache_element *p;   // Previous pointer
    cache_element *q;   // Current pointer
    cache_element *temp; // Element to remove
    
    // FIXED: Proper mutex error handling
    int temp_lock_val = pthread_mutex_lock(&lock);
    if(temp_lock_val != 0)
    {
        printf("Mutex lock failed in remove_cache_element: %d\n", temp_lock_val);
        return;
    }
    
    if(head != NULL) { // Cache not empty
        // Find element with oldest timestamp (LRU)
        for (q = head, p = head, temp = head; q->next != NULL; q = q->next) {
            if(((q->next)->lru_time_track) < (temp->lru_time_track)) {
                temp = q->next; // Found older element
                p = q;          // Keep track of previous
            }
        }
        
        // Remove element from linked list
        if(temp == head) { 
            head = head->next; // Removing head element
        } else {
            p->next = temp->next; // Bridge the gap
        }
        
        // Update cache size
        cache_size = cache_size - (temp->len) - sizeof(cache_element) - strlen(temp->url) - 1;
        
        // Free allocated memory
        free(temp->data);             
        free(temp->url);
        free(temp);
    } 
    
    // FIXED: Check unlock return value
    temp_lock_val = pthread_mutex_unlock(&lock);
    if(temp_lock_val != 0)
    {
        printf("Mutex unlock failed in remove_cache_element: %d\n", temp_lock_val);
    }
}

/*
 * Function: add_cache_element
 * Purpose: Add new response to cache with LRU management
 * Parameters:
 *   - data: response data to cache
 *   - size: size of response data
 *   - url: request URL (cache key)
 * Returns: 1 on success, 0 on failure
 */
int add_cache_element(char* data, int size, char* url){
    // FIXED: Proper mutex error handling
    int temp_lock_val = pthread_mutex_lock(&lock);
    if(temp_lock_val != 0)
    {
        printf("Mutex lock failed in add_cache_element: %d\n", temp_lock_val);
        return 0;
    }
    
    // Calculate total memory needed for new cache element
    int element_size = size + 1 + strlen(url) + sizeof(cache_element);
    
    // Check if element is too large for cache
    if(element_size > MAX_ELEMENT_SIZE){
        temp_lock_val = pthread_mutex_unlock(&lock);
        if(temp_lock_val != 0)
        {
            printf("Mutex unlock failed in add_cache_element: %d\n", temp_lock_val);
        }
        return 0; // Element too large
    }
    else
    {   
        // Remove old elements until we have enough space
        while(cache_size + element_size > MAX_SIZE){
            remove_cache_element();
        }
        
        // Allocate memory for new cache element
        cache_element* element = (cache_element*) malloc(sizeof(cache_element));
        if(element == NULL)
        {
            pthread_mutex_unlock(&lock);
            return 0;
        }
        
        // FIXED: Proper memory allocation with error checking
        element->data = (char*)malloc(size + 1);
        if(element->data == NULL)
        {
            free(element);
            pthread_mutex_unlock(&lock);
            return 0;
        }
        
        // FIXED: Use memcpy for binary data instead of strcpy
        memcpy(element->data, data, size);
        element->data[size] = '\0';
        
        element->url = (char*)malloc(strlen(url) + 1);
        if(element->url == NULL)
        {
            free(element->data);
            free(element);
            pthread_mutex_unlock(&lock);
            return 0;
        }
        
        // Initialize cache element
        strcpy(element->url, url);
        element->lru_time_track = time(NULL);    // Set current timestamp
        element->next = head;                    // Insert at head of list
        element->len = size;
        head = element;                          // Update head pointer
        cache_size += element_size;              // Update total cache size
        
        // FIXED: Check unlock return value
        temp_lock_val = pthread_mutex_unlock(&lock);
        if(temp_lock_val != 0)
        {
            printf("Mutex unlock failed in add_cache_element: %d\n", temp_lock_val);
        }
        return 1; // Success
    }
}