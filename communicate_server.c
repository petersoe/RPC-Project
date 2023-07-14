/**
 * @file server.c
 * @brief Main server code used by rpcgen to generate the client side of the client-server application. 
 * 
 * It keeps a list of clients and subscriptions in memory. It sends a copy of the memory to the disk every time a request for join, subscribe, leave, or unsubscribe is called. It saves a list of all send requests that it did not receive a verification for that it will send out upon an update request.
 */

#include "communicate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "PubSubTools.h"
#include <time.h>
#include <errno.h>
#include <pthread.h>

#define LOG_FILE "server.log"
#define SERVER_PORT 12345
#define BUFFER_SIZE 1024
#define MAX_KNOWN_CLIENTS 200
#define FAILED_PATH "failed.txt"
#define SUBSCRIPTION_PATH "SubscriptionData.txt"
#define CLIENT_PATH "ClientData.txt"

/**
 * @brief Mutex used for publishing to ensure data structures are accessed by verification thread while another thread is altering them.
 */
pthread_mutex_t publishingLock = PTHREAD_MUTEX_INITIALIZER;

//socket information

/**
 * @brief The descriptor for the server socket for receiving verifications from clients.
 */
int server_socket;

/**
 * @brief Flag indicating if the socket has been initialized.
 */
bool_t isSocketInitialized = FALSE;

//shared subscription information

/**
 * @brief The current number of subscriptions.
 */
int sub_count = 0;

/**
 * @brief The maximum number of subscriptions allocated in memory.
 *
 * This value will dynamically increase by 50% every time the maximum is reached.
 */
int sub_max = 100;

/**
 * @brief Array holding the subscription information. The size will dynamically increase as needed.
 *
 * Use the provided add/remove functions instead of explicitly declaring the size.
 */
pubSub * SubscriptionData;

//shared client information

/**
 * @brief Array holding the information of known clients.
 *
 * The maximum number of known clients is 200. Each client is stored as [IP][Port],
 * with a maximum of 15 characters for the IP and 15 characters for the Port.
 */
char ***ClientData;

/**
 * @brief The number of known clients.
 */
int NumberOfKnownClients;

//shared failed send information

/**
 * @brief The current number of failed sends.
 */
int failed_count = 0;

/**
 * @brief The maximum number of failed sends allocated in memory.
 *
 * This value will dynamically increase by 50% every time the maximum is reached.
 */
int failed_max = 100;

/**
 * @brief Array holding the information of failed sends.
 *
 * Use the provided add/remove functions instead of explicitly declaring the size.
 */
pubSub * FailedSends;

/**
 * @brief The number of verifications received.
 */
int number_of_verifications_received;

/**
 * @brief Array holding the verified IP addresses.
 */
struct sockaddr_in *verifiedIPs;

/**
 * @brief Struct used to pass arguments to the verification thread.
 * 
 */
struct verifyThreadArgs {
    int maxNumberOfIPs;
};

/**
 * @brief Check if the client data has been initialized and initialize it if necessary.
 * @return Boolean value indicating if the client data is initialized.
 */
bool_t checkForClientDataInitialization() {
    FILE *log = fopen(LOG_FILE, "a");
    if (log == NULL) {
        fprintf(stderr, "Error opening log file\n");
        exit(1);
    }
    static bool_t hasInitialized = FALSE;
    if (hasInitialized == FALSE) {
        ClientData = malloc(sizeof (char*[MAX_KNOWN_CLIENTS]));
        for (int i = 0; i < MAX_KNOWN_CLIENTS; i++) {
            ClientData[i] = malloc(sizeof (char*[2]));
            ClientData[i][0] = malloc(sizeof (char[15]));
            ClientData[i][1] = malloc(sizeof (char[15]));
        }
        NumberOfKnownClients = readClientData(CLIENT_PATH, ClientData, log);
        hasInitialized = TRUE;
    }
    fclose(log);
}

/**
 * @brief Check if the subscription data has been initialized and initialize it if necessary.
 * @return Boolean value indicating if the subscription data is initialized.
 */
bool_t checkForSubscriptionDataInitialization() {
    FILE *log = fopen(LOG_FILE, "a");
    if (log == NULL) {
        fprintf(stderr, "Error opening log file\n");
        exit(1);
    }
    static bool_t hasInitialized = FALSE;
    if (hasInitialized == FALSE) {
        pubSub *new_subs = (pubSub*) realloc(SubscriptionData, sub_max * sizeof (pubSub)); // allocate starting amount of memory for subscriptionData
        if (new_subs == NULL) {
            fprintf(log, "failed to allocate memory for SubscriptionData.\n");
            exit(1);
        }
        SubscriptionData = new_subs; // update pointer to new memory only if realloc does not fail
        readSubscriptionData(SUBSCRIPTION_PATH, &SubscriptionData, &sub_count, &sub_max, log);
    }
    hasInitialized = TRUE;
    fclose(log);
    return (hasInitialized);
}


/**
 * @brief Check if the failed sends data has been initialized and initialize it if necessary.
 * @return Boolean value indicating if the failed sends data is initialized.
 */
bool_t checkForFailedSendsInitialization() {
    FILE *log = fopen(LOG_FILE, "a");
    if (log == NULL) {
        fprintf(stderr, "Error opening log file\n");
        exit(1);
    }
    static bool_t hasInitialized = FALSE;
    if (hasInitialized == FALSE) {
        pubSub *new_pubSubs = (pubSub*) realloc(FailedSends, sub_max * sizeof (pubSub)); // allocate starting amount of memory for subscriptionData
        if (new_pubSubs == NULL) {
            fprintf(log, "failed to allocate memory for FailedSends.\n");
            exit(1);
        }
        FailedSends = new_pubSubs; // update pointer to new memory only if realloc does not fail
        readFailedData(FAILED_PATH, &FailedSends, &failed_count, &failed_max, log);
    }
    hasInitialized = TRUE;
    fclose(log);
    return (hasInitialized);
}

/**
 * @brief Ping function for RPC call.
 * @param[in] rqstp Pointer to the service request structure.
 * @return Boolean value indicating success.
 */
bool_t *
ping_203(rqstp)
struct svc_req *rqstp;
{
    static bool_t result = TRUE;
    return (&result);
}

/**
 * @brief Join function for RPC call. Adds a client to the list of known clients for the server.
 * @param[in] IP IP address of the client.
 * @param[in] Port Port number of the client.
 * @param[in] rqstp Pointer to the service request structure.
 * @return Boolean value indicating success.
 */
bool_t *
join_203(char *IP, int Port, struct svc_req *rqstp)
/*Adds a client to the list of known clients for the server*/ {
    static bool_t result; //don't know why, but it always has to be static
    result = FALSE;
    //logging
    FILE *log = fopen(LOG_FILE, "a");
    if (log == NULL) {
        fprintf(stderr, "Error opening log file\n");
        exit(1);
    }
    fprintf(log, "Got request to join:\tIP: %s | Port: %d\n", IP, Port);
    fclose(log);

    checkForClientDataInitialization();

    int clientIndex; // not doing anything here except acting as a required argument for "isClientInList"
    
    bool_t foundClient = isClientInList(ClientData, NumberOfKnownClients, IP, Port, &clientIndex);
    if (!foundClient) {
        if (NumberOfKnownClients + 1 < MAX_KNOWN_CLIENTS) {
            strcpy(ClientData[NumberOfKnownClients][0], IP);
            char numToSave[15];
            snprintf(numToSave, sizeof (numToSave), "%d", Port);
            strcpy(ClientData[NumberOfKnownClients][1], numToSave);
            NumberOfKnownClients++;
            if (saveClientData(CLIENT_PATH, ClientData, NumberOfKnownClients, log)) {
                result = TRUE;
            } else {
                result = FALSE;
            }

        }
    } else {
        puts("Client Already Exists On List\n");
    }

    fclose(log);
    return &result;
}

/**
 * @brief Leave function for RPC call. Removes a client from the list of known clients for the server.
 * @param[in] IP IP address of the client.
 * @param[in] Port Port number of the client.
 * @param[in] rqstp Pointer to the service request structure.
 * @return Boolean value indicating success.
 */
bool_t *
leave_203(char *IP, int Port, struct svc_req *rqstp) {
    static bool_t result; //don't know why, but it always has to be static
    result = FALSE;

    FILE *log = fopen(LOG_FILE, "a");
    if (log == NULL) {
        fprintf(stderr, "Error opening log file\n");
        exit(1);
    }
    fprintf(log, "Got request to leave:\tIP: %s | Port: %d\n", IP, Port);
    fclose(log);

    checkForClientDataInitialization();

    int clientIndex;
    
    bool_t foundClient = isClientInList(ClientData, NumberOfKnownClients, IP, Port, &clientIndex);
    if (foundClient == TRUE) {
        for (int i = clientIndex; i < NumberOfKnownClients; i++) {
            strcpy(ClientData[i][0], ClientData[i + 1][0]);
            strcpy(ClientData[i][1], ClientData[i + 1][1]);
        }
        free(ClientData[NumberOfKnownClients][0]);
        ClientData[NumberOfKnownClients][0] = malloc(sizeof (char[15]));
        free(ClientData[NumberOfKnownClients][1]);
        ClientData[NumberOfKnownClients][1] = malloc(sizeof (char[15]));
        NumberOfKnownClients--;
        saveClientData(CLIENT_PATH, ClientData, NumberOfKnownClients, log);
        result = TRUE;
    }
    
    return &result;
}

/**
 * @brief Subscribe function for RPC call. 
 * 
 * Adds a subscription for a client. Subscription article must match the format described by the assignment or function will return false.
 * @param[in] IP IP address of the client.
 * @param[in] Port Port number of the client.
 * @param[in] Article Article with subscribe info.
 * @param[in] rqstp Pointer to the service request structure.
 * @return Boolean value indicating success.
 */
bool_t *
subscribe_203(char *IP, int Port, char *Article, struct svc_req *rqstp) {
    static bool_t result; //don't know why, but it always has to be static
    result = FALSE;
    //logging
    FILE *log = fopen(LOG_FILE, "a");
    if (log == NULL) {
        fprintf(stderr, "Error opening log file\n");
        exit(1);
    }
    fprintf(log, "Got request to subscribe for:\tIP: %s | Port: %d | Article: %s\n", IP, Port, Article);

    checkForSubscriptionDataInitialization();

    result = add_Subscription(&SubscriptionData, IP, Port, Article, &sub_count, &sub_max, log);

    if (result == FALSE) {
        fprintf(log, "subscribe rejected or was unsuccessful for:\tIP: %s | Port: %d | Article: %s\n", IP, Port, Article);
        fclose(log);
        return &result;
    }

    result = saveSubscriptionData(SUBSCRIPTION_PATH, &SubscriptionData, sub_count, log);
    if (result == FALSE) {
        fprintf(log, "failed to save subscription data\n");
        fclose(log);
        return &result;
    }

    fprintf(log, "successfully subscribed for:\tIP: %s | Port: %d | Article: %s\n", IP, Port, Article);
    fclose(log);
    return &result;
}


/**
 * @brief Unsubscribe function for RPC call. 
 * 
 * Removes a subscription for a client. Subscription article must match the format described by the assignment or function will return false.
 * @param[in] IP IP address of the client.
 * @param[in] Port Port number of the client.
 * @param[in] Article Article to unsubscribe from.
 * @param[in] rqstp Pointer to the service request structure.
 * @return Boolean value indicating success.
 */
bool_t *
unsubscribe_203(char *IP, int Port, char *Article, struct svc_req *rqstp) {
    static bool_t result; //don't know why, but it always has to be static
    result = FALSE;
    //logging
    FILE *log = fopen(LOG_FILE, "a");
    if (log == NULL) {
        fprintf(stderr, "Error opening log file\n");
        exit(1);
    }
    fprintf(log, "Got request to unsubscribe for:\tIP: %s | Port: %d | Article: %s\n", IP, Port, Article);

    checkForSubscriptionDataInitialization();

    result = remove_Subscription(&SubscriptionData, IP, Port, Article, &sub_count, log);

    if (result == FALSE) {
        fprintf(log, "unsubscribe rejected or was unsuccessful for:\tIP: %s | Port: %d | Article: %s\n", IP, Port, Article);
        fclose(log);
        return &result;
    }

    result = saveSubscriptionData(SUBSCRIPTION_PATH, &SubscriptionData, sub_count, log);
    if (result == FALSE) {
        fprintf(log, "failed to save subscription data\n");
        fclose(log);
        return &result;
    }

    fprintf(log, "successfully unsubscribed for:\tIP: %s | Port: %d | Article: %s\n", IP, Port, Article);
    fclose(log);
    return &result;
}

/**
 * @brief Check if the socket for listening for verifications is initialized and initialize it if necessary.
 * @param[in] timeoutTimeForReceive Timeout time for receiving messages.
 * @param[in] log File pointer to the log file.
 */
void checkSocketisInitialized(int timeoutTimeForReceive, FILE *log) {
    //initialize the socket
    static struct sockaddr_in server_addr;
    if (isSocketInitialized == FALSE) {
        // Create a socket for the server
        server_socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (server_socket == -1) {
            fprintf(log, "Failed to create socket\n");
        }

        // Set up the server address
        memset(&server_addr, 0, sizeof (server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // Bind to all interfaces
        server_addr.sin_port = htons((uint16_t) SERVER_PORT);
        fprintf(log, "Initialized Server Socket. Server socket information: %s:%hu\n", inet_ntoa(server_addr.sin_addr), ntohs(server_addr.sin_port));
        // try to bind the server address to the socket
        if (bind(server_socket, (struct sockaddr *) &server_addr, sizeof (server_addr)) < 0) {
            int bind_errno = errno;
            fprintf(log, "socket bind failed for udp communication listening thread: %s\n", strerror(bind_errno));

            // Output the arguments given to bind
            fprintf(log, "Bind arguments: server_socket=%d, server_addr={ sin_family=%hu, sin_addr=%s, sin_port=%hu }\n",
                    server_socket, server_addr.sin_family, inet_ntoa(server_addr.sin_addr), ntohs(server_addr.sin_port));
        }
        // Set up the timeout value to determine how many seconds to wait before a timeout is declared and the thread stops listening for verifications. Any that the thread didn't get a verification from are assumed to not have received the message.
        struct timeval timeout;
        timeout.tv_sec = timeoutTimeForReceive; //how many seconds to wait before a timeout is declared and it stops listening for verifications. Any that i didn't get a verification from are assumed to not have received the message.
        timeout.tv_usec = 0;

        //set the socket to be usable by multiple clients
        int reuse = 1;
        if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
            fprintf(log, "Failed to set SO_REUSEADDR option\n");
        }
        
        // Set the socket option for receive timeout and be reusable to allow multiple sockets to bind to same IP and port
        if (setsockopt(server_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof (timeout)) < 0) {
            fprintf(log, "setsockopt failed\n");
        }
        isSocketInitialized = TRUE;
    }
}

/**
 * @brief Verification thread function for listening to verification messages from clients.
 * 
 * Will listen for a given number of verifications. If no verification is received after a set time, the client is assumed to not have received a communication and saves the message with IP and port of the failed client in failed.txt.
 * @param[in] arg Pointer to the verification thread arguments structure.
 * @return NULL.
 */
void *verificationThread(void *arg) {
    //set up logging
    FILE *log = fopen("verification.log", "a");
    if (log == NULL) {
        fprintf(stderr, "Error opening log file\n");
        exit(1);
    }

    //retrieve the args and set number_of_verifications and verified IPs back to defaults of zero and empty array
    struct verifyThreadArgs* args = (struct verifyThreadArgs*) arg;
    int MaxIPs = args->maxNumberOfIPs;

    //create object to store sender address information in.    
    struct sockaddr_in sender_addr;
    socklen_t len = sizeof (sender_addr);

    int keep_listening = 1;
    int i = 0;

    while (keep_listening == 1 && i < MaxIPs) {
        int verification;

        ssize_t bytes_received = recvfrom(server_socket, &verification, sizeof(int), MSG_WAITALL, (struct sockaddr *) &sender_addr, &len);
        //verification = ntohl(verification); // Convert back to host byte order
        if (bytes_received < 0) {
            // Error occurred or timeout
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                fprintf(log, "Finished Waiting for Verifications\n");
                keep_listening = 0;
                break;
            } else {
                fprintf(log, "recvfrom failed: %s\n", strerror(errno));
                fprintf(log, "exiting listening for verification prematurely.\n");
                break;
            }
        }
        else if (bytes_received == sizeof(int)) {
            /*if (verification == 100) {
                fprintf(log, "Success\n");
            } else {
             * //just not doing this right now.
                fprintf(log, "not successfully fixing the bit order, but did receive a message.\n");
            }
             */
        } else {
            fprintf(log, "Received unexpected data size\n");
        }
        // Message received successfully
        verifiedIPs[i] = sender_addr;
        i++;

        //this part is just for debugging
        fprintf(log, "Received verification from %s:%hu\n", inet_ntoa(sender_addr.sin_addr), ntohs(sender_addr.sin_port));
    }

    number_of_verifications_received = i;
    //close the log
    fflush(log);
    fclose(log);

    return NULL;
}

/**
 * @brief Publish function for RPC call. Publishes an article to subscribed clients.
 * @param[in] IP IP address of the publisher.
 * @param[in] Port Port number of the publisher.
 * @param[in] Article Article to publish.
 * @param[in] rqstp Pointer to the service request structure.
 * @return Boolean value indicating if publication successfully occurred. A success does not indicate if all clients successfully verified receiving articles.
 */
bool_t *
publish_203(char *IP, int Port, char *Article, struct svc_req *rqstp) {
    FILE *log = fopen(LOG_FILE, "a");
    if (log == NULL) {
        fprintf(stderr, "Error opening log file\n");
        exit(1);
    }
    
    fprintf(log, "Got request to publish from:\tIP: %s | Port: %d | Article: %s\n", IP, Port, Article);
    
    //make sure other data structures have been initialized
    checkForClientDataInitialization();
    checkForSubscriptionDataInitialization();
     
    fflush(log);
    static bool_t result = FALSE;
    result = TRUE;

    //create socket if it is not already open
    checkSocketisInitialized(2, log);

    //allocate memory for publication and fill it with correct data by initializing it.
    pubSub publication;
    initialize_publication(&publication, IP, Port, Article, log);
    if (publication.isInvalid == TRUE)
    {
        result = FALSE;
        return &result;
    }
    //search subscriptions to generate a correct list of addresses to send articles to.
    //The listOfAddresses object will increase dynamically if needed
    int starting_max_addresses = 10;
    address *listOfAddresses = malloc(starting_max_addresses * sizeof (address));
    int numAddressesFound = 0; //temp you will have to find
    searchSubscriptions(&SubscriptionData, &listOfAddresses, publication, starting_max_addresses, sub_count, &numAddressesFound, log);
    fprintf(log, "number of addresses found in subscriptions for given publication: %d\n",numAddressesFound);
    
    //make sure the IP and port combination are still joined and is present in clientData. List will expand dynamically if need be.
    int max_addresses_to_send = 10; //this will increase dynamically as needed
    address *listOfAddressesConfirmedinClientData = malloc(max_addresses_to_send * sizeof (address));
    int NumberOfSubscriptionsToSend = 0;
    
    for(int i = 0; i< numAddressesFound; i++)
    {
        int clientIndex = -1; // don't use the index here, but have to pass a value to use function
        if(isClientInList(ClientData, NumberOfKnownClients, listOfAddresses[i].IP, listOfAddresses[i].port, &clientIndex) == 1)
        {
            listOfAddressesConfirmedinClientData[NumberOfSubscriptionsToSend].IP = strdup(listOfAddresses[i].IP);
            listOfAddressesConfirmedinClientData[NumberOfSubscriptionsToSend].port = listOfAddresses[i].port;
            NumberOfSubscriptionsToSend++;
            
            // Reallocate memory for listOfAddressesConfirmedinClientData if needed
            if (NumberOfSubscriptionsToSend == max_addresses_to_send) {
                max_addresses_to_send = (int) (max_addresses_to_send * 1.5) + 1; // Expand capacity by 50%
                listOfAddressesConfirmedinClientData = realloc(listOfAddressesConfirmedinClientData, max_addresses_to_send * sizeof (address));
                if (listOfAddressesConfirmedinClientData == NULL) {
                    fprintf(stderr, "Error: Memory allocation failed\n");
                    exit(1);
                }
            }
        }
    }
    fprintf(log, "number of addresses to send publication to: %d\n",NumberOfSubscriptionsToSend);
   
    
    // we need to lock down publishing for the rest, because verifications can only come in from one deployment at a time with this code.
    pthread_mutex_lock(&publishingLock);

    //reset global variables for listening thread before starting it again
    struct verifyThreadArgs *verifyArgs = malloc(sizeof (struct verifyThreadArgs));
    verifyArgs->maxNumberOfIPs = NumberOfSubscriptionsToSend;
    verifiedIPs = malloc(sizeof (struct sockaddr_in) * NumberOfSubscriptionsToSend);
    number_of_verifications_received = 0;

    // Start verification listening thread
    pthread_t verification_tid;
    int ret;
    ret = pthread_create(&verification_tid, NULL, verificationThread, verifyArgs);
    if (ret != 0) {
        fprintf(log, "Error creating listen thread\n");
    }

    //send messages to IPs
    // I'm not saving the info for each publication message successfully sent out on the disk. If there is a crash on the server, we'll just probably have to assume the clients may or may not have received the articles.
    for (int i = 0; i < NumberOfSubscriptionsToSend; i++) {
        struct sockaddr_in client_addr;
        memset(&client_addr, 0, sizeof (client_addr));
        // Set up the client address
        client_addr.sin_family = AF_INET;
        inet_pton(AF_INET, listOfAddressesConfirmedinClientData[i].IP, &(client_addr.sin_addr)); 
        client_addr.sin_port = htons((uint16_t) listOfAddressesConfirmedinClientData[i].port);

        ssize_t bytes_sent = sendto(server_socket, publication.contents, strlen(publication.contents), 0, (struct sockaddr *) &client_addr, sizeof (client_addr));
        if (bytes_sent <= 0) {
            fprintf(log, "failed on the server side for sendto client IP: %s, port: %d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
            result = FALSE;
        }
    }

    //wait for the verification thread to finish. this should be at the timeout time after the last message received. If any send a verification after, it is too late, and they are determined to not have received the article.
    ret = pthread_join(verification_tid, NULL);
    if (ret != 0) {
        fprintf(log, "Error joining verification listening thread\n");
        result = FALSE;
    }

    //start: just for debugging. you can disable
    //fprintf(log, "Values of IPs that sent verification:\n");
    //for (int i = 0; i < number_of_verifications_received; i++) {
    //    char ipString[INET_ADDRSTRLEN];
    //    const char* ip = inet_ntop(AF_INET, &(verifiedIPs[i].sin_addr), ipString, INET_ADDRSTRLEN);
    //    if (ip == NULL) {
    //        fprintf(stderr, "\tError converting IP address\n");
    //        result = FALSE;
    //        continue;
    //    }

    //    int client_port = ntohs(verifiedIPs[i].sin_port);

    //    fprintf(log, "\tEntry %d: IP=%s, Port=%hu\n", i, ip, client_port);
    //}
    //end of: just for debugging. you can disable

    // Storing articles and sending information in failed.txt for IP and port combinations without a match
    checkForFailedSendsInitialization();
    
    for (int i = 0; i < NumberOfSubscriptionsToSend; i++) {
        // Check if the IP and port combination has a match in listOfAddressesConfirmedinClientData
        int hasMatch = 0;
        for (int j = 0; j < number_of_verifications_received; j++) {
            //convert IP and ports to int and char that match the format in the address struct
            int client_port = ntohs(verifiedIPs[j].sin_port);
            char ipString[INET_ADDRSTRLEN];
            const char* ip = inet_ntop(AF_INET, &(verifiedIPs[j].sin_addr), ipString, INET_ADDRSTRLEN);
                            if (ip == NULL) {
                fprintf(stderr, "\tError converting IP address\n");
                result = FALSE;
                continue;
            }

            //check to see if there is any match of the item in list of addresses in the verified addresses
            if (strcmp(listOfAddressesConfirmedinClientData[i].IP, ip) == 0 && listOfAddressesConfirmedinClientData[i].port == client_port) {
                hasMatch = 1;
                break;
            }
        }
        // If no match is found, send the IP and port combination to failed.txt
        if (hasMatch == 0) {
            addFailedSend(&FailedSends, listOfAddressesConfirmedinClientData[i].IP, listOfAddressesConfirmedinClientData[i].port, publication.contents, &failed_count, &failed_max, log);
            saveFailedData(FAILED_PATH, &FailedSends, failed_count, log);
        }
    }

    // Free the verified IPs, verifyArgs, and temporary publication
    free_pubSub(publication);
    free(verifiedIPs);
    free(verifyArgs);
    fflush(log);

    //free lists of addresses
    for(int i = 0 ; i < numAddressesFound;i++)
    {
        free(listOfAddresses[i].IP);
    }
    free(listOfAddresses);
    for(int i = 0; i < NumberOfSubscriptionsToSend;i++)
    {
        free(listOfAddressesConfirmedinClientData[i].IP);
    }
    free(listOfAddressesConfirmedinClientData);
    
    pthread_mutex_unlock(&publishingLock);


    return &result;
}


/**
 * @brief Update function for RPC call. 
 * 
 * Sends articles in failed.txt that match to the given IP and port combination. It does not verify if articles were received and will remove the article, IP, port combination from failed.txt.
 * @param[in] IP IP address of the client to update.
 * @param[in] Port Port of the client to update.
 * @param[in] rqstp Pointer to the service request structure.
 * @return Boolean indicating success.
 */
bool_t *
update_203(char *IP, int Port, struct svc_req *rqstp)
{
    static bool_t result; //don't know why, but it always has to be static
    result = FALSE;
    //logging
    FILE *log = fopen(LOG_FILE, "a");
    if (log == NULL) {
        fprintf(stderr, "Error opening log file\n");
        exit(1);
    }
    
    fprintf(log, "Got request to update:\tIP: %s | Port: %d\n", IP, Port);
    fflush(log); 
    
    //create socket if it is not already open
    checkSocketisInitialized(2, log);

    //check to see if failedSends object is initialized.
    checkForFailedSendsInitialization();
    
    //intialize articles variable to store articlies that failed to send to the given address. 
    const int max_articles = 100; // max number of articles that can be updated with one update request
    char* articles[max_articles];
    for (int i = 0; i < max_articles; i++) {
        articles[i] = (char*)malloc(124 * sizeof(char));
    }

    //get a list of articles that failed to send to the address given 
    int article_count = retrieve_failed_articles(&FailedSends, failed_count, IP, Port, articles, max_articles);

    //return 1 if there are no failed articles that need to be sent to the address
    if( article_count == 0)
    {
        result = TRUE;
        return (&result);
    }
    //send articles to IP
    // I'm not saving the info for each publication message successfully sent out on the disk. If there is a crash on the server, we'll just probably have to assume the clients may or may not have received the articles.
    for (int i = 0; i < article_count; i++) {
        struct sockaddr_in client_addr;
        memset(&client_addr, 0, sizeof (client_addr));
        
        // Set up the client address
        client_addr.sin_family = AF_INET;
        inet_pton(AF_INET, IP, &(client_addr.sin_addr)); 
        client_addr.sin_port = htons((uint16_t) Port);
        
        char message[256];
        strcpy(message, "DO NOT SEND VERIFICATION:");
        strcat(message, articles[i]);
        

        
        ssize_t bytes_sent = sendto(server_socket, message, strlen(message), 0, (struct sockaddr *) &client_addr, sizeof (client_addr));
        if (bytes_sent <= 0) {
            fprintf(log, "failed on the server side for sendto client IP: %s, port: %d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
            result = FALSE;
        }

    }
    
    //delete articles sent from the failed file
    //delete failed articles
    for (int i = 0; i < article_count; i++){
        deleteFailedSend(&FailedSends, &failed_count, IP, Port, articles[i]);
    }
    saveFailedData(FAILED_PATH, &FailedSends, failed_count, log);

    
    // Free the allocated memory]
    for (int i = 0; i < max_articles; i++) {
        free(articles[i]);
    }
    
    //make sure the log file is updated
    fflush(log); 
    
    result = TRUE;
    return (&result);
}

