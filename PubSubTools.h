/**
 * @file PubSubTools.h
 * @brief Header file for PubSubTools module. Used for pubSub object and useful functions for communicate_server.c.
 */

#ifndef PUBSUBTOOLS_H
#define PUBSUBTOOLS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <ctype.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "communicate.h"

/**
 * @brief Structure representing an address.
 * 
 */
typedef struct {
    char *IP;
    int port;
    int hash;
} address;

/**
 * @brief Structure representing a publication or subscription.
 * 
 */
typedef struct {
    bool_t isInvalid;
    address client_address;
    char *type;
    char *originator;
    char *org;
    char *contents;
    int hashNoAddress;
} pubSub;

/**
 * @brief Number of subscriptions stored.
 * 
 */
extern int sub_count; // number of subscriptions currently active

/**
 * @brief Number of subscriptions allocated for in memory. Dynamically increased with add_subscription if necessary.
 * 
 */
extern int sub_max;

/**
 * @brief Custom strtok function. Needed because strtok alters the string passed to it, and I didn't want to alter the original string.
 * @param str The string to tokenize
 * @param delimiters The delimiter characters
 * @return A pointer to the next token in the string, or NULL if no more tokens are found
 */
char* strtok_custom(char* str, const char* delimiters);

/**
 * @brief Frees the memory allocated for a pubSub object
 * @param p The pubSub object to free
 */
void free_pubSub(pubSub p);

/**
 * @brief Copies the contents of one pubSub object to another
 * @param pubNew The destination pubSub object
 * @param pubOriginal The source pubSub object
 */
void cpyPubSub(pubSub *pubNew, pubSub *pubOriginal);

/**
 * @brief Computes the hash value for a string
 * @param str The string to hash
 * @return The hash value
 */
int hash_string(char *str);

/**
 * @brief Computes the hash value for an IP address and port
 * @param IP The IP address
 * @param Port The port number
 * @return The hash value
 */
int hashAddress(char*IP, int Port);

/**
 * @brief Searches the SubscriptionData for relevant addresses for a given input article and adds them to listOfAddresses.
 * Does not properly utilize the hashes. I would have to setup a hashmap, but i ended up not having the time, and it didn't seem necessary for this project.
 * @param SubscriptionData The array of pubSub objects representing subscriptions
 * @param listOfAddresses The array of address objects to store the matching IP addresses
 * @param sub The pubSub object representing the article to search for
 * @param CapacityOfAddresses The capacity of the listOfAddresses array
 * @param sub_count The number of subscriptions in the SubscriptionData array
 * @param addressCount A pointer to store the number of addresses found
 * @param log The log file
 */
void searchSubscriptions(pubSub **SubscriptionData, address **listOfAddresses, pubSub sub, int CapacityOfAddresses, int sub_count, int * addressCount, FILE *log);

/**
 * @brief Formats the article by removing trailing newline and converting characters to lowercase before the last semicolon
 * @param article The article string to format
 */
void formatArticle(char** article);

/**
 * @brief Parses the article string and populates the pubSub object
 * @param article The article string to parse
 * @param sub The pubSub object to populate
 * @param log The log file
 * @return 1 if parsing is successful, 0 otherwise
 */
int parse_Article(char *article, pubSub *sub, FILE *log);

/**
 * @brief Initializes a pubSub object representing a subscription
 * @param sub The pubSub object to initialize
 * @param IP The IP address
 * @param port The port number
 * @param Article The article string
 * @param log The log file
 */
void initialize_subscription(pubSub *sub, char *IP, int port, char *Article, FILE *log);



/**
 * @brief Adds a new subscription to the SubscriptionData array
 * @param SubscriptionData The array of pubSub objects representing subscriptions
 * Does not properly utilize hashing yet. I would have to setup a hashmap, but i ended up not having the time, and it didn't seem necessary for this project.
 * @param IP The IP address
 * @param port The port number
 * @param Article The article string
 * @param sub_count A pointer to the number of subscriptions in the array
 * @param sub_max A pointer to the maximum capacity of the array
 * @param log The log file
 * @return 1 if the subscription is added successfully, 0 otherwise
 */
int add_Subscription(pubSub **SubscriptionData, char *IP, int port, char *Article, int* sub_count, int* sub_max, FILE *log);

/**
 * @brief Adds a failed send to the FailedSends array and saves it to the disk as a backup.
 * 
 * @param[in,out] FailedSends Pointer to the array of FailedSends.
 * @param[in] IP IP address of the client.
 * @param[in] Port Port number of the client.
 * @param[in] contents Contents of the failed send.
 * @param[in,out] failed_count Pointer to the count of failed sends.
 * @param[in,out] failed_max Pointer to the maximum number of failed sends.
 * @param[in] log Pointer to the log file.
 * @return 1 if the failed send was added successfully, 0 otherwise.
 */
int addFailedSend(pubSub ** FailedSends, char * IP, int  Port, char* contents, int *failed_count, int* failed_max, FILE *log);

/**
 * @brief Deletes a failed send from the FailedSends array and from the disk.
 * 
 * @param[in,out] FailedSends Pointer to the array of Failed Sends.
 * @param[in,out] failed_count Pointer to the count of failed sends.
 * @param[in] IP IP address of the client.
 * @param[in] Port Port number of the client.
 * @param[in] contents Contents of the failed send.
 * @return 1 if the failed send was deleted successfully, 0 otherwise.
 */
int deleteFailedSend(pubSub** FailedSends, int* failed_count, char * IP, int  Port, char* contents);

/**
 * @brief Initializes a pubSub object for a publication with the given IP, port, and article.
 * 
 * @param[out] pub Pointer to the publication.
 * @param[in] IP IP address of the client.
 * @param[in] port Port number of the client.
 * @param[in] Article Contents of the article.
 * @param[in] log Pointer to the log file.
 */
void initialize_publication(pubSub *pub, char *IP, int port, char *Article, FILE *log);


/**
 * @brief Removes a subscription from the SubscriptionData array.
 * 
 * Does not properly utilize the hashes yet. I would have to setup a hashmap, but i ended up not having the time, and it didn't seem necessary for this project.
 * @param[in,out] SubscriptionData Pointer to the array of subscriptions pubSub objects.
 * @param[in] IP IP address of the client.
 * @param[in] port Port number of the client.
 * @param[in] Article Contents of the subscription.
 * @param[in,out] sub_count Pointer to the count of subscriptions.
 * @param[in] log Pointer to the log file.
 * @return 1 if the subscription was removed successfully, 0 otherwise.
 */
int remove_Subscription(pubSub **SubscriptionData, char *IP, int port, char *Article, int* sub_count, FILE *log);

 /**
 * @brief Checks if a client exists in ClientData.
 * 
 * If a match is found, updates the destination client index to the correct location.
 * @param[in] ClientData Pointer to the array of ClientData.
 * @param[in] NumberOfKnownClients Number of known clients.
 * @param[in] IP IP address of the client.
 * @param[in] Port Port number of the client.
 * @param[out] destClientIndex Pointer to the variable storing the destination client index.
 * @return True (1) if the client exists, False (0) otherwise.
 */
int isClientInList(char***ClientData, int NumberOfKnownClients, char *IP, int Port, int *destClientIndex);

/**
 * @brief Reads the client data from the saved file.
 * @param[in] clientDataFilepath Path to the client data file.
 * @param[out] ClientData Pointer to the array of ClientData.
 * @param[in] log Pointer to the log file.
 * @return The number of known clients as an int.
 */
int readClientData(char * clientDataFilepath, char ***ClientData, FILE *log);

/**
 * @brief Saves the client data to the file.
 * 
 * @param[in] clientDataFilepath Path to the client data file.
 * @param[in] ClientData Pointer to the array of ClientData.
 * @param[in] NumberOfKnownClients Number of known clients.
 * @param[in] log Pointer to the log file.
 * @return 1 if the client data was saved successfully, 0 otherwise.
 */
int saveClientData(char * clientDataFilepath, char ***ClientData, int NumberOfKnownClients, FILE *log);

/**
 * @brief Reads the subscription data from the saved file.
 * 
 * @param[in] subscriptionFilePath Path to the subscription data file.
 * @param[out] SubscriptionData Pointer to the array of subscriptions.
 * @param[in,out] sub_count Pointer to the count of subscriptions.
 * @param[in,out] sub_max Pointer to the maximum number of subscriptions.
 * @param[in] log Pointer to the log file.
 * @return 1 if the subscription data was read successfully, 0 otherwise.
 */
int readSubscriptionData(char * subscriptionFilePath, pubSub ** SubscriptionData, int * sub_count, int * sub_max, FILE *log);

/**
 * @brief Saves the subscription data to the file.
 * 
 * @param[in] subscriptionFilePath Path to the subscription data file.
 * @param[in] SubscriptionData Pointer to the array of subscriptions.
 * @param[in] sub_count Number of subscriptions.
 * @param[in] log Pointer to the log file.
 * @return 1 if the subscription data was saved successfully, 0 otherwise.
 */
int saveSubscriptionData(char * subscriptionFilePath, pubSub ** SubscriptionData, int sub_count, FILE *log);

/**
 * @brief Reads the failed data from the saved file.
 * 
 * @param[in] FailedFilePath Path to the failed data file.
 * @param[out] FailedData Pointer to the array of failed data.
 * @param[in,out] failed_count Pointer to the count of failed data.
 * @param[in,out] failed_max Pointer to the maximum number of failed data.
 * @param[in] log Pointer to the log file.
 * @return 1 if the failed data was read successfully, 0 otherwise.
 */
int readFailedData(char * FailedFilePath, pubSub ** FailedData, int * failed_count, int* failed_max, FILE *log);

/**
 * @brief Retrieves the failed sent articles for a given client.
 * 
 * @param[in] FailedSends Pointer to the array of FailedSends to retrieve from.
 * @param[in] failed_count int to store the number of failed articles
 * @param[in] ip IP address of the client
 * @param[in] port Port of the client
 * @param[in] articles object to store the failed articles within
 * @param[in] max_articles max number of articles that can be stored in the articles object
 * @return int The number of failed articles retrieved.
 */
int retrieve_failed_articles(pubSub ** FailedSends, int failed_count, const char* ip, int port, char** articles, int max_articles);


/**
 * @brief Saves the failed data to the file.
 * 
 * @param[in] FailedFilePath Path to the failed data file.
 * @param[in] FailedData Pointer to the array of failed data.
 * @param[in] failed_count Number of failed data.
 * @param[in] log Pointer to the log file.
 * @return 1 if the failed data was saved successfully, 0 otherwise.
 */
int saveFailedData(char * failedFilePath, pubSub ** FailedData, int failed_count, FILE *log);


#endif /* PUBSUBTOOLS_H */
