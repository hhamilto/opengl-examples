/* Copyright (c) 2014 Scott Kuhl. All rights reserved.
 * License: This code is licensed under a 3-clause BSD license. See
 * the file named "LICENSE" for a full copy of the license.
 */

/**
   @file

    DGR provides a framework for a master process to share data with slave processes via UDP packets on a network.

    @author Scott Kuhl
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <poll.h>
#include <time.h>




/** The dgr_record struct is used internally by DGR to hold a single
 * variable that DGR is keeping track of. */
typedef struct {
	char name[1024]; /**< The name of the variable */
	int size;        /**< Number of bytes of data in this variable */
	void *buffer;    /**< The bytes of data in this variable */
} dgr_record;




/** Maximum number of records DGR can handle. */
#define DGR_MAX_LIST_SIZE 1024
/** A list of records DGR is tracking */
static dgr_record dgr_list[DGR_MAX_LIST_SIZE]; 
/** Size of the DGR record list */
static int dgr_list_size = 0;

/* The socket that we are sending/receiving from */
static int dgr_socket;
static struct addrinfo *dgr_addrinfo;
static time_t dgr_time_lastreceive; /**< time we received last packet, 0 if haven't received anything yet. */

/* Other DGR variables. */
static int dgr_mode = 0;  /**< Set to 1 if we are master, 0 otherwise */
static int dgr_disabled = 0; /**< Set to 1 if we are running in a DGR environment, 0 otherwise */


/** Frees resources that DGR has used. */
static void dgr_free()
{
	for(int i=0; i<dgr_list_size; i++)
		free(dgr_list[i].buffer);
	dgr_list_size = 0;
}

/** Initializes a master DGR process that will send packets out on the network. */
static void dgr_init_master()
{
	const char *ipAddr = getenv("DGR_MASTER_DEST_IP");
	const char *port = getenv("DGR_MASTER_DEST_PORT");
	if(ipAddr == NULL || strcmp(ipAddr, "0.0.0.0") == 0)
	{
		dgr_disabled = 1;
		printf("DGR Master: Won't transmit since IP address was not provided or was 0.0.0.0.\n");
	}
	else
		dgr_disabled = 0;

	if(port == NULL)
	{
		printf("DGR Master: No port was specified in the DGR_MASTER_DEST_PORT environment variable.\n");
		exit(1);
	}

	printf("DGR Master: Preparing to send packets to %s port %s.\n", ipAddr, port);
	
	struct addrinfo hints, *servinfo;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;

	int rv;
	if ((rv = getaddrinfo(ipAddr, port, &hints, &servinfo)) != 0) {
		fprintf(stderr, "DGR Master: getaddrinfo: %s\n", gai_strerror(rv));
		exit(1);
	}
	
	// loop through all the results and make a socket
	struct addrinfo *p;
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((dgr_socket = socket(p->ai_family, p->ai_socktype,
		                         p->ai_protocol)) == -1) {
			perror("DGR Master: socket");
			continue;
		}
		break;
	}

	if (p == NULL) {
		fprintf(stderr, "DGR Master: failed to bind socket\n");
		exit(1);
	}

	dgr_addrinfo = p;
}

/** Initializes a DGR slave process which will receive packets from a master process. */
static void dgr_init_slave()
{
	const char* port = getenv("DGR_SLAVE_LISTEN_PORT");
	if(port == NULL)
	{
		fprintf(stderr, "DGR Slave: DGR_SLAVE_LISTEN_PORT was not set.\n");
		exit(1);
	}
	printf("DGR Slave: Preparing to receive packets on port %s.\n", port);
	
	dgr_time_lastreceive = 0;
	struct addrinfo hints, *servinfo, *p;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC; // set to AF_INET forces IPv4; AF_INET6 forces IPv6; AF_UNSPEC allows any
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	int rv;
	if ((rv = getaddrinfo(NULL, port, &hints, &servinfo)) != 0) {
		fprintf(stderr, "DGR Slave: getaddrinfo: %s\n", gai_strerror(rv));
		exit(1);
	}

	// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((dgr_socket = socket(p->ai_family, p->ai_socktype,
		                     p->ai_protocol)) == -1) {
			perror("DGR Slave: socket");
			continue;
		}
		if (bind(dgr_socket, p->ai_addr, p->ai_addrlen) == -1) {
			close(dgr_socket);
			perror("DGR Slave: bind");
			continue;
		}

		break;
	}

	if (p == NULL) {
		fprintf(stderr, "DGR Slave: Failed to bind socket\n");
		exit(1);
	}

	freeaddrinfo(servinfo);
}


/** Indicates if this process is either a master process or a slave
    process as specified by the DGR environment variables.

    @return 1 if we are a master process, 0 if we are a slave process.
*/
int dgr_is_master()
{ return dgr_mode; }

/** Indicates if DGR is properly enabled or if it is disabled.

    @return 1 if DGR is working, 0 if it is not.
*/
int dgr_is_enabled()
{
	if(dgr_disabled)
		return 0;
	return 1;
}


/** Initialize DGR. DGR options are specified via environment
 * variables. This function should typically be called once near the
 * beginning of a DGR program. */
void dgr_init()
{
	const char* master = getenv("DGR_MODE");

	dgr_disabled = 1;
	if(master != NULL)
	{
		if(strcmp(master, "master") == 0)
		{
			dgr_mode = 1;
			dgr_disabled = 0;
			dgr_init_master();
		}
		else if(strcmp(master, "slave") == 0)
		{
			dgr_mode = 0;
			dgr_disabled = 0;
			dgr_init_slave();
		}
	}
	
	if(dgr_disabled)
		fprintf(stderr, "DGR: DGR is disabled; not a valid DGR environment.\n");

	// if there already is a list, free it.
	if(dgr_list_size > 0)
		dgr_free();
}


/** Given a name, find the index of the name in our list. Returns -1 if
 * name is not found. */
static int dgr_findIndex(const char *name)
{
	for(int i=0; i<dgr_list_size; i++)
		if(strcmp(name, dgr_list[i].name) == 0)
			return i;
	return -1;
}


/** Given a label, a buffer to store data, and the size of that buffer,
 * get data from DGR, store it in buffer and return the actual size of
 * the data we copied into the buffer.
 *
 * @return Returns -1 if DGR didn't know about the name. Returns -2 the buffer you
 * provided was too small. Returns -3 if DGR is not enabled.
 */
static int dgr_get(const char *name, void* buffer, int bufferSize)
{
	if(dgr_disabled)
		return -3;
	
	int index = dgr_findIndex(name);
	if(index == -1)
		return -1;

	/* If we found the record... */
	dgr_record *rec = &(dgr_list[index]);
	/* Copy the data if there is enough room */
	if(bufferSize >= rec->size)
	{
		memcpy(buffer, rec->buffer, rec->size);
		return rec->size;
	}
	else /* 'buffer' wasn't large enough to store data. */
		return -2;
}

/** Adds a variable to DGRs list of variables. These variables will be sent to slaves when dgr_update() is called.
 * @param name The name of the variable.
 * @param buffer A pointer to the variable.
 * @param size The number of bytes used by the variable.
 */
static void dgr_set(const char *name, void *buffer, int size)
{
	if(dgr_disabled)
		return;
	
	// printf("dgr_set(%s, %p, %d)\n", name, buffer, size);
	int index = dgr_findIndex(name);
	if(index == -1)
	{
		// printf("DGR Master: The name '%s' is new to dgr, storing it at location %d\n", name, dgr_list_size);

		if(dgr_list_size > DGR_MAX_LIST_SIZE)
		{
			printf("DGR Master: You have exceeded the maximum list size for DGR.");
			exit(1);
		}

		dgr_record *record = &(dgr_list[dgr_list_size]);
		sprintf(record->name, "%s",  name);
		record->size = size;
		record->buffer = malloc(size);
		memcpy(record->buffer, buffer, size);

		dgr_list_size++;
	}
	else
	{
		dgr_record *record = &(dgr_list[index]);
		// printf("DGR Master: The name '%s' is already known to dgr (at index %d)\n", name, index);

		if(record->size != size)
		{
//			printf("DGR Master: The name %s used to have size %d but now has size %d.", name, record->size, size);
			free(record->buffer);
			record->buffer = malloc(size);
			record->size = size;
		}
		memcpy(record->buffer, buffer, size);
	}
}

/** Set a variable if we are a DGR master (so that we can send it to
 * slaves) and get a variable if we are a DGR slave. The variable is
 * stored in 'buffer' and is 'bufferSize' bytes long.
 *
 * If we are a slave, get the variable from the master and store it in
 * 'buffer' which is 'bufferSize' bytes large. If anything goes wrong
 * (your buffer is the wrong size, the record name that you requested
 * wasn't sent by the server, etc) an error message will be printed
 * and your buffer variable will be unchanged.
 *
 * Once you add a variable with a name, you cannot (currently) remove
 * it from DGR. However, you can overwrite an existing variable with a
 * new value. If you set a variable once and never set it again, DGR
 * will keep sending that variable.
 *
 * @param name A string representing the name of the variable. Both the DGR master and DGR slaves must use the same string for the same variable.
 * @param buffer A pointer to the data (an int, float, array, struct, etc.)
 * @param bufferSize The size of the data in the buffer in bytes.
 */
void dgr_setget(const char *name, void* buffer, int bufferSize)
{
	if(dgr_disabled)
		return;
	
	if(dgr_mode)
		dgr_set(name, buffer, bufferSize);
	else
	{
		int ret = dgr_get(name, buffer, bufferSize);
		if(ret == -1)
			printf("DGR Slave: Tried to get '%s' from DGR, but DGR didn't have it!\n", name);
		else if(ret == -2)
			printf("DGR Slave: Tried to get '%s' from DGR, but you didn't provide a large enough buffer.\n", name);

		else if(ret != bufferSize)
			printf("DGR Slave: Successfully retrieved '%s' from DGR but you provided a buffer that didn't match the size of the data you are retrieving. Your buffer is %d bytes but the '%s' record is %d bytes.\n", name, bufferSize, name, ret);
	}
}


/** Takes the list of DGR records and puts them into a compact byte
 * stream. The format is:
 *   
 * label character string<br>
 * Null terminator at end of string<br>
 * An integer indicating the size of the data that follows.<br>
 * A buffer of the data.<br>
 *
 * @param size The size of the data being serialized.
 * @return A serialized array of bytes (to be free()'d by the caller)
*/
char* dgr_serialize(int *size)
{
	int spaceNeeded = 0;
	for(int i=0; i<dgr_list_size; i++)
		spaceNeeded += strlen(dgr_list[i].name)+1+sizeof(int)+dgr_list[i].size;
	*size = spaceNeeded;

	char *serialized = malloc(spaceNeeded);
	char *ptr = serialized;
	for(int i=0; i<dgr_list_size; i++)
	{
		int bytesPrinted = sprintf(ptr, "%s", dgr_list[i].name);
		ptr += bytesPrinted+1; // extra byte for null terminated string.
		memcpy(ptr, &(dgr_list[i].size), sizeof(int));
		ptr += sizeof(int);
		memcpy(ptr, dgr_list[i].buffer, dgr_list[i].size);
		ptr += dgr_list[i].size;
	}

	return serialized;
}


/** Unserializes serialized data and stores it in our global dgr_list
 * variable. We do not blow away the list, instead we just update the
 * data that is already in the list.
 *
 * @param size Length of the serialized data.
 * @param serialized The serialized data as an array of bytes.
 **/
static void dgr_unserialize(int size, char *serialized)
{
	char *ptr = serialized;

	while(ptr < serialized + size)
	{
		int c = 0;
		char name[1024];
		while(*ptr != 0)
		{
			name[c++] = *ptr;
			ptr++;
		}
		name[c++] = '\0';
		ptr++;
		// printf("unserialized: %s\n", name);

		int size = 0;
		memcpy(&size, ptr, sizeof(int));
		ptr += sizeof(int);

		char data[size];
		memcpy(data, ptr, size);
		ptr += size;

		dgr_set(name, data, size);
	}

}


/** Prints a list of variables that DGR is aware of. */
void dgr_print_list()
{
	if(dgr_disabled)
	{
		printf("DGR is disabled or not initialized correctly.\n");
		return;
	}
	printf("Current DGR list (index, size, buffer, name):\n");
	for(int i=0; i<dgr_list_size; i++)
	{
		dgr_record *r = &(dgr_list[i]);
		printf("%3d %5d %p %s\n", i, r->size, r->buffer, r->name);
	}
	if(dgr_list_size == 0)
		printf("[ the list is empty ]\n");
}

/** Serializes and sends DGR data out across a network. */
static void dgr_send()
{
	if(dgr_disabled)
		return;
	int  bufSize = 0;
	char *buf = dgr_serialize(&bufSize);

	// no need to send an empty packet.
	if(bufSize == 0 || dgr_list_size == 0)
		return;
	
	/* If the message is too large to send, sendto() will not send the
	 * message, and will set errno to EMSGSIZE. The MTU may limit the
	 * amount of data that we can send. With an MTU of 1500, we can
	 * only expect to send 1472 bytes. Even with the small MTU, the
	 * system may still allow us to send larger UDP packets due to
	 * IPv4 fragmentation. */
	int numbytes;
	if((numbytes = sendto(dgr_socket, buf, bufSize, 0,
	                      dgr_addrinfo->ai_addr, dgr_addrinfo->ai_addrlen)) == -1) {
		perror("DGR Master: sendto");
		exit(1);
	}
	if(numbytes != bufSize) // double check that everything got sent
	{
		fprintf(stderr, "DGR Master: Error sending all of the bytes in the message.");
		exit(1);
	}
}

/** Receives DGR data from the network.
 *
 * @param timeout If timeout > 0, dgr_receive() will block for at most
 * 'timeout' milliseconds. If a timeout occurs, DGR will exit. If
 * timeout==0, dgr_receive() not block and will read whatever
 * information is available to read. If timeout==0, we still might
 * exit if we haven't received information for a while (and we have
 * received information successfully in the past). */
static void dgr_receive(int timeout)
{
	if(dgr_disabled)
		return;
	
	if(dgr_time_lastreceive != 0) // if we have received a packet previously
	{
		int seconds = 15;
		if(time(NULL) - dgr_time_lastreceive >= seconds)
		{
			printf("DGR Slave: dgr_receive() hasn't received packets within %d seconds. We did receive some one or more packets earlier. Did the master or relay die? Exiting...\n", seconds);
			exit(1);
		}
	}
	
	struct pollfd fds;
	fds.fd = dgr_socket;
	fds.events = POLLIN;
	int retval = poll(&fds, 1, timeout);
	if(retval == -1)
	{
		perror("poll()");
		exit(1);
	}
	else if(retval == 0) // nothing to read within timeout value
	{
		/* If a non-zero timeout value was specified and we timed out, exit() */
		if(timeout > 0)
		{
			printf("DGR Slave: dgr_receive() never received anything and timed out (%f second timeout). Exiting...\n", timeout/1000.0);
			exit(1);
		}
		return;
	}


	
	struct sockaddr_storage their_addr;
	socklen_t addr_len = sizeof their_addr;

	char serialized[1024*1024];
	int numbytes;
	/* Read packets until there are no more to read. This ensures that
	 * we are always using the newest packet. For example, 5 packets
	 * might arrive while the slave is rendering a scene. We want to
	 * make sure that we use the newest packet. */
	while(1)
	{
		if ((numbytes = recvfrom(dgr_socket, serialized, 1024*1024, 0,
		                         (struct sockaddr *)&their_addr, &addr_len)) == -1) {
			perror("recvfrom");
			exit(1);
		}

		// if there is nothing to read anymore from the socket, break out of loop.
		struct pollfd fds;
		fds.fd = dgr_socket;
		fds.events = POLLIN;
		int retval = poll(&fds, 1, 0);
		if(retval == 0)
			break;
	}
	dgr_time_lastreceive = time(NULL);
	
	dgr_unserialize(numbytes, serialized);
}

/** Send or receive data depending on DGR configuration. If we are a
 * DGR master, dgr_update() will send data to the network. if we are
 * DGR slave, dgr_update() will receive data from the network. In an
 * OpenGL DGR program, you would typically call this method every time
 * you render a frame. */
void dgr_update()
{
	if(dgr_mode)
		dgr_send();
	else
		// if it is our first time receiving, allow for a delay.
		if(dgr_time_lastreceive == 0)
			dgr_receive(10000);
		else
			dgr_receive(0);
}


