/*////////////////////////////////////////////////////////////////////////////
// normproxy -- Proxy that listens for data on port A to send out over NORM 
// on port B, and forwards incoming NORM traffic onto another port.  
// Optionally, it can also proactively connect to an application listening for 
// incoming traffic (ie: a server).
//
// Copyright 2010, Rob Lass <urlass@cs.drexel.edu> 
//
//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program.  If not, see <http://www.gnu.org/licenses/>./
//
////////////////////////////////////////////////////////////////////////////*/

#define MAXHOSTNAME 1024
//TODO:  get rid of this limitation
#define MAXINPUTSIZE 1024*1024*5

#include<sys/select.h>
#include<sys/socket.h>
#include<sys/types.h>
#include<sys/un.h>

#include <fcntl.h>

#include<netinet/in.h>
#include<netdb.h>

#include<string.h>
#include<stdlib.h>
#include<unistd.h>
#include<limits.h>
#include<ctype.h>
#include<stdio.h>
#include<errno.h>
#include<getopt.h>
#include <time.h>

#include "normApi.h"

//GLOBALS
extern int errno;
NormInstanceHandle norm_instance;
bool debug;
int norm_node_id = -1;

//////////////////////////////////////////////////////////////////////////////
//converts the NORM enums to equivalent text
//////////////////////////////////////////////////////////////////////////////
void printEventTypeReceived(FILE * file , int type){
	switch(type){
		case NORM_TX_QUEUE_VACANCY:
			fprintf(file, "NORM_TX_QUEUE_VACANCY received!\n");
			break;
		case NORM_TX_QUEUE_EMPTY:
			fprintf(file, "NORM_TX_QUEUE_EMPTY received!\n");
			break;
		case NORM_TX_FLUSH_COMPLETED:
			fprintf(file, "NORM_TX_FLUSH_COMPLETED received!\n");
			break;
		case NORM_TX_WATERMARK_COMPLETED:
			fprintf(file, "NORM_TX_WATERMARK_COMPLETED received!\n");
			break;
		case NORM_TX_OBJECT_SENT:
			fprintf(file, "NORM_TX_OBJECT_SENT received!\n");
			break;
		case NORM_TX_OBJECT_PURGED:
			fprintf(file, "NORM_TX_OBJECT_PURGED received!\n");
			break;
		case NORM_LOCAL_SENDER_CLOSED:
			fprintf(file, "NORM_LOCAL_SENDER_CLOSED received!\n");
			break;
		case NORM_CC_ACTIVE:
			fprintf(file, "NORM_CC_ACTIVE received!\n");
			break;
		case NORM_CC_INACTIVE:
			fprintf(file, "NORM_CC_INACTIVE received!\n");
			break;
		case NORM_REMOTE_SENDER_NEW:
			fprintf(file, "NORM_REMOTE_SENDER_NEW received!\n");
			break;
		case NORM_REMOTE_SENDER_ACTIVE:
			fprintf(file, "NORM_REMOTE_SENDER_ACTIVE received!\n");
			break;
		case NORM_REMOTE_SENDER_INACTIVE:
			fprintf(file, "NORM_REMOTE_SENDER_INACTIVE received!\n");
			break;
		case NORM_REMOTE_SENDER_PURGED:
			fprintf(file, "NORM_REMOTE_SENDER_PURGED received!\n");
			break;
		case NORM_RX_OBJECT_NEW:
			fprintf(file, "NORM_RX_OBJECT_NEW received!\n");
			break;
		case NORM_RX_OBJECT_INFO:
			fprintf(file, "NORM_RX_OBJECT_INFO received!\n");
			break;
		case NORM_RX_OBJECT_UPDATED:
			fprintf(file, "NORM_RX_OBJECT_UPDATED received!\n");
			break;
		case NORM_RX_OBJECT_COMPLETED:
			fprintf(file, "NORM_RX_OBJECT_COMPLETED received!\n");
			break;
		case NORM_RX_OBJECT_ABORTED:
			fprintf(file, "NORM_RX_OBJECT_ABORTED received!\n");
			break;
		case NORM_GRTT_UPDATED:
			fprintf(file, "NORM_GRTT_UPDATED received!\n");
			break;
		case NORM_EVENT_INVALID:
			fprintf(file, "NORM_EVENT_INVALID received!\n");
			break;
		default:
			fprintf(file, "Unknown event type %d! (this shouldn't happen!)\n", type);
			break;
	}
}

//////////////////////////////////////////////////////////////////////////////
//This was based on the function of the same name in Richard Steven's book
//__Advanced Unix Programming__.
//////////////////////////////////////////////////////////////////////////////

int writeBlockToSocket(int the_socket, const char * data, int len){

	int left = len;
	int written = 0;

	if(debug)
		fprintf(stderr, "Writing to socket: %s\n", data);

	while(left > 0){
		written = send(the_socket, data, len, 0);
		const char * error_code;
		if(written < 0){
			if(errno == EINTR){
				error_code = "EINTR";
				written = 0;
			}else if(errno == EAGAIN){
				written = 0;
				error_code = "EAGAIN";
			}else if(errno == EBADF){
				written = 0;
				error_code = "EBADF";
			}else if(errno == EFAULT){
				written = 0;
				error_code = "EFAULT";
			}else if(errno == EFBIG){
				written = 0;
				error_code = "EFBIG\n";
			}else if(errno == EIO){
				written = 0;
				error_code = "EIO\n";
			}else if(errno == ENOSPC){
				written = 0;
				error_code = "ENOSPC";
			}else if(errno == EPIPE){
				written = 0;
				error_code = "EPIPE";
			}else if(errno == EWOULDBLOCK){
				written = 0;
				error_code = "EWOULDBLOCK";
			}else if(errno == ENOTCONN){
				written = 0;
				error_code = "ENOTCONN";
			}
			fprintf(stderr, "WARNING in writeToSocket(): Trying to write again with code %s.\n", error_code);
		}else if(written < left){
			if(debug)
				fprintf(stderr, "Only wrote %d of %d bytes of incoming NORM data to client socket!\n", written, len);
		}

		left -= written;
	}

	return len;
}

//////////////////////////////////////////////////////////////////////////////
//  We could probably just use send, but this
//does more error checking, and it chunks data into blocks that are big enough
//to not overflow.
//////////////////////////////////////////////////////////////////////////////
void writeToSocket(int the_socket, const char * data, long len){
	int written;
	long left = len;

	const char * block_data = data;

	//write one max_int size block at a time
	while(left > 0){
		if(left < INT_MAX){
			written = writeBlockToSocket(the_socket, block_data, left);
		}else{
			written = writeBlockToSocket(the_socket, block_data, left);
		}
		block_data += written;
		left -= written;
	}
	


}

//////////////////////////////////////////////////////////////////////////////
//Sets up the socket that waits for a client to connect.
//////////////////////////////////////////////////////////////////////////////
int setupSocket(int port){
	int client_socket;
	struct sockaddr_in saddr_in;
	struct hostent *h;
	char myhostname[MAXHOSTNAME+1];

	memset(&saddr_in, 0, sizeof(struct sockaddr_in));
	gethostname(myhostname, MAXHOSTNAME);
	h = gethostbyname(myhostname);

	if(h==NULL){
		fprintf(stderr, "Error getting local hostname!\n");
		abort();
	}

	saddr_in.sin_family = h->h_addrtype;
	saddr_in.sin_port = htons(port);

	if((client_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		fprintf(stderr, "Error creating socket on port %d!\n", port);
		abort();
	}

	if(bind(client_socket, (struct sockaddr*)&saddr_in, sizeof(struct sockaddr_in))==-1){
		fprintf(stderr, "Error binding socket on port %d!\n", port);
		abort();
	}

	if(listen(client_socket, 1) < 0){ 
		fprintf(stderr, "Error listening for connections.\n");
		abort();
	}
	if(debug)
		fprintf(stderr, "Successfully created listening socket.\n");
	return client_socket;
}


int connectToServer(char * server_addr, int port){
	int socketfd;
	struct addrinfo hints, *res;
	char sport[8];

	sprintf(sport, "%d", port);

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	getaddrinfo(server_addr, sport, &hints, &res);

	socketfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	connect(socketfd, res->ai_addr, res->ai_addrlen);

	return socketfd;
}

//////////////////////////////////////////////////////////////////////////////
//Sets up the NORM session that will listen for incoming NORM traffic and push
//data out over NORM.
//////////////////////////////////////////////////////////////////////////////
NormSessionHandle createNormSession(NormSessionId norm_session_id, char * norm_maddr, unsigned short norm_port, unsigned long buffer_space, unsigned short segment_size, unsigned char block_size, unsigned char num_parity, NormDescriptor *norm_fd){

	NormSessionHandle norm_session;

	unsigned int iseed = (unsigned int)time(NULL);
	srand (iseed);
	while(norm_node_id < 1)
		norm_node_id = rand();
	

	norm_instance = NormCreateInstance();
	norm_session = NormCreateSession(norm_instance, norm_maddr, norm_port, norm_node_id);
	if(debug)
		fprintf(stderr, "Using %d as my norm node id.\n", norm_node_id);
	NormStartSender(norm_session, norm_session_id, buffer_space, segment_size, block_size, num_parity);
	NormStartReceiver(norm_session, buffer_space);

	*norm_fd = NormGetDescriptor(norm_instance);

	return norm_session;
}


//////////////////////////////////////////////////////////////////////////////
// MAIN///////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
int main(int argc, char ** argv){
	int c;
	int proxy_socket, conn;
	NormDescriptor norm_fd;
	bool connect_proactively = false;
	bool session_finished = false;
	bool duplex_connection = false;

	struct sockaddr_un inpeer_addr;
	socklen_t peer_addr_size;

	///////////
	// Default values 
	///////////
	NormSessionHandle norm_session;

	NormSessionId norm_session_id = 1027;
	unsigned long buffer_space = 1024*1024;
	unsigned short segment_size = 1400;
	unsigned char block_size = 64;
	unsigned char num_parity = 16;
	const char * norm_maddr = "224.1.2.3";
	char * server_addr;
	unsigned short norm_port = 6003;
	int port = 1027;
	int remote_port = 1027;


	//By default, no debugging information.
	debug = false;

	///////////
	// Process command-line arguments
	///////////
	static struct option long_options[] =
	{
		{"block-size", required_argument, 0, 'B'},
		{"debug", no_argument, 0, 'D'},
		{"num-parity", required_argument, 0, 'P'},
		{"mcast-address", required_argument, 0, 'a'},
		{"buffer-space", required_argument, 0, 'b'},
		{"client-device", required_argument, 0, 'c'},
		{"norm-device", required_argument, 0, 'd'},
		{"session-id", required_argument, 0, 'n'},
		{"proxy-port", required_argument, 0, 'p'},
		{"segment-size", required_argument, 0, 's'},
		{"server-address", required_argument, 0, 'y'},
		{"norm-port", required_argument, 0, 'z'},
		{"duplex", required_argument, 0, 'e'},
		{"remote-port", required_argument, 0, 'q'},
		{"norm-node-id", required_argument, 0, 'i'},
		{0,0,0,0}
	};

	int options_index = 0;

	while ((c = getopt_long(argc, argv, "b:s:B:P:a:c:z:p:n:d:y:i:De", 
					long_options, &options_index)) != -1){
		switch(c){
			//unimplemented
			case 'd'://handle the device
				fprintf(stderr, "-d is not yet implemented\n");
				break;
			case 'e'://handle the device
				duplex_connection = true;
				break;
			case 'c'://handle the device
				fprintf(stderr, "-c is not yet implemented\n");
				break;
				//implemented
			case 'b'://buffer space for NORM
				buffer_space = atoi(optarg);
				//printf("buffer_space = %li\n", buffer_space);
				break;
			case 's'://segment size for NORM
				segment_size  = atoi(optarg);
				//printf("segment_size = %hi\n", segment_size);
				break;
			case 'B'://block size for NORM
				block_size = atoi(optarg);
				//printf("block_size  = %d\n", block_size);
				break;
			case 'P'://number of parity bits for NORM
				num_parity = atoi(optarg);
				//printf("num_parity = %d\n", num_parity);
				break;
			case 'a'://NORM multicast address to use
				norm_maddr = optarg;
				//printf("norm_maddr = %s\n", norm_maddr);
				break;
			case 'z'://NORM port to use
				norm_port = atoi(optarg);
				//printf("norm_port  = %d\n", norm_port);
				break;
			case 'p'://handle the port for client connections
				port = atoi(optarg);
				//printf("client_port = %d\n", port);
				break;
			case 'q'://handle the port for client connections
				remote_port = atoi(optarg);
				break;
			case 'n': // handle the norm session ID
				norm_session_id = atoi(optarg);
				//printf("norm session id = %d\n", norm_session_id);
				break;
			case 'D': // turn on debug mode
				debug = true;
				printf("Debug mode!\n");
				break;
			case 'y':
				server_addr = optarg;
				connect_proactively = true;
				break;
			case 'i':
				norm_node_id = atoi(optarg);
				break;
		}
	}


	//if we are duplexing, don't do any of the proactive connection stuff
	//(because we only want to connect once we have some data to hand off)
	if(duplex_connection)
		connect_proactively = false;

	///////////
	// Set up NORM
	///////////
	norm_session = createNormSession(norm_session_id, (char *)norm_maddr, norm_port, buffer_space, segment_size, block_size, num_parity, &norm_fd);

	while(true){
		///////////
		// Set up socket for the application being proxied
		///////////
		bool connected = false; // have we connected to the proxied client?
		if(duplex_connection)
			fprintf(stderr, "Proxy is in duplex mode. \n");
		if(connect_proactively){
			if(debug)
				fprintf(stderr, "Proxy is proactively connecting to %s:%d.\n",
						server_addr, port);
			conn = connectToServer(server_addr, port);
			connected=true;
			/*
			//test for any web server (to make sure the server socket code works)
			const char * foo = "get /\n";
			int bar = sizeof(foo);
			writeToSocket(conn, foo, bar);
			*/
		}else{ //default:  listening mode
			proxy_socket = setupSocket(port);
		}



		///////////
		// Main loop: Rebroadcast unicast data AND rebroadcast NORM data.
		///////////
		while(!session_finished){

			//set up select to block until some data comes in on any port
			fd_set rfds;
			int retval, highest, norm_fd_int_format;

			norm_fd_int_format = (int)norm_fd;

			FD_ZERO(&rfds);
			//select over the NORM socket, the listening socket and the proxy
			//socket, the latter only if we've connected.
			FD_SET(norm_fd_int_format, &rfds);
			if(connected){
				FD_SET(conn, &rfds);
			}else{
				FD_SET(proxy_socket, &rfds);
			}

			//figure out which socket has the highest FD
			if(norm_fd_int_format > proxy_socket)
				highest = norm_fd_int_format;
			else
				highest = proxy_socket;

			if(connected){
				if(conn > highest)
					highest = conn;
			}

			// block indefinitely, until a socket is ready to read
			retval = select(highest+1, &rfds, NULL, NULL, NULL);
			if(debug)
				fprintf(stderr, "%d sockets are ready to read!\n", retval);

			if(retval < 0){
				printf("Error selecting socket to read from!\n");
				perror("select()");
				abort();
			}

			///////////LISTENING SOCKET IS READY//////////////
			if(FD_ISSET(proxy_socket, &rfds)){
				peer_addr_size = sizeof(struct sockaddr_un);
				//create the listening socket
				if((conn= accept(proxy_socket, (struct sockaddr *) &inpeer_addr, &peer_addr_size)) < 0){
					fprintf(stderr, "ERROR:  incoming connection has problems.\n");
					abort();
				}
				if(debug)
					printf("Created a new client socket.\n");
				connected = true;
			}

			///////////CLIENT / SERVER PROXY SOCKET IS READY///////////////
			if(connected && FD_ISSET(conn, &rfds)){

				//read incoming client data
				char * in_buffer = (char *)malloc(MAXINPUTSIZE);
				int n = read(conn, in_buffer, MAXINPUTSIZE);
				if(n<0){
					printf("Error reading data from incoming socket: %d!\n", n);
					abort();
				}else if(n==0){
					printf("WARNING: zero byte data stream on incoming socket.\n");
					printf("WARNING: treating this as though socket is closed.\n");

					//close the client connection,  since we got a 0 byte
					//stream over it
					close(conn);
					connected = false;
				}else{
					//broadcast client data over NORM
					NormDataEnqueue(norm_session, in_buffer, n);
					if(debug)
						printf("Sent data: %s\n", in_buffer);
				}
			}

			///////////NORM SOCKET IS READY//////////////
			if(FD_ISSET(norm_fd, &rfds)){
				NormEvent event;

				//if we don't successfully get a NORM event, ignore and keep going
				if (!NormGetNextEvent(norm_instance, &event)) continue;

				//if it's an object completed event (ie: we received data), write
				//it to the client socket, otherwise just record the type of event
				//and continue
				switch (event.type){
					case NORM_RX_OBJECT_COMPLETED:
						{
							unsigned int obj_size = NormObjectGetSize(event.object);
							//use normdatadetachdata instead, and then free
							//the pointer when done
							char* data_ptr = NormDataDetachData(event.object);

							if(debug)
								fprintf(stderr, "NORM_RX_OBJECT_COMPLETED: object>%p size>%u ...\n", event.object, obj_size);

							if(duplex_connection){
								//connect to the socket listening for the
								//response and send data
								int duplex_conn = 
										connectToServer(server_addr, port);
								writeToSocket(duplex_conn, data_ptr, obj_size);
								close(duplex_conn);
							}else{
								writeToSocket(conn, data_ptr, obj_size);
							}

							free(data_ptr);
						}

						break;
					default:
						if(debug)
							printEventTypeReceived(stderr, event.type);
						break;
				}
			}
		}
		close(conn);
	}

	///////////
	// Shut everything down.
	///////////
	NormStopSender(norm_session);
	NormStopReceiver(norm_session);
	NormDestroySession(norm_session);
	NormDestroyInstance(norm_instance);
	if(debug)
		printf("NORM shut down successfully.\n");

	return 0;
}
