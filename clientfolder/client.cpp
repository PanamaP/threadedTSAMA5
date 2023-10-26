//
// Simple chat client for TSAM-409
//
// Command line: ./chat_client 4000 
//
// Author: Jacky Mallett (jacky@ru.is)
// g++ -Wall -std=c++11 client.cpp -o client
//
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <vector>
#include <thread>

#include <iostream>
#include <sstream>
#include <thread>
#include <map>
#include <time.h>

std::string getTimestamp() {
    time_t now = time(nullptr);
    struct tm* local = localtime(&now);
    char buffer[20];

    strftime(buffer, sizeof(buffer), "[%d/%m/%y-%H:%M:%S]", local);
    return std::string(buffer);
}

// Threaded function for handling responss from server
void listenServer(int serverSocket)
{
    int nread;                                  // Bytes read from socket
    char buffer[1025];                          // Buffer for reading input

    while(true)
    {
       memset(buffer, 0, sizeof(buffer));
       nread = read(serverSocket, buffer, sizeof(buffer));

       if(nread == 0)                      // Server has dropped us
       {
          printf("Over and Out\n");
          exit(0);
       }
       else if(nread > 0)
       {
            std::cout << getTimestamp() << "]" << " Received: " << buffer << std::endl;
            break;
       }
       printf("\n");
    }
}


int main(int argc, char* argv[])
{
   struct addrinfo hints, *svr;              // Network host entry for server
   struct sockaddr_in serv_addr;           // Socket address for server
   int serverSocket;                         // Socket used for server 
   int nwrite;                               // No. bytes written to server
   char buffer[1025];                        // buffer for writing to server
   bool finished;                   
   int set = 1;                              // Toggle for setsockopt

   if(argc != 3)
   {
        printf("Usage: chat_client <ip  port>\n");
        printf("Ctrl-C to terminate\n");
        exit(0);
   }

   hints.ai_family   = AF_INET;            // IPv4 only addresses
   hints.ai_socktype = SOCK_STREAM;

   memset(&hints,   0, sizeof(hints));

   if(getaddrinfo(argv[1], argv[2], &hints, &svr) != 0)
   {
       perror("getaddrinfo failed: ");
       exit(0);
   }

   struct hostent *server;
   server = gethostbyname(argv[1]);

   bzero((char *) &serv_addr, sizeof(serv_addr));
   serv_addr.sin_family = AF_INET;
   bcopy((char *)server->h_addr,
      (char *)&serv_addr.sin_addr.s_addr,
      server->h_length);
   serv_addr.sin_port = htons(atoi(argv[2]));

   serverSocket = socket(AF_INET, SOCK_STREAM, 0);

   // Turn on SO_REUSEADDR to allow socket to be quickly reused after 
   // program exit.

   if(setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &set, sizeof(set)) < 0)
   {
       printf("Failed to set SO_REUSEADDR for port %s\n", argv[2]);
       perror("setsockopt failed: ");
   }

   
   if(connect(serverSocket, (struct sockaddr *)&serv_addr, sizeof(serv_addr) )< 0)
   {
       // EINPROGRESS means that the connection is still being setup. Typically this
       // only occurs with non-blocking sockets. (The serverSocket above is explicitly
       // not in non-blocking mode, so this check here is just an example of how to
       // handle this properly.)
       if(errno != EINPROGRESS)
       {
         printf("Failed to open socket to server: %s\n", argv[1]);
         perror("Connect failed: ");
         exit(0);
       }
   }

    // Send handshake after connection
    //const char* handshake_msg = "CLIENT_CONNECT";
    const char* handshake_msg = "CLIENT_CONNECT";
    send(serverSocket, handshake_msg, strlen(handshake_msg), 0);

    // Listen and print replies from server
    //std::thread serverThread(listenServer, serverSocket);

    finished = false;
    while(!finished)
    {
        std::cout << "Menu:" << std::endl;
        std::cout << "1. List all connected servers" << std::endl;
        std::cout << "2. Get message for group" << std::endl;
        std::cout << "3. Get all messages for group" << std::endl;
        std::cout << "4. Send message to group" << std::endl;
        std::cout << "5. Get statusreq for group" << std::endl;
        std::cout << "6. Exit \n";

        int choice;
        std::cin >> choice;
        std::cin.ignore();

        std::string groupID;
        std::string message;
        std::string cmd;

        switch(choice){
            case 1:
                cmd = "LISTSERVERS";
                break;
            case 2:
                std::cout << "Enter group id:";
                std::cin >> groupID;
                cmd = "GETMSG," + groupID;
                break;
            case 3:
                std::cout << "Enter group id:";
                std::cin >> groupID;
                cmd = "GETALLMSG," + groupID;
                break;
            case 4:
                std::cout << "Enter group id:";
                std::cin >> groupID;
                std::cin.ignore();  // Clear the newline from previous input
                std::cout << "Enter your message: ";
                std::getline(std::cin, message);
                cmd = "SENDMSG," + groupID + "," + message;
                break;
            case 5:
                std::cout << "Enter group id:";
                std::cin >> groupID;
                cmd = "STATUSREQ," + groupID;
                break;
            case 6:
                std::cout << "Exiting.." << std::endl;
                finished = true;
                exit(0);
            default:
                std::cout << "Please enter a number between 1-5." << std::endl;
                continue;
        }


        bzero(buffer, sizeof(buffer));
        strncpy(buffer, cmd.c_str(), sizeof(buffer) - 1);
        nwrite = send(serverSocket, buffer, strlen(buffer) + 1,0);

        if(nwrite  == -1)
        {
            perror("send() to server failed: ");
            finished = true;
        }
        else
        {
            std::cout << getTimestamp() << "]" << " Sent: " << buffer << std::endl;
        }
        listenServer(serverSocket);
   }
}