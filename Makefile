# Set the name of the client and server executables
CLIENT = communicate_client
SERVER = communicate_server

# Set the source files for the client and server
SOURCES_CLNT.c =  
SOURCES_CLNT.h =
SOURCES_SVC.c = PubSubTools.c
SOURCES_SVC.h = PubSubTools.h
SOURCES.x = communicate.x

# Set the target files for the client and server
TARGETS_SVC.c = communicate_svc.c communicate_server.c communicate_xdr.c
TARGETS_CLNT.c = communicate_clnt.c communicate_client.c communicate_xdr.c
TARGETS = communicate.h communicate_xdr.c communicate_clnt.c communicate_svc.c communicate_client.c communicate_server.c
CLEAN__TARGETS = communicate.h communicate_clnt.c communicate_svc.c communicate_xdr.c

# Set the object files for the client and server
OBJECTS_CLNT = $(SOURCES_CLNT.c:%.c=%.o) $(TARGETS_CLNT.c:%.c=%.o)
OBJECTS_SVC = $(SOURCES_SVC.c:%.c=%.o) $(TARGETS_SVC.c:%.c=%.o)

# Set the compiler flags
CFLAGS += -g
LDLIBS += -lnsl
RPCGENFLAGS += -N

#new default that stops and reloads server process as well as doing a clean make
rebuild:
	make clean
	make offServerClientBuild
	echo "starting communicate_server."
	 ./communicate_server
	
offServerClientBuild:
	make clean
	if pgrep communicate_server > /dev/null; then \
	    echo "communicate_server was running. Ending it.";\
	    pkill "communicate_server";\
	else \
	    echo "communicate_server was not running." ;\
	fi;	
	make all

clean:
	$(RM) core $(CLEAN__TARGETS) $(OBJECTS_CLNT) $(OBJECTS_SVC) $(CLIENT) $(SERVER)

# Define the build targets
all : $(CLIENT) $(SERVER)

# Generate the necessary source files from the .x file
$(TARGETS) : $(SOURCES.x) 
	rpcgen $(RPCGENFLAGS) $(SOURCES.x)

$(OBJECTS_CLNT) : $(SOURCES_CLNT.c) $(SOURCES_CLNT.h) $(TARGETS_CLNT.c) 

$(OBJECTS_SVC) : $(SOURCES_SVC.c) $(SOURCES_SVC.h) $(TARGETS_SVC.c) 

# Build the client and server executables
$(CLIENT) : $(OBJECTS_CLNT) 
	$(LINK.c) -o $(CLIENT) $(OBJECTS_CLNT) $(LDLIBS) 
$(SERVER) : $(OBJECTS_SVC) 
	$(LINK.c) -o $(SERVER) $(OBJECTS_SVC) $(LDLIBS)


