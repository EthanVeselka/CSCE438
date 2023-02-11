#include <glog/logging.h>
// TODO: Implement Chat Server.
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "interface.h"

#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <thread>
#include <mutex>
#include <algorithm>

using namespace std;

void error(string msg)
{
    // perror(msg);
	LOG(ERROR) << msg;
    exit(1);
}

//Helper struct to keep track of room variables for the threads
struct Room {
    string name;
    int port;
    int fd;
    vector<int> clientfds;
};

//Helper struct to keep track of socket variables after room creation
struct Socket{
    int sockfd, newsockfd, portno;
    socklen_t clilen;
    struct sockaddr_in server_address, cli_addr;
};

void procc_cmnd(int sock, vector<Room>* chatrooms, vector<Socket>* sockets); //process command thread function
void sendtoclient(int sock, Reply reply); //helper send function
void createRoom(string name, vector<Room>* rooms, vector<Socket>* sockets); // create room function and set socket struct

mutex m; //mutex for thread locking
vector<Room> chatrooms; //main ds, keeps track of all chatrooms and associated clients
vector<Socket> sockets; //socket vector

//broadcasts message to the i'th chatroom from clientfd
void broadcast(const string message, int clientfd, int i) {
    // unique_lock<mutex> lock(m);
    char buf[MAX_DATA];
    strcpy(buf, message.c_str());
    for (auto client : chatrooms.at(i).clientfds) {
        if (client == clientfd) {continue;}
        send(client, buf, sizeof(buf), 0);
        if (clientfd == -1) {
            close(client);
            close(chatrooms.at(i).fd);
        }
    }
}

//handle function called by threads each time a client joins a room
void handle(int client_socket, int i) {
    char buffer[MAX_DATA];
    while (true) {
        memset(buffer, 0, MAX_DATA);
        int bytes_received = recv(client_socket, buffer, MAX_DATA, 0);
        if (bytes_received <= 0) {
            close(client_socket);
            unique_lock<mutex> lock(m);
            auto iter = find(chatrooms.at(i).clientfds.begin(), chatrooms.at(i).clientfds.end(), client_socket);
            if (iter != chatrooms.at(i).clientfds.end()) {
                chatrooms.at(i).clientfds.erase(iter);
            }
            break;
        }
        string message(buffer, bytes_received);
        broadcast(message, client_socket, i);
    }
}

//main server function, loops and continually calls threads fro command/chatroom processing
int main(int argc, char *argv[]) {
    //google::InitGoogleLogging(argv[0]);

    //LOG(INFO) << "Starting Server";
    vector<thread> threads;

    int sockfd, newsockfd, portno, pid;
    socklen_t clilen;
    struct sockaddr_in server_address, cli_addr;

    if (argc < 2)
    {
        error("ERROR, no port provided");
        exit(1);
    }

    //create new socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");

    //read portno
    portno = atoi(argv[1]);

    //set server address struct values
    bzero((char *)&server_address, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(portno);


    //bind socket to address
    if (bind(sockfd, (struct sockaddr *)&server_address, sizeof(server_address)) < 0)
        error("ERROR on binding");

    //listen at socket for connection
    listen(sockfd, 5);
    clilen = sizeof(cli_addr);
    
    //enter process_command/thread loop
    while (1)
    {
        //accept call blocks proccess until connection is formed
        newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
        if (newsockfd < 0)
            error("ERROR on accept");
        
        threads.push_back(thread(procc_cmnd, newsockfd, &chatrooms, &sockets));
    }

    return 0; /* we never get here */
}

/*
 There is a separate instance of this function for each connection.
 It handles all communication once a connnection has been established.
 */
void procc_cmnd(int sock, vector<Room>* chatrooms, vector<Socket>* sockets)
{
    int n;
    char buffer[MAX_DATA];
    bzero(buffer, MAX_DATA);

    /*
    Read incoming message from socket, selects one for reading based on info avaialability
    */
    fd_set readfds;
    timeval t;
    t.tv_sec = 0;
    t.tv_usec = 0;
    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);
    select(sock + 1, &readfds, NULL, NULL, NULL);
    if (FD_ISSET(sock, &readfds)) {
        n = read(sock, buffer, 255);
        if (n < 0)
            error("ERROR reading from socket");
    }

    //tokenizing command
    string str = buffer;
    stringstream ss(str);
    string word;
    string cmnd;
    string name;
    ss >> word;
    cmnd = word;
    touppercase((char *)cmnd.c_str(), cmnd.size());
    if (cmnd != "LIST") {
        ss >> word;
        name = word;
    }

    //creates a room
    Reply reply;
    if (cmnd == "CREATE") {
        if (name == "create") {
            reply.status = FAILURE_INVALID;
            sendtoclient(sock, reply);
        }
        else if (chatrooms->size() == 0) {
            reply.status = SUCCESS;
            createRoom(name, chatrooms, sockets);
            sendtoclient(sock, reply);
        }
        else {
            for (int i = 0; i < chatrooms->size(); ++i) {
                if (chatrooms->at(i).name == name) {
                    reply.status = FAILURE_ALREADY_EXISTS;
                    sendtoclient(sock, reply);
                    break;
                }
                else if ((i == chatrooms->size() - 1) && (chatrooms->at(i).name != name)) {
                    reply.status = SUCCESS;
                    createRoom(name, chatrooms, sockets);
                    sendtoclient(sock, reply);
                }
            }
        }   
    }
    else if (cmnd == "DELETE") { // deletes a room
        string warning = "Warning:the chatting room is going to be closed...";
        
        if (chatrooms->size() == 0) {
            reply.status = FAILURE_NOT_EXISTS;
            sendtoclient(sock, reply);
        }
        else {
            for (int i = 0; i < chatrooms->size(); ++i) {
                if (chatrooms->at(i).name == name) {
                    broadcast(warning, -1, i);
                    close(chatrooms->at(i).fd);
                    chatrooms->at(i).name = "";
                    chatrooms->at(i).clientfds.clear();
                    reply.status = SUCCESS;
                    sendtoclient(sock, reply);
                    break;
                }
                else if ((i == chatrooms->size() - 1) && (chatrooms->at(i).name != name)) {
                    reply.status = FAILURE_NOT_EXISTS;
                    sendtoclient(sock, reply);
                }
            }
        }
    }
    else if (cmnd == "JOIN"){ //joins the sock (client) to the room, launches handler thread
        if (chatrooms->size() == 0) {
            reply.status = FAILURE_NOT_EXISTS;
            sendtoclient(sock, reply);
        }
        else {
            for (int i = 0; i < chatrooms->size(); ++i) {
                if (chatrooms->at(i).name == name) {
                    reply.status = SUCCESS;
                    reply.port = chatrooms->at(i).port;
                    reply.num_member = chatrooms->at(i).clientfds.size();
                    sendtoclient(sock, reply);
                    //connection here;
                    int newsockfd = accept(chatrooms->at(i).fd, (struct sockaddr *)&sockets->at(i).cli_addr, &sockets->at(i).clilen);
                    if (newsockfd < 0)
                        error("ERROR on accept");
                    chatrooms->at(i).clientfds.push_back(newsockfd);
                    thread(handle, chatrooms->at(i).clientfds.at(chatrooms->at(i).clientfds.size() - 1), i).detach();
                    break;
                }
                else if ((i == chatrooms->size() - 1) && (chatrooms->at(i).name != name)) {
                    reply.status = FAILURE_NOT_EXISTS;
                    sendtoclient(sock, reply);
                }
            }
        }
    }
    else if (cmnd == "LIST"){ //replies with a list of all available rooms
        string list = "";
        if (chatrooms->size() == 0) {
            reply.status = SUCCESS;
            strcpy(reply.list_room, list.c_str());
            sendtoclient(sock, reply);
        }
        else { 
            for (int i = 0; i < chatrooms->size(); ++i) {
                if (chatrooms->at(i).name == "") {continue;}
                list += chatrooms->at(i).name + ",";
            }
            reply.status = SUCCESS;
            strcpy(reply.list_room, list.c_str());
            sendtoclient(sock, reply);
        }
    }
    else {
        reply.status = FAILURE_INVALID;
        sendtoclient(sock, reply);
    }
}

void sendtoclient(int sockfd, Reply reply) 
{
    char buf[MAX_DATA];
    int size = sizeof(reply);
    memcpy(&buf, &reply, size);
    if (send(sockfd, buf, size, 0) < 0)
	{
		// LOG(ERROR) << "ERROR: send failed";
		error("ERROR: send failed");
		exit(EXIT_FAILURE);		
	}
}

void createRoom(string name, vector<Room>* chatrooms, vector<Socket>* sockets) { //std socket, struct, bind, listen calls for socket creation;
    Room room;
    room.name = name;

    Socket socket_;
    int sockfd, newsockfd, portno;
    socklen_t clilen;
    struct sockaddr_in server_address, cli_addr;


    //create new master socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");

    //use port 8081 by default and use the ones after that relative to size
    portno = chatrooms->size() + 49152;

    bzero((char *)&server_address, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(portno);
    
    if (bind(sockfd, (struct sockaddr *)&server_address, sizeof(server_address)) < 0)
        error("ERROR on binding");
    
    listen(sockfd, 5);
    clilen = sizeof(cli_addr);
    room.fd = sockfd;
    room.port = portno;

    socket_.sockfd = sockfd;
    socket_.newsockfd = newsockfd;
    socket_.portno = portno;
    socket_.clilen = clilen;
    socket_.server_address = server_address;
    socket_.cli_addr;
    sockets->push_back(socket_);
    chatrooms->push_back(room);
}
