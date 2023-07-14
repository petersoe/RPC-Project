/**
 * @file communicate_client.c
 * @brief This is file used by rpcgen to generate the client side of the client-server application. It uses a ping thread to ensure server is active, blocks access to make new commands if the server goes down, and re-establishes a connection if server goes down. It also ensures any missed articles (from when client was not able to send verification after receiving an article) are downloaded.
 */

#include "communicate.h"
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <limits.h>
#include <ifaddrs.h>

#define MAX_MESSAGE_SIZE 1024
#define MAX_ARGS 4
#define SERVER_IP "192.168.0.20"
#define SERVER_PORT "12345"


/**
 * @brief Names used to identify the interfaces that are loopback interfaces, so IP addresses from these interfaces are not used for "local" call or initial update.
 * 
 */
const char* loopbackInterfaceNames[] = {
        "lo",
        "lo0",
        "lo1",
        "lo127.0.0.1",
        "lo::1",
        "localhost",
        "loopback"
    };

/**
 * @brief The mutex used to lock access to client data and subscription data on disk and in memory.
 * 
 */
pthread_mutex_t my_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief The mutex used to lock the ability to make further calls to the server.
 * 
 */
pthread_mutex_t lockDown = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief The mutex used to lock access to the client handle. This is done to prevent multiple cnt_creates from being called in one active connection, which creates memory problems.
 * 
 */
pthread_mutex_t clntMutex; 

/**
 * @brief Boolean used to determine to exit the application.
 * 
 */
bool_t continue_running = TRUE;

/**
 * @brief The socket descriptor for incoming UDP messages for the client.
 * 
 */
int clientSocket;

/**
 * @brief The client handle used to communicate with the server.
 * 
 */
CLIENT* clnt = NULL;

/**
 * @brief Function to recreate the client handle.
 *
 * @param host The host to connect to.
 */
void recreateClientHandle(const char* host) {
    pthread_mutex_lock(&clntMutex);
    if (clnt != NULL) {
        clnt_destroy(clnt);
        clnt = NULL;
    }
    clnt = clnt_create(host, COMMUNICATE_PROG, COMMUNICATE_VERSION, "udp");
    if (clnt == NULL) {
        clnt_pcreateerror(host); // Handle the error appropriately
    } 
    pthread_mutex_unlock(&clntMutex);
}

/**
 * @brief Arguments that are passed to the ping thread.
 * 
 */
struct pingThreadArgs {
    int sleepTime; /**< Sleep time between pings. */
    char* host; /**< The host to ping. */
    char* clientIP; /**< IP address of the client. */
    int listeningPort; /**< Port to listen for incoming connections. */
};

/**
 * @brief Arguments that are passed to the listen thread.
 * 
 */
struct listenThreadArgs {
    int port; /**< Port to listen for incoming connections. */
    char* host; /**< The host to connect to. */
};

/**
 * @brief Displays time in a human-readable format.
 *
 * @param seconds The total number of seconds.
 * @param timeOutput The output string for displaying the time.
 */
void displayTime(int seconds, char*timeOutput) {
    int years, months, weeks, days, hours, minutes;
    const int SECONDS_IN_MINUTE = 60;
    const int MINUTES_IN_HOUR = 60;
    const int HOURS_IN_DAY = 24;
    const int DAYS_IN_WEEK = 7;
    const int DAYS_IN_MONTH = 30;
    const int MONTHS_IN_YEAR = 12;
    years = seconds / (DAYS_IN_MONTH * MONTHS_IN_YEAR * HOURS_IN_DAY * MINUTES_IN_HOUR * SECONDS_IN_MINUTE);
    seconds %= (DAYS_IN_MONTH * MONTHS_IN_YEAR * HOURS_IN_DAY * MINUTES_IN_HOUR * SECONDS_IN_MINUTE);
    months = seconds / (DAYS_IN_MONTH * HOURS_IN_DAY * MINUTES_IN_HOUR * SECONDS_IN_MINUTE);
    seconds %= (DAYS_IN_MONTH * HOURS_IN_DAY * MINUTES_IN_HOUR * SECONDS_IN_MINUTE);
    weeks = seconds / (DAYS_IN_WEEK * HOURS_IN_DAY * MINUTES_IN_HOUR * SECONDS_IN_MINUTE);
    seconds %= (DAYS_IN_WEEK * HOURS_IN_DAY * MINUTES_IN_HOUR * SECONDS_IN_MINUTE);
    days = seconds / (HOURS_IN_DAY * MINUTES_IN_HOUR * SECONDS_IN_MINUTE);
    seconds %= (HOURS_IN_DAY * MINUTES_IN_HOUR * SECONDS_IN_MINUTE);
    hours = seconds / (MINUTES_IN_HOUR * SECONDS_IN_MINUTE);
    seconds %= (MINUTES_IN_HOUR * SECONDS_IN_MINUTE);
    minutes = seconds / SECONDS_IN_MINUTE;
    seconds %= SECONDS_IN_MINUTE;
    if (years > 0) {
         sprintf(timeOutput,"%d years\n", years);
    } else if (months > 0) {
        sprintf(timeOutput,"%d months\n", months);
    } else if (weeks > 0) {
        sprintf(timeOutput,"%d weeks\n", weeks);
    } else if (days > 0) {
        sprintf(timeOutput,"%d days\n", days);
    } else if (hours > 0) {
        sprintf(timeOutput,"%d hours\n", hours);
    } else if (minutes > 0) {
        sprintf(timeOutput,"%d minutes\n", minutes);
    } else {
        sprintf(timeOutput,"%d seconds\n", seconds);
    }
}

/**
 * @brief Retrieves the first local network IP address found that is not a loopback address.
 *
 * @param client_IP The buffer to store the IP address.
 * @return Returns 1 if successful, 0 otherwise.
 */
int get_local_net_IP(char * client_IP)
{
    int result = 0;
    // Retrieve the client's IP address
    struct ifaddrs *ifap, *ifa;

    if (getifaddrs(&ifap) == -1) {
        perror("getifaddrs");
        return FALSE;
    }
    
    size_t numLoopbackInterfaces = sizeof(loopbackInterfaceNames) / sizeof(loopbackInterfaceNames[0]);
    for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        
        int isLoopback = 0;
        for (size_t i = 0; i < numLoopbackInterfaces; ++i) {
            if (strcmp(ifa->ifa_name, loopbackInterfaceNames[i]) == 0) {
                isLoopback = 1;
                break;
            }
        }

        if (isLoopback) {
            continue; // Skip loopback interface
        }
        struct sockaddr_in* addr = (struct sockaddr_in*)ifa->ifa_addr;
        inet_ntop(AF_INET, &(addr->sin_addr), client_IP, INET_ADDRSTRLEN);
        result = 1;
        break;
    }
    freeifaddrs(ifap);
    return result;
}


/**
 * @brief Joins the program on the server. Prints Success, Rejection, or failed call. A redundant join will be rejected.
 *
 * @param arg_IP The IP address to add.
 * @param arg_port The port number to add.
 */
void join_prog(char* arg_IP, int arg_port) {
    bool_t *result;
    result = join_203(arg_IP, arg_port, clnt);
    if (result == (bool_t *) NULL) {
        clnt_perror(clnt, "call failed");
    }
    else if (*result == TRUE)
    {
        puts("success!");
    }
    else
    {
        puts("Rejected Join. Probably due to already joined.");
    }
}

/**
 * @brief Leaves the program on the server. Prints Success, Rejection, or failed call. A redundant leave will be rejected. Leaving will not erase any existing subscriptions. Subscriptions without a corresponding joined client will not result in any sent publications.
 *
 * @param arg_IP The IP address of the client.
 * @param arg_port The port number the client will be listening on for publications.
 */
void leave_prog(char* arg_IP, int arg_port) {
    bool_t *result;
    result = leave_203(arg_IP, arg_port, clnt);
    if (result == (bool_t *) NULL) {
        clnt_perror(clnt, "call failed");
    }
    else if (*result == TRUE)
    {
        puts("success!");
    }
    else
    {
        puts("Rejected Leave. Probably due to mispelling or due to not currently joined.");
    }
}

/**
 * @brief Subscribes to publications on the server based on given information. The IP and Port must be already joined to the server for the subscription to succeed.
 *
 * @param arg_IP The IP address of the client adding a subscription.
 * @param arg_port The port number the client will be listening on.
 * @param article An article detailing the publications to subscribe to. This must follow the proper format given by the assignment for subscriptions.
 */
void subscribe_prog(char *arg_IP, int arg_port, char *article) {
    bool_t *result;
    
    result = subscribe_203(arg_IP, arg_port, article, clnt);
    if (result == (bool_t *) NULL) {
        clnt_perror(clnt, "call failed");
    }
    else if (*result == TRUE)
    {
        puts("success!");
    }
    else
    {
        puts("Rejected Subscribe. Could be invalid article, mispelled, or the subscription could already exist. Check server logs for more details.");
    }
}

/**
 * @brief Unsubscribes to publications on the server based on given information.
 *
 * @param arg_IP The IP address of the client removing a subscription.
 * @param arg_port The port number the client will be listening on.
 * @param article An article detailing the publications to unsubscribe from. This must follow the proper format given by the assignment for subscriptions.
 */
void unsubscribe_prog(char *arg_IP, int arg_port, char *article) {
    bool_t *result;

    result = unsubscribe_203(arg_IP, arg_port, article, clnt);
    if (result == (bool_t *) NULL) {
        clnt_perror(clnt, "call failed");
    }
        else if (*result == TRUE)
    {
        puts("success!");
    }
    else
    {
        puts("Rejected Unsubscribe. Could be invalid article, mispelled, or the subscription may not have existed. Check server logs for more details.");
    }
}

/**
 * @brief Publishes an article on the server. Publications are sent out to all subscribed and active clients. There may be a several second delay while the server waits for potentially slow verifications before marking the sent message as failed for those clients. The publishing client will not receive a success notification until the operation is complete.
 *
 * @param host The host to connect to.
 * @param arg_IP The IP address of the client publishing. --saved in server logging, but not used in application
 * @param arg_port The port number the client publishing listens on. --saved in server logging, but not used in application
 * @param article The article to publish. This will contain the contents of the publication along with corresponding article information. This must correspond with requirements for publications given in the assignment.
 */
void publish_prog(char *host, char *arg_IP, int arg_port, const char *article) {
    bool_t *result;
    
    if (clnt == NULL) {
        clnt_pcreateerror(host);
        exit(1);
    }
    
    result = publish_203(arg_IP, arg_port, article, clnt);
    if (result == (bool_t *) NULL) {
        clnt_perror(clnt, "call failed");
    }
        else if (*result == TRUE)
    {
        puts("success!");
    }
    else
    {
        puts("Rejected Publish.");
    }
}

/**
 * @brief Updates the given client IP and Port with any publications it failed to verify for (possibly due to being inactive). This update will not require verification. The server will assume all previously failed articles for this IP and Port are successfully sent.
 *
 * @param arg_IP The IP address of the client to update.
 * @param arg_port The port number the client is listening ons.
 */
void update_prog(char* arg_IP, int arg_port) {
    bool_t *result;
    result = update_203(arg_IP, arg_port, clnt);
    if (result == (bool_t *) NULL) {
        clnt_perror(clnt, "call failed");
    }
    else if (*result == TRUE)
    {
        puts("success!");
    }
    else
    {
        puts("Unknown Update Failure.");
    }
}

/**
 * @brief Pings the server to check if it is reachable.
 *
 * @return Returns TRUE if ping succeeds, FALSE otherwise.
 */
bool_t ping_prog() { 
    bool_t *result;
    result = ping_203(clnt);
    if (result == (bool_t *) NULL) {
        clnt_perror(clnt, "call failed");
        return FALSE;
    }
    return TRUE;
}

/**
 * @brief Performs a series of test calls to the server.
 *
 * @param arg_IP The IP address of the client.
 * @param arg_port The port number the client will listen on.
 */
void test_calls_prog_203(char * arg_IP, int arg_port) {
    char *SubscribeArticle = "Sports;testAuthor;TestOrg;";
    char *PublishArticle = "Sports;testAuthor;TestOrg;This is an auto-generated article from the test command.";
    bool_t *result_1;
    bool_t *result_2;
    bool_t *result_3;
    bool_t *result_4;
    bool_t *result_5;
    bool_t *result_6;
    bool_t *result_7;
    bool_t result3AlreadyCalculated = FALSE;

    result_1 = ping_203(clnt);
    if (result_1 == (bool_t *) NULL) {
        clnt_perror(clnt, "ping call failed\n");
    } else {
        printf("call to ping succeeded\n");
    }
    result_2 = join_203(arg_IP, arg_port, clnt);
    if (result_2 == (bool_t *) NULL) {
        clnt_perror(clnt, "join call failed\n");
    } else if (*result_2 == FALSE) 
    {
        //checking the case where the IP already existed on the client
        result_3 = leave_203(arg_IP, arg_port, clnt);
        if (*result_3 == TRUE) 
        {
            result_2 = join_203(arg_IP, arg_port, clnt);
            if (*result_2 == TRUE)
            {
                printf("call to join succeeded\n");
                result3AlreadyCalculated = TRUE;
            }
        }
        else
        {
            printf("call to join succeeded, but execution returned failure notification.\n");
        }   
    }
    else{
        printf("call to join succeeded\n");
    }
    result_4 = subscribe_203(arg_IP, arg_port, SubscribeArticle, clnt);
    if (result_4 == (bool_t *) NULL) {
        clnt_perror(clnt, "subscribe call failed\n");
    } else if (*result_4 == FALSE) 
    {
        printf("call to subscribe succeeded, but execution returned failure notification.\n");
    }
    else {
        printf("call to subscribe succeeded\n");
    }
    result_5 = publish_203(arg_IP, arg_port, PublishArticle, clnt);
    if (result_5 == (bool_t *) NULL) {
        clnt_perror(clnt, "publish call failed\n");
    } else if (*result_5 == FALSE) 
    {
        printf("call to publish succeeded, but execution returned failure notification.\n");
    }
    else {
        printf("call to publish succeeded\n");
    }
    result_7 = update_203(arg_IP, arg_port, clnt);
    if (result_7 == (bool_t *) NULL) {
        clnt_perror(clnt, "update call failed\n");
    } else if (*result_7 == FALSE) 
    {
        printf("call to update succeeded, but execution returned failure notification.\n");
    }
    else {
        printf("call to update succeeded\n");
    }
    result_6 = unsubscribe_203(arg_IP, arg_port, SubscribeArticle, clnt);
    if (result_6 == (bool_t *) NULL) {
        clnt_perror(clnt, "unsubscribe call failed\n");
    } else if (*result_6 == FALSE) 
    {
        printf("call to unsubscribe succeeded, but execution returned failure notification.\n");
    }
    else {
        printf("call to unsubscribe succeeded\n");
    }
    if(result3AlreadyCalculated == FALSE)
    {
        result_3 = leave_203(arg_IP, arg_port, clnt);
    }
    if (result_3 == (bool_t *) NULL) {
        clnt_perror(clnt, "leave call failed\n");
    } else if (*result_3 == FALSE) 
    {
        printf("call to leave succeeded, but execution returned failure notification.\n");
    }
    else {
        printf("call to leave succeeded\n");
    }
}

// Thread 1: listens for incoming UDP connections

/**
 * @brief Listens for incoming UDP connections to receive publications.Constantly runs while program is active.The publication is not listening while saving a received publication.If incoming publications are sent very rapidly, this thread may miss some.Testing did not reveal any misses with rapid publications.
 *
 * @param arg Pointer to the listen thread arguments.
 */
void *listen_thread(void *arg) {
    struct listenThreadArgs* args = (struct listenThreadArgs*) arg;
    int port = args->port;
    char *host = args->host;
 
    //sets up a UDP listening socket for the client to receive communications from the server
    int client_fd; // File descriptor for the client socket
    struct sockaddr_in client_addr, server_addr;
    socklen_t len;
    memset(&client_addr, 0, sizeof (client_addr)); //making sure all padded bytes are zero in the new addresses.

    // Create a socket for the client
    if ((client_fd = socket(AF_INET, SOCK_DGRAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    

    // Set up the client address
    client_addr.sin_family = AF_INET; //makes it IPv4
    //inet_pton(AF_INET, host, &(client_addr.sin_addr));  this line should have allowed me to just receive incoming messages from the host server, but i couldn't figure out why it wouldn't bind
    client_addr.sin_addr.s_addr = htonl(INADDR_ANY); // Bind to all interfaces
    client_addr.sin_port = htons(port);
    // try to bind the address to a socket
    if (bind(client_fd, (struct sockaddr *) &client_addr, sizeof (client_addr)) < 0) {
        perror("socket bind failed for udp communication listening thread");
        exit(EXIT_FAILURE);
    }
    
    //listening loop
    char message[MAX_MESSAGE_SIZE];
    char * doNotVerifyPrefix = "DO NOT SEND VERIFICATION:";
    size_t prefixLen = strlen(doNotVerifyPrefix);
    bool_t continue_running_local = TRUE;
    while (continue_running_local == TRUE) {
        // Check if the thread can continue to listen
        continue_running_local = continue_running;

        // Receive message from the server
        len = sizeof (server_addr);
        memset(&server_addr, 0, sizeof (server_addr));
        memset(&message, 0, sizeof (message));
        //code should wait here until a message is sent
        recvfrom(client_fd, &message, MAX_MESSAGE_SIZE, MSG_WAITALL, (struct sockaddr *) &server_addr, &len);
        //if response is for a test message for update we don't want to save it
        if (strncmp(message, doNotVerifyPrefix, prefixLen) != 0)
        {
            //sending confirmation back to the server now the information is saved. Otherwise, the information might still be lost if the server considered the information saved after receiving but before saving.
            int verification = 100;
            int verification_network_order = verification;//htons(verification);
            if (sendto(client_fd, &verification_network_order, sizeof(verification_network_order), 0, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
                printf("sendto verification failed. server doesn't think client got the article.");
            }
        }
        else
        {
            //remove the "DO NOT SEND VERIFICAION:" prefix from the message
            memmove(message, message + prefixLen, strlen(message) - prefixLen + 1);
            
        }

        // starting to save received article now, so time to lock things up
        pthread_mutex_lock(&my_mutex);
        //open file for appending articles received or create one if it doesn't exist.
        // Check if the file exists
        char * receivedArticlesFilename = "ReceivedArticles.txt";
        FILE* receivedArticles = fopen(receivedArticlesFilename, "r");
        if (receivedArticles == NULL) {
            // File doesn't exist, create a new file for writing
            receivedArticles = fopen(receivedArticlesFilename, "w");
            if (receivedArticles == NULL) {
                printf("Failed to open or create ReceivedArticles.txt file. Aborting.\n");\
                abort();
            }
        } else {
            // File exists, open it in append mode
            receivedArticles = fopen(receivedArticlesFilename, "a");
            if (receivedArticles == NULL) {
                printf("Failed to open or create ReceivedArticles.txt file. Aborting.\n");
                abort();
            }
        }
        // save received message
        fprintf(receivedArticles,"%s\n", message);
        fclose(receivedArticles);
        //unlock stuff
        pthread_mutex_unlock(&my_mutex);
    }
}

/**
* @brief Checks if the string can be converted to a valid UINT16 port number.
*
* @param str The string to check.
* @return Returns 1 if the string is a valid port number, 0 otherwise.
*/

int isPortConvertableToUINT16(const char *str) {
    //if the length of the string max for INT_MAX is reached, the number must be too large.
    if (strlen(str) > snprintf(NULL, 0, "%d", UINT16_MAX)) {
        //The string has more characters than a string version of INT_MAX could have.
        printf("This has too many characters to be a valid port number: %s",str);
        return 0;
    }
    char *endptr;
    long result = strtol(str, &endptr, 10);
    // If endptr is equal to the input string, no conversion was performed
    if (endptr == str)
    {
        printf("this is not a valid port number: %s",str);
        return 0;
    }
    // If endptr points to a non-null character, there are trailing characters
    if (*endptr != '\0' && *endptr != '\n')
    {
        printf("this is not a valid port number: %s",str);
        return 0;
    }
    //check to see if the port can exist
    if (result > UINT16_MAX || result < 0) {
        printf("The value exceeds the maximum allowed port number.\n");
        return 0;
    }
    return 1;
}

/**
 * @brief Sends periodic pings to the server in a separate thread. If a ping fails, new commands are blocked. The thread will continue to attempt to reconnect. Once reconnected, listening will resume and an update of any missed publications will be retrieved. A restart of the client application will be necessary for new manual client commands.
 *
 * @param arg Pointer to the ping thread arguments.
 */
void *ping_thread(void *arg) {
    struct pingThreadArgs* args = (struct pingThreadArgs*) arg;
    int sleep_time = args->sleepTime;
    char* host = args->host;
    char* client_ip = args->clientIP;
    int listeningPort = args->listeningPort;
    
    //start ping loop to keep make sure server is up.
    bool_t continue_running_local = TRUE;
    bool_t lastOneFailed = FALSE;
    int iterationsOffline = 0;
    while (continue_running_local) {
        //ping host 
        if(lastOneFailed == TRUE)
        {
            recreateClientHandle(host);
            if (clnt == NULL) {
                int secondsOffline = iterationsOffline*3;
                char timeString[60];
                displayTime(secondsOffline,timeString);
                printf("\n Server still unreachable. Trying another ping in 3 seconds. Time down: %s",timeString);
                sleep(3);
                iterationsOffline++;
                continue;
            }
        }
        bool_t ping_result = ping_prog();
        if(ping_result == TRUE)
        {
            if(lastOneFailed)
            {
                printf("\nBackground ping succeeded! Unblocking other tasks.\n");
                
                printf("\nusing %s as local IP for update\n",client_ip);
                printf("attempting to update %s ...",client_ip);
                update_prog(client_ip,listeningPort);
                
                printf("\nListening to the server for publications is still active.\nPlease restart the client application to invoke new commands.");
                
                //pthread_mutex_unlock(&lockDown);
                //printf("\nEnter command: ");
            }
            lastOneFailed = FALSE;
        } else
        {
            printf("\nBackground ping failed. Server not reachable. Blocking other Tasks. Trying another ping in 3 seconds.");
            if(lastOneFailed == FALSE)
            {
                pthread_mutex_lock(&lockDown);
            }
            lastOneFailed = TRUE;
            sleep(3);
            iterationsOffline++;
            continue;
        };
        
        //go to sleep for a while
        sleep(sleep_time);
    }
}

/**
 * The main function of the client program.
 * @return 0 on successful execution
 */
int main() {
    char host[17];
    char portChar[17];
    strcpy(portChar, "54321"); // DEBUG: Default port number
    strcpy(host, SERVER_IP); // DEBUG: Default server IP

    // Grab address information from the user
    bool_t ping_result = FALSE;
    int port = -1;

    while (ping_result != TRUE) {
        printf("\nEnter host IPv4: ");
        printf("%s\n", host); // DEBUG: Print entered host IP

        printf("Trying an initial ping...");
        recreateClientHandle(host);

        if (clnt == NULL) {
            continue;
        }

        // Send initial ping to check if the server is up
        ping_result = ping_prog();

        if (ping_result != TRUE) {
            printf("Error: Unable to ping the server.\n");
        }
        else {
            printf("Success!\n");
        }

        bool_t good_port = FALSE;

        while (good_port != TRUE) {
            printf("\nEnter the port number you want to start listening on: ");
            printf("%s\n", portChar); // DEBUG: Print entered port number

            // Remove the trailing newline character
            if (portChar[strlen(portChar) - 1] == '\n') {
                portChar[strlen(portChar) - 1] = '\0';
            }

            // Convert the input string to an integer
            port = atoi(portChar);

            // Check if the conversion was successful
            if (port != 0 || strcmp(portChar, "0") == 0) {
                good_port = TRUE;
            }
            else {
                printf("Invalid input. Please enter a valid port number.\n");
            }
        }
    }

    // Start listen thread
    int ret;
    pthread_t listen_tid;
    struct listenThreadArgs listenArgs = { port, host };
    ret = pthread_create(&listen_tid, NULL, listen_thread, &listenArgs);
    if (ret != 0) {
        perror("Error creating listen thread");
        exit(EXIT_FAILURE);
    }

    // Update the client to get any articles it may have missed while offline
    char client_ip[INET_ADDRSTRLEN];
    get_local_net_IP(client_ip);

    printf("\nUsing %s as the local IP for update\n", client_ip);
    printf("Attempting to update %s ...\n", client_ip);
    update_prog(client_ip, port);

    // Start ping thread
    pthread_t ping_tid;
    struct pingThreadArgs pingArgs = { 10, host, client_ip, port }; // The sleep time of the listener thread
    ret = pthread_create(&ping_tid, NULL, ping_thread, &pingArgs);
    if (ret != 0) {
        perror("Error creating ping thread");
        exit(EXIT_FAILURE);
    }

    // Start command operations
    bool_t continue_running_local = TRUE;
    const int max_arg_size = 1000;
    char input[1000];

    // Initialize arguments
    char* argums[MAX_ARGS];
    for (int i = 0; i < MAX_ARGS; i++) {
        argums[i] = malloc(max_arg_size);
    }

    while (continue_running_local) {
        printf("\nEnter command: ");

        // Make sure nothing is locked down by getting and releasing the lockDown Mutex
        pthread_mutex_lock(&lockDown);
        pthread_mutex_unlock(&lockDown);

        fgets(input, sizeof(input), stdin);
        char* token = strtok(input, " "); // No spaces are allowed unless in an article with quotes
        int argc = 0;

        while (token && argc < MAX_ARGS) {
            // Remove trailing newline
            if (token[strlen(token) - 1] == '\n') {
                token[strlen(token) - 1] = '\0';
            }

            if (argc < MAX_ARGS) {
                memset(argums[argc], 0, max_arg_size);

                if (token[0] == '"' && argc == 3) {
                    // If a quote starts the last argument, concatenate all tokens until a closing quote is found
                    char quoted_arg[1000] = "";

                    while (token != NULL && token[strlen(token) - 1] != '"' && token[strlen(token) - 1] != '\0') {
                        // Remove trailing newline in the case of quotes
                        if (token[strlen(token) - 1] == '\n') {
                            token[strlen(token) - 1] = '\0';
                        }

                        strlcat(quoted_arg, token, sizeof(quoted_arg));
                        token = strtok(NULL, " ");

                        if (token != NULL) {
                            strlcat(quoted_arg, " ", sizeof(quoted_arg));
                        }
                    }

                    // Remove leading and trailing quotes
                    if (quoted_arg[0] == '"') {
                        memmove(quoted_arg, quoted_arg + 1, strlen(quoted_arg));
                    }

                    if (quoted_arg[strlen(quoted_arg) - 1] == '"') {
                        quoted_arg[strlen(quoted_arg) - 1] = '\0';
                    }

                    strlcpy(argums[argc], quoted_arg, max_arg_size);
                    free(quoted_arg);
                }
                else {
                    strlcpy(argums[argc], token, max_arg_size);
                    token = strtok(NULL, " ");
                }
            }

            argc++;
        }

        // Check command and call corresponding function
        if (strncmp(argums[0], "ping\0", 5) == 0) {
            if (argc != 1) {
                printf("\nMake sure you don't add any accidental spaces.\n");
                printf("Argument count: %d\n", argc);
                printf("Usage: ping [no arguments]\n");
            }
            else {
                ping_result = ping_prog();
                if (ping_result != TRUE) {
                    perror("Error: Unable to ping the server.\n");
                }
                else {
                    printf("Success!\n");
                }
            }
        }
        else if (strncmp(argums[0], "publish\0", 8) == 0) {
            if (argc != 4) {
                printf("\nIf the article contains spaces, it needs to be in quotes.\n");
                printf("Do not nest your quotes; use single quotes instead of double if needed.\n");
                printf("Argument count: %d\n", argc);
                printf("Usage: publish <IP> <port> <article>\n");
            }
            else if (isPortConvertableToUINT16(argums[2]) == 1) {
                char arg_IP[16];
                if (strcmp(argums[1], "local") == 0) {
                    strcpy(arg_IP, client_ip);
                    printf("Using %s as the local IP\n", client_ip);
                }
                else {
                    strcpy(arg_IP, argums[1]);
                }
                int arg_port = atoi(argums[2]);
                char* article = argums[3];

                publish_prog(host, arg_IP, arg_port, article);
            }
        }
        else if (strncmp(argums[0], "unsubscribe\0", 12) == 0) {
            if (argc != 4) {
                printf("\nMake sure you don't add any accidental spaces.\n");
                printf("Argument count: %d\n", argc);
                printf("Usage: unsubscribe <IP> <port> <article>\n");
            }
            else if (isPortConvertableToUINT16(argums[2]) == 1) {
                char arg_IP[16];
                if (strcmp(argums[1], "local") == 0) {
                    strcpy(arg_IP, client_ip);
                    printf("Using %s as the local IP\n", client_ip);
                }
                else {
                    strcpy(arg_IP, argums[1]);
                }
                int arg_port = atoi(argums[2]);
                char* article = argums[3];
                unsubscribe_prog(arg_IP, arg_port, article);
            }
        }
        else if (strncmp(argums[0], "subscribe\0", 10) == 0) {
            if (argc != 4) {
                printf("\nMake sure you don't add any accidental spaces.\n");
                printf("Argument count: %d\n", argc);
                printf("Usage: subscribe <IP> <port> <article>\n");
            }
            else if (isPortConvertableToUINT16(argums[2]) == 1) {
                char arg_IP[16];
                if (strcmp(argums[1], "local") == 0) {
                    strcpy(arg_IP, client_ip);
                    printf("Using %s as the local IP\n", client_ip);
                }
                else {
                    strcpy(arg_IP, argums[1]);
                }
                int arg_port = atoi(argums[2]);
                char* article = argums[3];
                subscribe_prog(arg_IP, arg_port, article);
            }
        }
        else if (strncmp(argums[0], "leave\0", 6) == 0) {
            if (argc != 3) {
                printf("\nMake sure you don't add any accidental spaces.\n");
                printf("Argument count: %d\n", argc);
                printf("Usage: leave <IP> <port>\n");
            }
            else if (isPortConvertableToUINT16(argums[2]) == 1) {
                char arg_IP[16];
                if (strcmp(argums[1], "local") == 0) {
                    strcpy(arg_IP, client_ip);
                    printf("Using %s as the local IP\n", client_ip);
                }
                else {
                    strcpy(arg_IP, argums[1]);
                }
                int arg_port = atoi(argums[2]);
                leave_prog(arg_IP, arg_port);
            }
        }
        else if (strncmp(argums[0], "join\0", 5) == 0) {
            if (argc != 3) {
                printf("\nMake sure you don't add any accidental spaces.\n");
                printf("Argument count: %d\n", argc);
                printf("Usage: join <IP> <port>\n");
            }
            else if (isPortConvertableToUINT16(argums[2]) == 1) {
                char arg_IP[16];
                if (strcmp(argums[1], "local") == 0) {
                    strcpy(arg_IP, client_ip);
                    printf("Using %s as the local IP\n", client_ip);
                }
                else {
                    strcpy(arg_IP, argums[1]);
                }
                int arg_port = atoi(argums[2]);
                join_prog(arg_IP, arg_port);
            }
        }
        else if (strncmp(argums[0], "update\0", 7) == 0) {
            if (argc != 3) {
                printf("\nMake sure you don't add any accidental spaces.\n");
                printf("Argument count: %d\n", argc);
                printf("Usage: update <IP> <port>\n");
            }
            else {
                char arg_IP[16];
                if (strcmp(argums[1], "local") == 0) {
                    strcpy(arg_IP, client_ip);
                    printf("Using %s as the local IP\n", client_ip);
                }
                else {
                    strcpy(arg_IP, argums[1]);
                }
                int arg_port = atoi(argums[2]);
                update_prog(arg_IP, arg_port);
            }
        }
        else if (strncmp(argums[0], "test\0", 5) == 0) {
            if (argc != 1) {
                printf("\nMake sure you don't add any accidental spaces.\n");
                printf("Argument count: %d\n", argc);
                printf("Usage: test [no arguments]\n");
            }
            else {
                printf("Using %s as the local IP\n", client_ip);
                test_calls_prog_203(client_ip, 54321); // DEBUG: Using default port number
            }
        }
        else if (strncmp(argums[0], "exit\0", 5) == 0) {
            if (argc != 1) {
                printf("\nMake sure you don't add any accidental spaces.\n");
                printf("Argument count: %d\n", argc);
                printf("Usage: exit\n");
            }
            else {
                // We already have the mutex lock, so it is safe to assume that the listener thread is not doing any work,
                // and we can stop it safely.
                continue_running_local = FALSE;
                continue_running = FALSE;
                pthread_cancel(ping_tid);
                pthread_cancel(listen_tid);
                printf("Threads exited. Goodbye.\n");
                return 1;
            }
        }
        else if (strncmp(argums[0], "help", 4) == 0) {
            printf("<IP> can be substituted with \"local\" to use the first non-loopback IPv4 found.\n");
            printf("Available commands:\n");
            printf("\tping\n");
            printf("\tjoin <IP> <port>\n");
            printf("\tleave <IP> <port>\n");
            printf("\tsubscribe <IP> <port> <article>\n");
            printf("\tunsubscribe <IP> <port> <article>\n");
            printf("\tpublish <IP> <port> <article>\n");
            printf("\tupdate <IP> <port>\n");
            printf("\thelp\n");
            printf("\texit\n");
        }
        else {
            printf("Invalid command. Type 'help' for a list of commands.\n");
        }

        pthread_mutex_unlock(&my_mutex);

        for (int i = 0; i < MAX_ARGS; i++) {
            free(argums[i]);
        }

        // Clear input string after using
        memset(input, 0, sizeof(input));
    }

    // Wait for threads to finish
    pthread_join(ping_tid, NULL);
    pthread_join(listen_tid, NULL);

    return 0;
}
