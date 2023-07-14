/**
 * @file PubSubTools.c
 * @brief mainfile for pubSub object and useful functions for communicate_server.c
 */

#include "PubSubTools.h"

/**
 * @brief Array of valid types for subscriptions
 */
const char *valid_types[] = {"", "sports", "lifestyle", "entertainment", "business", "technology", "science", "politics", "health"};


/**
 * @brief Custom strtok function. Needed because strtok alters the string passed to it, and I didn't want to alter the original string.
 * @param str The string to tokenize
 * @param delimiters The delimiter characters
 * @return A pointer to the next token in the string, or NULL if no more tokens are found
 */
char* strtok_custom(char* str, const char* delimiters) {
    static char* token = NULL;
    if (str != NULL)
        token = str;

    if (token == NULL)
        return NULL;

    char* start = token;
    char* end = strpbrk(token, delimiters);

    if (end != NULL) {
        *end = '\0';
        token = end + 1;
    } else {
        token = NULL;
    }

    return start;
}

/**
 * @brief Frees the memory allocated for a pubSub object
 * @param p The pubSub object to free
 */
void free_pubSub(pubSub p) {
    free(p.client_address.IP);
    free(p.type);
    free(p.originator);
    free(p.org);
    free(p.contents);
}

/**
 * @brief Copies the contents of one pubSub object to another
 * @param pubNew The destination pubSub object
 * @param pubOriginal The source pubSub object
 */
void cpyPubSub(pubSub *pubNew, pubSub *pubOriginal) {
    pubNew->isInvalid = pubOriginal->isInvalid;
    strcpy(pubNew->type, pubOriginal->type);
    strcpy(pubNew->originator, pubOriginal->originator);
    strcpy(pubNew->org, pubOriginal->org);
    strcpy(pubNew->contents, pubOriginal->contents);
    pubNew->hashNoAddress = pubOriginal->hashNoAddress;
    strcpy(pubNew->client_address.IP, pubOriginal->client_address.IP);
    pubNew->client_address.port = pubOriginal->client_address.port;
    pubNew->client_address.hash = pubOriginal->client_address.hash;
}

/**
 * @brief Computes the hash value for a string
 * @param str The string to hash
 * @return The hash value
 */
int hash_string(char *str) {
    int hash = 5381;
    int c;
    while (*str != '\0') {
        c = *str++;
        hash = ((hash << 5) + hash) + c; // djb2 hash function
    }
    return hash;
}

/**
 * @brief Computes the hash value for an IP address and port
 * @param IP The IP address
 * @param Port The port number
 * @return The hash value
 */
int hashAddress(char*IP, int Port) {

    // convert the integer port to a string
    char portStr[6]; // assuming max 5 digits for the port
    sprintf(portStr, "%d", Port);

    // allocate memory for the combined string
    char * combined = malloc(strlen(IP) + 1 + strlen(portStr) + 1); // +1 for the space and +1 for the null terminator
    strlcpy(combined, IP, 60); // copy the first string
    strlcat(combined, " ", 60); // concatenate a space between IP and Port (123 2 is different than 12 32, but without it'd treat as 1232 no matter what)
    strlcat(combined, portStr, 60); // concatenate the second string

    int result = hash_string(combined);

    // free the allocated memory
    free(combined);

    return result;
}

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
void searchSubscriptions(pubSub **SubscriptionData, address **listOfAddresses, pubSub sub, int CapacityOfAddresses, int sub_count, int * addressCount, FILE *log) {
    int i, j;
    *addressCount = 0;
    int capacity = CapacityOfAddresses; // Track the capacity of the listOfAddresses array
    for (i = 0; i < sub_count; i++) {
        // Iterate through SubscriptionData to find matching subscriptions
        if ((strcmp((*SubscriptionData)[i].type, "") == 0 || strcmp(sub.type, (*SubscriptionData)[i].type) == 0)
                && (strcmp((*SubscriptionData)[i].originator, "") == 0 || strcmp(sub.originator, (*SubscriptionData)[i].originator) == 0)
                && (strcmp((*SubscriptionData)[i].org, "") == 0 || strcmp(sub.org, (*SubscriptionData)[i].org) == 0)) {
            // Check if the address is already in listOfAddresses by checking the hashes (not trying to use hashes anymore. just comparing values)
            // The address may already be in the list of Addresses, because more than one subscription can result in the article being sent,
            // but you only want one article sent to each address.      
            for (j = 0; j < *addressCount; j++) {
                if (strcmp((*listOfAddresses)[j].IP,(*SubscriptionData)[i].client_address.IP) == 0 
                        && (*listOfAddresses)[j].port == (*SubscriptionData)[i].client_address.port) {
                    break;
                }
            }
            if (j == *addressCount) {
                // Reallocate memory for listOfAddresses if needed
                if (*addressCount >= capacity) {
                    capacity = (int) (capacity * 1.5) + 1; // Expand capacity by 50%
                    *listOfAddresses = realloc(*listOfAddresses, capacity * sizeof (address));
                    if (*listOfAddresses == NULL) {
                        fprintf(stderr, "Error: Memory allocation failed\n");
                        exit(1);
                    }
                }
                // Add the new address to listOfAddresses since it was not found in listOfAddresses
                (*listOfAddresses)[*addressCount].IP = strdup((*SubscriptionData)[i].client_address.IP);
                (*listOfAddresses)[*addressCount].port = (*SubscriptionData)[i].client_address.port;
                (*listOfAddresses)[*addressCount].hash = (*SubscriptionData)[i].client_address.hash;
                *addressCount = *addressCount + 1;
            }
        }
    }
}

/**
 * @brief Formats the article by removing trailing newline and converting characters to lowercase before the last semicolon
 * @param article The article string to format
 */
void formatArticle(char** article) {
    //get rid of \n if it ends the article (likely due to console returning usually putting in a /n
    if ((*article)[strlen(*article) - 1] == '\n') {
        (*article)[strlen(*article) - 1] = '\0';
    }
    char* lastSemicolon = strrchr(*article, ';'); // Find the last semicolon
    if (lastSemicolon != NULL) {
        // Convert characters to lowercase before the last semicolon
        for (char* p = *article; p < lastSemicolon; p++) { //is address of p less than address of lastSemicolon
            *p = tolower(*p);
        }
    }
}

/**
 * @brief Parses the article string and populates the pubSub object
 * @param article The article string to parse
 * @param sub The pubSub object to populate
 * @param log The log file
 * @return 1 if parsing is successful, 0 otherwise
 */
int parse_Article(char *article, pubSub *sub, FILE *log) {
    //strtok alters article, so we will create a duplicate.
    char * articleNew = malloc(121 * sizeof (char));
    strcpy(articleNew, article);
    char* tokens[4];
    char* token = strtok_custom(articleNew, ";");
    int count = 0;
    while (token != NULL) {
        tokens[count] = token;
        count++;
        token = strtok_custom(NULL, ";");
    }
    if (count == 4) {
        //convert to lower case (contents remains unchanged)
        for (int i = 0; i < 3; i++) {
            for (int j = 0; tokens[i][j]; j++) {
                tokens[i][j] = tolower(tokens[i][j]);
            }
        }
        bool_t isValidType = FALSE;
        for (int j = 0; j < sizeof (valid_types) / sizeof (valid_types[0]); j++) {
            if (strcmp(tokens[0], valid_types[j]) == 0) {
                sub->type = strdup(tokens[0]);
                isValidType = TRUE;
            }
        }
        if (isValidType == FALSE) {
            fprintf(log, "Error: first argument must be empty or one of the following types: Sports, Lifestyle, Entertainment, Business, Technology, Science, Politics, Health\n");
            sub->isInvalid = TRUE;
            return 0;
        }
        sub->originator = strdup(tokens[1]);
        sub->org = strdup(tokens[2]);
        sub->contents = strdup(tokens[3]);
    } else if (count < 4) {
        fprintf(log, "Error: expected 4 arguments in Article, received %d\n", count);
        sub->isInvalid = TRUE;
        return 0;
    } else {
        fprintf(log, "Error: expected 4 arguments in Article, received more than 4.\n", count);
        sub->isInvalid = TRUE;
        return 0;
    }
    if (strlen(sub->type) == 0 && strlen(sub->org) == 0 && strlen(sub->originator) == 0) {
        fprintf(log, "Error: at least one of the first three arguments must not be empty.\n");
        sub->isInvalid = TRUE;
        return 0;
    }
    return 1;
}

/**
 * @brief Initializes a pubSub object representing a subscription
 * @param sub The pubSub object to initialize
 * @param IP The IP address
 * @param port The port number
 * @param Article The article string
 * @param log The log file
 */
void initialize_subscription(pubSub *sub, char *IP, int port, char *Article, FILE *log) {
    //assign IP, port, and create a hash for them
    sub->client_address.IP = strdup(IP);
    sub->client_address.port = port;
    sub->client_address.hash = hashAddress(IP, port);

    //universal requirements for article are in the parse_article function
    if (parse_Article(Article, sub, log) == 0) {
        sub->isInvalid = TRUE;
        return;
    };
    sub->hashNoAddress = hash_string(Article);
    // subscription only requirements for article are checked here
    if (strlen(sub->contents) != 0) {
        fprintf(log, "Error: Incorrectly formatted Article.\nError: Subscription cannot have contents (i.e. the 4th argument must be empty) in the Article.\n");
        sub->isInvalid = TRUE;
    } else {
        sub->isInvalid = FALSE;
    }
    return;
}



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
int add_Subscription(pubSub **SubscriptionData, char *IP, int port, char *Article, int* sub_count, int* sub_max, FILE *log) {
    //check if the limit is met for number of subscriptions. Reallocate more memory and increase max by 50% if so.
    formatArticle(&Article);
    if (*sub_count == *sub_max) {
        *sub_max = *sub_count + (*sub_count / 2); // increase by 50%
        pubSub *new_subs = (pubSub*) realloc(*SubscriptionData, *sub_max * sizeof (pubSub)); // allocate new memory
        if (new_subs == NULL) {
            fprintf(log, "failed to allocate new memory for SubscriptionData allocation increase.\n");
            return 0;
        }
        *SubscriptionData = new_subs; // update pointer to new memory only if realloc does not fail
    }
    
    //create subscription 
    initialize_subscription(&((*SubscriptionData)[*sub_count]), IP, port, Article, log);
    if ((*SubscriptionData)[*sub_count].isInvalid == 1) {
        fprintf(log, "failed to create subscription and thus also to add it.\n");
        return 0;
    }

    //check to make sure that the subscription doesn't already exist 
    for (int i = 0; i < *sub_count; i++) {
        if ((*SubscriptionData)[*sub_count].hashNoAddress == (*SubscriptionData)[i].hashNoAddress &&
                (*SubscriptionData)[*sub_count].client_address.hash == (*SubscriptionData)[i].client_address.hash) {
            //subscription already exists
            free_pubSub((*SubscriptionData)[*sub_count]);
            return 0;
        }
    }
    *sub_count = *sub_count + 1;
    return 1;
}

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
int addFailedSend(pubSub ** FailedSends, char * IP, int  Port, char* contents, int *failed_count, int* failed_max, FILE *log) {
    //check if the limit is met for number of publications. Reallocate more memory and increase max by 50% if so.
    if (*failed_count == *failed_max) {
        *failed_max = *failed_count + (*failed_count / 2); // increase by 50%
        pubSub *new_pubs = (pubSub*) realloc(*FailedSends, *failed_max * sizeof (pubSub)); // allocate new memory
        if (new_pubs == NULL) {
            fprintf(log, "failed to allocate new memory for FailedSends allocation increase.\n");
            return 0;
        }

        *FailedSends = new_pubs; // update pointer to new memory only if realloc does not fail
    }
    //allocate memory and add failed send
    (*FailedSends)[*failed_count].client_address.IP = strdup(IP);
    (*FailedSends)[*failed_count].client_address.port = Port;
    (*FailedSends)[*failed_count].contents = strdup(contents);

    *failed_count = *failed_count + 1;
    return 1;
}

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
int deleteFailedSend(pubSub** FailedSends, int* failed_count, char * IP, int  Port, char* contents) {
    int index = -1; // Variable to store the index of the matching failedSend, initialized to -1

    // Find the index of the matching failedSend
    for (int i = 0; i < *failed_count; i++) {
        if (strcmp((*FailedSends)[i].contents, contents) == 0 &&
                strcmp((*FailedSends)[i].client_address.IP, IP) == 0 &&
                (*FailedSends)[i].client_address.port == Port) {
            index = i;
            break;
        }
    }
    if (index != -1) {
        // Free the memory associated with the matching failedSend
        free((*FailedSends)[index].client_address.IP);
        free((*FailedSends)[index].contents);

        // Shift the elements after the index to the left
        for (int i = index; i < *failed_count - 1; i++) {
            (*FailedSends)[i] = (*FailedSends)[i + 1];
        }

        // Decrement the failed count
        (*failed_count)--;
        return 1;
    }
    else
    {
        return 0;
    }
}

/**
 * @brief Initializes a pubSub object for a publication with the given IP, port, and article.
 * 
 * @param[out] pub Pointer to the publication.
 * @param[in] IP IP address of the client.
 * @param[in] port Port number of the client.
 * @param[in] Article Contents of the article.
 * @param[in] log Pointer to the log file.
 */
void initialize_publication(pubSub *pub, char *IP, int port, char *Article, FILE *log) {
    //assign IP, port, and create a hash for them
    pub->client_address.IP = strdup(IP);
    pub->client_address.port = port;
    pub->client_address.hash = hashAddress(IP, port);

    //universal requirements for article are in the parse_article function
    if (parse_Article(Article, pub, log) == 0) {
        pub->isInvalid = TRUE;
        return;
    };
    pub->hashNoAddress = hash_string(Article);
    // subscription only requirements for article are checked here
    if (strlen(pub->contents) == 0) {
        fprintf(log, "Error: Publication must have contents (i.e. the 4th argument must not be empty) in the Article.\n");
        pub->isInvalid = TRUE;
    } else {
        pub->isInvalid = FALSE;
    }
    return;
}


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
int remove_Subscription(pubSub **SubscriptionData, char *IP, int port, char *Article, int* sub_count, FILE *log) {
    //check if the limit is met for number of subscriptions. Reallocate more memory and increase max by 50% if so.
    formatArticle(&Article);
    pubSub temp;
    int inidex_of_subscription = -1;
    //create subscription 
    initialize_subscription(&temp, IP, port, Article, log);
    if (temp.isInvalid == 1) {
        fprintf(log, "failed to create subscription and thus also to add it.\n");
        free_pubSub(temp);
        return 0;
    }

    //check to make sure that the subscription exists
    for (int i = 0; i < *sub_count; i++) {
        if (temp.hashNoAddress == (*SubscriptionData)[i].hashNoAddress &&
                temp.client_address.hash == (*SubscriptionData)[i].client_address.hash) {
            //subscription exists
            inidex_of_subscription = i;
            break;
        }
    }
    //If the subscription exists, I will delete it and move all the items to the left
    //I don't decrease the size of the Subscription Table
    if (inidex_of_subscription != -1) {
        free_pubSub((*SubscriptionData)[inidex_of_subscription]);
        *sub_count = *sub_count - 1;
        for (int i = inidex_of_subscription; i < *sub_count - 1; i++) {
            SubscriptionData[i] = SubscriptionData[i + 1];
        }
        free_pubSub(temp);
        return 1;
    }
    //the subscription wasn't found
    free_pubSub(temp);
    return 0;

}

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
int isClientInList(char***ClientData, int NumberOfKnownClients, char *IP, int Port, int *destClientIndex) {
    int foundClient = FALSE;
    for (int i = 0; i < NumberOfKnownClients; i++) {
        int endLoop = FALSE;
        if (foundClient == FALSE) {
            if (strcmp(ClientData[i][0], IP) == 0) {
                if (Port == atoi(ClientData[i][1])) {
                    foundClient = TRUE;
                    *destClientIndex = i;
                    endLoop = TRUE;
                    return TRUE;
                    break;

                }
            }
        }
    }
    return FALSE;
}

/**
 * @brief Reads the client data from the saved file.
 * @param[in] clientDataFilepath Path to the client data file.
 * @param[out] ClientData Pointer to the array of ClientData.
 * @param[in] log Pointer to the log file.
 * @return The number of known clients as an int.
 */
int readClientData(char * clientDataFilepath, char ***ClientData, FILE *log) {
    // Open file in read mode
    FILE *clientFileStream;
    clientFileStream = fopen(clientDataFilepath, "r");

    if (NULL == clientFileStream) {
        fprintf(log, "Client Data file can't be opened \n");

        return 0;
    }
    // Try acquiring shared lock on file, waiting until it is available
    int attempts = 0;
    const int maxLockAttempts = 25;
    const int lockWaitTimeSeconds = 1;
    struct flock fl = {F_RDLCK, SEEK_SET, 0, 0, 0};
    while (fcntl(fileno(clientFileStream), F_SETLK, &fl) == -1) {
        if (++attempts > maxLockAttempts) {
            fprintf(log, "Timed out waiting for shared lock on ClientData.txt\n");
            fclose(clientFileStream);

            return -1;
        }
        sleep(lockWaitTimeSeconds); // wait 1 second before trying again
    }
    // Update number of subscribers stored as the first line in the Subscriber Data file
    char firstLine[5], *result;
    result = fgets(firstLine, 5, clientFileStream);
    if (result == NULL) {
        fprintf(log, "No client data to read in the file\n");
        fl.l_type = F_UNLCK;
        fcntl(fileno(clientFileStream), F_SETLK, &fl);
        fclose(clientFileStream);

        return 0;
    }
    int NumberOfKnownClients = atoi(result);
    if (NumberOfKnownClients > 0) {
        int i = 0;
        while (fscanf(clientFileStream, "%s %s", ClientData[i][0], ClientData[i][1]) == 2) {
            i++;
        }
    }
    // Release shared lock on file
    fl.l_type = F_UNLCK;
    fcntl(fileno(clientFileStream), F_SETLK, &fl);

    // Close the file
    fclose(clientFileStream);


    return NumberOfKnownClients;
}

/**
 * @brief Saves the client data to the file.
 * 
 * @param[in] clientDataFilepath Path to the client data file.
 * @param[in] ClientData Pointer to the array of ClientData.
 * @param[in] NumberOfKnownClients Number of known clients.
 * @param[in] log Pointer to the log file.
 * @return 1 if the client data was saved successfully, 0 otherwise.
 */
int saveClientData(char * clientDataFilepath, char ***ClientData, int NumberOfKnownClients, FILE *log) {
    int clientFileDescriptor;
    struct flock fl;
    int lockAttempts = 0;
    const int maxLockAttempts = 10;
    const int lockWaitTimeSeconds = .1;
    // Wait until file is unlocked or max attempts reached
    while (lockAttempts < maxLockAttempts) {
        clientFileDescriptor = open(clientDataFilepath, O_WRONLY | O_EXCL | O_TRUNC, 0644);

        if (clientFileDescriptor >= 0) {
            // Acquire exclusive lock
            fl.l_type = F_WRLCK;
            fl.l_whence = SEEK_SET;
            fl.l_start = 0;
            fl.l_len = 0;
            if (fcntl(clientFileDescriptor, F_SETLK, &fl) == -1) {
                fprintf(log, "Failed to acquire exclusive lock on file\n");
                close(clientFileDescriptor);

                return 0;
            }
            // writing to the file
            char numToSave[15];
            sprintf(numToSave, "%d", NumberOfKnownClients);
            dprintf(clientFileDescriptor, "%s\n", numToSave);
            for (int i = 0; i < NumberOfKnownClients; i++) {
                dprintf(clientFileDescriptor, "%s %s\n", ClientData[i][0], ClientData[i][1]);
            }
            // Release exclusive lock
            fl.l_type = F_UNLCK;
            if (fcntl(clientFileDescriptor, F_SETLK, &fl) == -1) {
                fprintf(log, "Failed to release exclusive lock on file\n");
                close(clientFileDescriptor);

                return 0;
            }
            // Closing the file
            close(clientFileDescriptor);

            return 1;
        }
        // Wait for file to be unlocked
        sleep(lockWaitTimeSeconds);
        lockAttempts = lockAttempts + 1;
    }
    fprintf(log, "Timed out waiting for exclusive lock on ClientData.txt\n");
    return 0;
}

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
int readSubscriptionData(char * subscriptionFilePath, pubSub ** SubscriptionData, int * sub_count, int * sub_max, FILE *log) {
    // Open file in read mode
    FILE *subscriptionFileStream;
    subscriptionFileStream = fopen(subscriptionFilePath, "r");

    if (NULL == subscriptionFileStream) {
        //try creating the file
        subscriptionFileStream = fopen(subscriptionFilePath, "w");
        if (subscriptionFileStream != NULL) {
            fclose(subscriptionFileStream);
            subscriptionFileStream = fopen(subscriptionFilePath, "r");
            if (NULL == subscriptionFileStream) {
                fprintf(log, "Subscription Data file can't be opened \n");
                return 0;
            }
        } else {
            fprintf(log, "Subscription Data file can't be opened \n");
            return 0;
        }
    }

    // Try acquiring shared lock on file, waiting until it is available
    int attempts = 0;
    const int maxLockAttempts = 25;
    const int lockWaitTimeSeconds = 1;
    struct flock fl = {F_RDLCK, SEEK_SET, 0, 0, 0};
    while (fcntl(fileno(subscriptionFileStream), F_SETLK, &fl) == -1) {
        if (++attempts > maxLockAttempts) {
            fprintf(log, "Timed out waiting for shared lock on %s",subscriptionFilePath);
            fclose(subscriptionFileStream);
            return 0;
        }
        sleep(lockWaitTimeSeconds); // wait 1 second before trying again
    }
    
    // don't use result anymore. I just use it to monitor the entry count in text editor.
    // Check to see if the contents of the file is empty by checking to see if the first line is empty
    char firstLine[5], *result;
    result = fgets(firstLine, 5, subscriptionFileStream);
    if (result == NULL) {
        fprintf(log, "No subscription data to read in the file\n");
        fl.l_type = F_UNLCK;
        fcntl(fileno(subscriptionFileStream), F_SETLK, &fl);
        fclose(subscriptionFileStream);
        return 0;
    }
    char* IP = malloc(16 * sizeof (char)); // allocate memory for IP (assuming it's an IPv4 address)
    char* portStr = malloc(16 * sizeof (char));
    char* article = malloc(121 * sizeof (char)); // allocate memory for article (assuming it's no longer than 120 characters +1 for \0 character)
    while (fscanf(subscriptionFileStream, "%s %s %s", IP, portStr, article) == 3) {
        int port = atoi(portStr);
        add_Subscription(SubscriptionData, IP, port, article, sub_count, sub_max, log);
    }
    free(portStr);
    free(IP);
    free(article);
    // Release shared lock on file
    fl.l_type = F_UNLCK;
    fcntl(fileno(subscriptionFileStream), F_SETLK, &fl);

    // Close the file
    fclose(subscriptionFileStream);
    return 1;
}

/**
 * @brief Saves the subscription data to the file.
 * 
 * @param[in] subscriptionFilePath Path to the subscription data file.
 * @param[in] SubscriptionData Pointer to the array of subscriptions.
 * @param[in] sub_count Number of subscriptions.
 * @param[in] log Pointer to the log file.
 * @return 1 if the subscription data was saved successfully, 0 otherwise.
 */
int saveSubscriptionData(char * subscriptionFilePath, pubSub ** SubscriptionData, int sub_count, FILE *log) {
    int subscriptionFileDescriptor;
    struct flock fl;
    int lockAttempts = 0;
    const int maxLockAttempts = 10;
    const int lockWaitTimeSeconds = 0.5;
    // Wait until file is unlocked or max attempts reached
    while (lockAttempts < maxLockAttempts) {
        subscriptionFileDescriptor = open(subscriptionFilePath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (subscriptionFileDescriptor >= 0) {
            // Acquire exclusive lock
            fl.l_type = F_WRLCK;
            fl.l_whence = SEEK_SET;
            fl.l_start = 0;
            fl.l_len = 0;
            if (fcntl(subscriptionFileDescriptor, F_SETLK, &fl) == -1) {
                fprintf(log, "Failed to acquire exclusive lock on file");
                close(subscriptionFileDescriptor);
                return 0;
            }

            char numToSave[15];
            sprintf(numToSave, "%d", sub_count);
            dprintf(subscriptionFileDescriptor, "%s\n", numToSave);
            for (int i = 0; i < sub_count; i++) {
                char portStr[6]; // assuming max 5 digits for the port
                sprintf(portStr, "%d", (*SubscriptionData)[i].client_address.port);
                dprintf(subscriptionFileDescriptor, "%s %s %s;%s;%s;%s\n",
                        (*SubscriptionData)[i].client_address.IP,
                        portStr,
                        (*SubscriptionData)[i].type,
                        (*SubscriptionData)[i].originator,
                        (*SubscriptionData)[i].org,
                        (*SubscriptionData)[i].contents);
            }
            // Release exclusive lock
            fl.l_type = F_UNLCK;
            if (fcntl(subscriptionFileDescriptor, F_SETLK, &fl) == -1) {
                fprintf(log, "Failed to release exclusive lock on file\n");
                close(subscriptionFileDescriptor);
                return 0;
            }
            // Closing the file
            close(subscriptionFileDescriptor);
            return 1;
        }
        // Wait for file to be unlocked
        sleep(lockWaitTimeSeconds);
        lockAttempts++;
    }
    fprintf(log, "Timed out waiting for exclusive lock on %s\n",subscriptionFilePath);
    return 0;
}

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
int readFailedData(char * FailedFilePath, pubSub ** FailedData, int * failed_count, int* failed_max, FILE *log) {
    // Open file in read mode
    FILE *failedFileStream;
    failedFileStream = fopen(FailedFilePath, "r");

    if (NULL == failedFileStream) {
        //try creating the file
        failedFileStream = fopen(FailedFilePath, "w");
        if (failedFileStream != NULL) {
            fclose(failedFileStream);
            failedFileStream = fopen(FailedFilePath, "r");
            if (NULL == failedFileStream) {
                fprintf(log, "Failed Data file can't be opened \n");
                return 0;
            }
        } else {
            fprintf(log, "Failed Data file can't be opened \n");
            return 0;
        }
    }

    // Try acquiring shared lock on file, waiting until it is available
    int attempts = 0;
    const int maxLockAttempts = 25;
    const int lockWaitTimeSeconds = 1;
    struct flock fl = {F_RDLCK, SEEK_SET, 0, 0, 0};
    while (fcntl(fileno(failedFileStream), F_SETLK, &fl) == -1) {
        if (++attempts > maxLockAttempts) {
            fprintf(log, "Timed out waiting for shared lock on %s",FailedFilePath);
            fclose(failedFileStream);
            return 0;
        }
        sleep(lockWaitTimeSeconds); // wait 1 second before trying again
    }
    
    // don't use result anymore. I just use it to monitor the entry count in text editor.
    // Check to see if the contents of the file is empty by checking to see if the first line is empty
    char firstLine[5], *result;
    result = fgets(firstLine, 5, failedFileStream);
    if (result == NULL) {
        fprintf(log, "No failed data to read in the file\n");
        fl.l_type = F_UNLCK;
        fcntl(fileno(failedFileStream), F_SETLK, &fl);
        fclose(failedFileStream);
        return 0;
    }
    
    //int failed_in_storage = atoi(result); //I don't use this number anymore, but it's not necessary to update the code to remove it.
    char* IP = malloc(16 * sizeof (char)); // allocate memory for IP (assuming it's an IPv4 address)
    char* portStr = malloc(16 * sizeof (char));
    char* contents = malloc(124 * sizeof (char)); // allocate memory for article (assuming it's no longer than 120 characters +1 for \0 character)
    while (fscanf(failedFileStream, "IP: %s | Port: %s | Contents: %[^\n]\n", IP, portStr, contents) == 3) {
        int port = atoi(portStr);
        addFailedSend(FailedData, IP, port, contents, failed_count, failed_max, log);
    }
    free(portStr);
    free(IP);
    free(contents);
    // Release shared lock on file
    fl.l_type = F_UNLCK;
    fcntl(fileno(failedFileStream), F_SETLK, &fl);

    // Close the file
    fclose(failedFileStream);
    return 1;
}

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
int retrieve_failed_articles(pubSub ** FailedSends, int failed_count, const char* ip, int port, char** articles, int max_articles) {
    int article_count = 0;
    int i = 0;
    while (i < failed_count && article_count < max_articles) {
        if (strcmp((*FailedSends)[i].client_address.IP, ip) == 0 && (*FailedSends)[i].client_address.port == port) {
            strncpy(articles[article_count], (*FailedSends)[i].contents,127);
            article_count++;
        }
        i++;
    }
    return article_count;
}

/**
 * @brief Saves the failed data to the file.
 * 
 * @param[in] FailedFilePath Path to the failed data file.
 * @param[in] FailedData Pointer to the array of failed data.
 * @param[in] failed_count Number of failed data.
 * @param[in] log Pointer to the log file.
 * @return 1 if the failed data was saved successfully, 0 otherwise.
 */
int saveFailedData(char * failedFilePath, pubSub ** FailedData, int failed_count, FILE *log) {
    int failedFileDescriptor;
    struct flock fl;
    int lockAttempts = 0;
    const int maxLockAttempts = 10;
    const int lockWaitTimeSeconds = 0.5;
    // Wait until file is unlocked or max attempts reached
    while (lockAttempts < maxLockAttempts) {
        failedFileDescriptor = open(failedFilePath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (failedFileDescriptor >= 0) {
            // Acquire exclusive lock
            fl.l_type = F_WRLCK;
            fl.l_whence = SEEK_SET;
            fl.l_start = 0;
            fl.l_len = 0;
            if (fcntl(failedFileDescriptor, F_SETLK, &fl) == -1) {
                fprintf(log, "Failed to acquire exclusive lock on file\n");
                close(failedFileDescriptor);
                return 0;
            }

            char numToSave[15];
            sprintf(numToSave, "%d", failed_count);
            dprintf(failedFileDescriptor, "%s\n", numToSave);
            for (int i = 0; i < failed_count; i++) {
                char portStr[6]; // assuming max 5 digits for the port
                sprintf(portStr, "%d", (*FailedData)[i].client_address.port);
                dprintf(failedFileDescriptor, "IP: %s | Port: %s | Contents: %s\n",
                        (*FailedData)[i].client_address.IP,
                        portStr,
                        (*FailedData)[i].contents);
            }
            // Release exclusive lock
            fl.l_type = F_UNLCK;
            if (fcntl(failedFileDescriptor, F_SETLK, &fl) == -1) {
                fprintf(log, "Failed to release exclusive lock on file\n");
                close(failedFileDescriptor);
                return 0;
            }
            // Closing the file
            close(failedFileDescriptor);
            return 1;
        }
        // Wait for file to be unlocked
        sleep(lockWaitTimeSeconds);
        lockAttempts++;
    }
    fprintf(log, "Timed out waiting for exclusive lock on %s\n",failedFilePath);
    return 0;
}
