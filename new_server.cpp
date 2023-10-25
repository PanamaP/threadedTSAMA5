//
// Simple botnet server for TSAM-409 Assignment 5
//
// Compile: g++ -Wall -std=c++11 new_server.cpp -o tsamgroup37
//
// Command line: ./tsamgroup <port>
//
// Author: Elfar Snær Arnarson (elfar21@ru.is)
//


#include <iostream>
#include <string>
#include <stdio.h>
#include <string.h>

#include <vector>
#include <map>
#include <queue>
#include <thread>
#include <mutex>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <net/if.h>
#include <ifaddrs.h>

#define STX '\002'
#define ETX '\003'


class Message {
public:
    std::string fromGroupID;
    std::string content;

    Message(std::string fromGroupID, std::string content) : fromGroupID(fromGroupID), content(content) {}
    ~Message() {} // destructor
};

class Connection {
public:
    int csocket;
    std::string ip;
    int port;
    std::string groupID;

    bool operator==(const Connection& other) const {
        return csocket == other.csocket && ip == other.ip && port == other.port && groupID == other.groupID;
    }

    Connection(int csocket, std::string ip, int port, std::string groupID) : csocket(csocket), ip(ip), port(port), groupID(groupID) {}
    ~Connection() {} // destructor
};

class Server {
private:
    int sock;               // Socket for connections to server
    int port;               // Port to listen on
    std::string groupID;    // Group ID
    std::string ip;         // IP address of this server
    const int BACKLOG = 10; // Number of server to server connections to queue
    const int ClientBacklog = 10; // Number of client to server connections to queue

    std::vector<Connection> server_connections;
    std::vector<Connection> client_connections;
    std::map<int, int> messages_waiting; // sockfd -> number of messages waiting
    std::map<std::string, std::vector<Message>> messages_for_groups; // groupID -> messages

    std::mutex server_connections_mutex;
    std::mutex client_connections_mutex;
    std::mutex messages_mutex;

public:
    Server(int port) : port(port) {}
    virtual ~Server() {} // destructor

    // Initialize the socket
    int createSocket() 
    {
        int set = 1;                // for setsockopt
        if ((sock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) < 0)
        {
            perror("Failed to open socket");
            return (-1);
        }

        // Turn on SO_REUSEADDR to allow socket to be quickly reused after
        // program exit.

        if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &set, sizeof(set)) < 0)
        {
            perror("Failed to set SO_REUSEADDR:");
            return (-1);
        }

        return sock;
    }

    // Binds the socket to the port
    int bindSocket() 
    {
        struct sockaddr_in sk_addr; // address settings for bind()
        memset(&sk_addr, 0, sizeof(sk_addr));

        sk_addr.sin_family = AF_INET;
        sk_addr.sin_addr.s_addr = INADDR_ANY;
        sk_addr.sin_port = htons(port);

        // Bind to socket to listen for connections
        if (bind(sock, (struct sockaddr *)&sk_addr, sizeof(sk_addr)) < 0)
        {
            perror("Failed to bind to socket:");
            return (-1);
        }
        else
        {
            return (sock);
        }
    }

    // Listen for connections
    int listenSocket() 
    {
        if (listen(sock, BACKLOG) < 0)
        {
            perror("Failed to listen on socket.");
            return (-1);
        }
        
        return 1;
    }

    // Accept connections
    void acceptConnection() {
        const int BUFFER_SIZE = 5000;
        char buffer[BUFFER_SIZE];
        int nread;
        memset(buffer, 0, sizeof(buffer));

        while(true)
        {
            struct sockaddr_in sk_addr; // address settings of server
            socklen_t sk_addr_len = sizeof(sk_addr); // length of address settings struct

            int newsock = accept(sock, (struct sockaddr *)&sk_addr, &sk_addr_len);
            if(newsock < 0)
            {
                // nothing to accept, keep waiting
                continue;
            }

            std::cout << "Checking handshake from connection" << std::endl;
            nread = recv(newsock, buffer, sizeof(buffer), 0);
            std::string message(buffer, nread);

            if (message.find("QUERYSERVERS,") != std::string::npos)
            {
                std::cout << "Received message from server: " << message << std::endl;
                std::string connectingIP = inet_ntoa(sk_addr.sin_addr);
                int connectingPort = ntohs(sk_addr.sin_port);
                std::string groupID = message.substr(message.find(",") + 1);
                Connection newConnection(newsock, connectingIP, connectingPort, groupID);
                std::cout << "New server connection accepted" << std::endl;
                std::cout << "IP: " << connectingIP << " Port: " << connectingPort << std::endl;

                // Lock the thread and add new connection
                server_connections_mutex.lock();
                server_connections.push_back(newConnection);
                server_connections_mutex.unlock();

                sendHandshakeMesage(newConnection);

                // New thread to handle messages from this server
                std::thread handleServerMessagesThread(&Server::handleServerMessages, this, newConnection);
                handleServerMessagesThread.detach(); // Detach so main doesnt wait for it
            }
            else if (message.find("CLIENT_CONNECT") != std::string::npos)
            {
                std::cout << "Received message from server: " << message << std::endl;
                std::string connectingIP = inet_ntoa(sk_addr.sin_addr);
                int connectingPort = ntohs(sk_addr.sin_port);
                Connection newConnection(newsock, connectingIP, connectingPort, "");
                std::cout << "New client connection accepted" << std::endl;
                std::cout << "IP: " << connectingIP << " Port: " << connectingPort << std::endl;

                // Lock the thread and add new connection
                client_connections_mutex.lock();
                client_connections.push_back(newConnection);
                client_connections_mutex.unlock();

                // New thread to handle messages from this client
                std::thread handleclientMessagesThread(&Server::handleClientMessages, this, newConnection);
                handleclientMessagesThread.detach(); // Detach so main doesnt wait for it
            }
            else
            {
                // Didn't receive a handshake
                sendMessageToClient(Connection(newsock, "", 0, ""), "ERROR: Did not receive handshake");
                std::cout << "Did not receive handshake from connection" << std::endl;
                close(newsock);
                break;
            }
        }
    }

    // Handle server messages
    void handleServerMessages(Connection connection) 
    {
        const int BUFFER_SIZE = 5000;
        char buffer[BUFFER_SIZE];
        int nread;

        std::cout << "Handling server messages" << std::endl;

        while(true)
        {
            memset(buffer, 0, sizeof(buffer));

            //nread = recv(connection.socket, buffer, sizeof(buffer), 0);
            nread = read(connection.csocket, buffer, sizeof(buffer));

            if(nread <= 0){
                // Connection closed
                std::cout << "Server connection closed. " << "IP: " << connection.ip << " Port: " << connection.port << " GroupID: " << connection.groupID << std::endl;
                server_connections_mutex.lock();
                
                for (size_t i = 0; i < server_connections.size(); ++i) {
                    if (server_connections[i] == connection) {
                        server_connections.erase(server_connections.begin() + i);
                        break;
                    }
                }
                
                server_connections_mutex.unlock();
                close(connection.csocket);
                break;
            }

            std::string message(buffer);
            std::cout << "Received message from server: " << message << std::endl;

            if (message.find("QUERYSERVERS,") != std::string::npos)
            {
                // Send list of servers to server
                process_QUERYSERVERS(connection);
            }
            else if (message.find("SERVERS,") != std::string::npos) // TODO: This currently only prints the server list
            {
                // Add servers to server_connections
                std::string serverList = message;
                std::cout << "Server list: " << serverList << std::endl;
                std::string delimiter = ";";
                size_t pos = 0;
                std::string token;
                while ((pos = serverList.find(delimiter)) != std::string::npos) {
                    token = serverList.substr(0, pos);
                    std::cout << token << std::endl;
                    serverList.erase(0, pos + delimiter.length());
                }
            }
            else if (message.find("KEEPALIVE,") != std::string::npos)
            {
                std::string numberOfMessageStr = message.substr(message.find(",") + 1);
                int numberOfMessages = std::stoi(numberOfMessageStr);
                std::cout << "Received KEEPALIVE from server: " << connection.groupID << " with number of messages: " << numberOfMessages << std::endl;
                incrementMessagesWaiting(messages_waiting, connection.csocket, numberOfMessages);
            }
            else if (message.find("FETCH MSGS,") != std::string::npos)
            {
                std::string groupID = message.substr(message.find(",") + 1);
                messages_mutex.lock();

                // Check if there are messages for this group
                if(messages_for_groups.find(groupID) != messages_for_groups.end())
                {
                    for(const auto& messageToGroup : messages_for_groups[groupID])
                    {
                        sendMessageToServer(connection, messageToGroup.content);
                    }

                    // If the server request fetch is the same as the groupID, clear the messages
                    if (groupID == connection.groupID)
                    {
                        messages_for_groups[groupID].clear();
                        messages_waiting[connection.csocket] = 0;
                    }
                }
                messages_mutex.unlock();
            } 
            else if (message.find("SEND MSG,") != std::string::npos)
            {
                process_SEND_MSG(connection, message);
            }
            else if (message.find("STATUSREQ,") != std::string::npos)
            {
                std::string fromGroupID = message.substr(message.find(",") + 1);
                std::string status = "STATUSRESP," + this->groupID + "," + fromGroupID;
                messages_mutex.lock();
                for(const auto& pair : messages_for_groups)
                {
                    const std::string& serverGroupID = pair.first;
                    const size_t numberOfMessages = pair.second.size();

                    status += "," + serverGroupID + "," + std::to_string(numberOfMessages);
                }
                messages_mutex.unlock();
                sendMessageToServer(connection, status);
            }
        }
    }

    // Handle client messages
    void handleClientMessages(Connection connection) {
        const int BUFFER_SIZE = 5000;
        char buffer[BUFFER_SIZE];
        int nread;

        std::cout << "Handling client messages" << std::endl;

        while(true)
        {
            memset(buffer, 0, sizeof(buffer));
            nread = read(connection.csocket, buffer, sizeof(buffer));

            if(nread <= 0){
                // Connection closed
                std::cout << "Client connection closed. " << "IP: " << connection.ip << " Port: " << connection.port << std::endl;
                client_connections_mutex.lock();
                
                for (size_t i = 0; i < client_connections.size(); ++i) {
                    if (client_connections[i] == connection) {
                        client_connections.erase(client_connections.begin() + i);
                        break;
                    }
                }
                
                client_connections_mutex.unlock();
                close(connection.csocket);
                break;
            }

            std::string message(buffer);
            std::cout << "Received message from client: " << message << std::endl;

            // GETMSG, GROUP ID
            if (message.find("GETMSG,") != std::string::npos)
            {
                std::string groupID = message.substr(message.find(",") + 1);
                
            }
            else if (message.find("LISTSERVERS") != std::string::npos)
            {
                // Send list of servers to server
                process_QUERYSERVERS(connection);
            }
        }
    }

    // Connect to a server
    void connectToServer(const std::string& targetIP, int targetPort, const std::string& groupID) 
    {
        int connectionSocket = socket(AF_INET, SOCK_STREAM, 0);
        if(connectionSocket < 0)
        {
            perror("Failed to open socket");
            return;
        }

        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(targetPort);

        if (inet_pton(AF_INET, targetIP.c_str(), &server_addr.sin_addr) <= 0)
        {
            perror("Failed to get address");
            return;
        }

        if (connect(connectionSocket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
        {
            perror("Failed to connect to server");
            close(connectionSocket);
            return;
        }

        Connection newConnection(connectionSocket, targetIP, targetPort, groupID);

        // Lock the thread and add new connection
        server_connections_mutex.lock();
        server_connections.push_back(newConnection);
        server_connections_mutex.unlock();

        std::cout << "Connected to server" << std::endl;
        std::cout << "IP: " << targetIP << " Port: " << targetPort << std::endl;
        std::cout << "Checking on messages from server" << std::endl;
        
        sendHandshakeMesage(newConnection);

        // New thread to handle messages from this server
        std::thread handleConnectMessagesThread(&Server::handleServerMessages, this, newConnection);
        handleConnectMessagesThread.detach(); // Detach so main doesnt wait for it
    }

    // Send handshake message to server
    void sendHandshakeMesage(const Connection& serverConnection)
    {
        sendMessageToServer(serverConnection, "QUERYSERVERS,P3_GROUP_37");
        std::cout << "Sent handshake message to server, "  << std::endl;
    }

    // Send keep alive message to connection
    void sendKeepAlive(const Connection& connection)
    {
        messages_mutex.lock();
        // If connection socket doesn't exist it will create it with value 0
        int count = messages_waiting[connection.csocket]; 
        messages_mutex.unlock();

        sendMessageToServer(connection, "KEEPALIVE," + std::to_string(count));
        std::cout << "Sent KEEPALIVE to server with number of messages: " << count << std::endl;
    }

    // Send message to server with STX and ETX
    void sendMessageToServer(const Connection& serverConnection, std::string message) 
    {
        message = std::string(1, STX) + message + std::string(1, ETX);
        if(send(serverConnection.csocket, message.c_str(), message.size(), 0) < 0)
        {
            perror("Failed to send message to server");
            return;
        }
    }

    // Send message to server
    void sendMessageToClient(const Connection clientConnection, std::string message)
    {
        if(send(clientConnection.csocket, message.c_str(), message.size(), 0) < 0)
        {
            perror("Failed to send message to client");
            return;
        }
    }

    void process_QUERYSERVERS(const Connection connection)
    {
        // Send list of servers to server
        std::string serverList = "SERVERS,P3_GROUP_37," + ip + "," + std::to_string(port) + ";";
        for (size_t i = 0; i < server_connections.size(); i++)
        {
            serverList += server_connections[i].groupID + "," + server_connections[i].ip + "," + std::to_string(server_connections[i].port) + ";";
            std::cout << "Server: " << server_connections[i].ip << "," << server_connections[i].port << ";" << std::endl;
        }
        std::cout << "Sending server list: " << serverList << std::endl;
        sendMessageToServer(connection, serverList);
    }

    // Process the received SEND_MSG command
    void process_SEND_MSG(const Connection connection, const std::string& message)
    {
        // Get the position of all the commas
        size_t toGroupIdPos = message.find(",");
        size_t fromGroupIdPOs = message.find(",", toGroupIdPos + 1);
        size_t messagePos = message.find(",", fromGroupIdPOs + 1);

        // Get each token by pos in message
        std::string toGroupID = message.substr(toGroupIdPos + 1, fromGroupIdPOs - toGroupIdPos - 1);
        std::string fromGroupID = message.substr(fromGroupIdPOs + 1, messagePos - fromGroupIdPOs - 1);
        std::string content = message.substr(messagePos + 1);

        // If it failed to extract any of the tokens
        if(toGroupIdPos == std::string::npos || fromGroupIdPOs == std::string::npos || messagePos == std::string::npos)
        {
            sendMessageToServer(connection, "SEND MSG,ERROR. Format should be: SEND_MSG,<TO GROUP ID>,<FROM GROUP ID>,<Message content>");
            return;
        }

        messages_mutex.lock();
        messages_for_groups[toGroupID].push_back(Message(fromGroupID, content));
        messages_mutex.unlock();
        std::cout << "Received message for group " << toGroupID << " from " << fromGroupID << ": " << content << std::endl;

        if(toGroupID == this->groupID)
        {
            // If the message is for this server, send it to all clients
            for(const auto& client : client_connections)
            {
                sendMessageToClient(client, content);
            }
        }
        else
        {
            // If the message is not for this server, send it to the correct server
            bool sent = false;
            for(const auto& server : server_connections)
            {
                if(server.groupID == toGroupID)
                {
                    sendMessageToServer(server, message);
                    sent = true;
                    break;
                }
            }

            if(!sent)
            {
                std::cout << "No server with groupID " << toGroupID << " has been connected" << std::endl;
            }
        }
    }

    // Find the IP of this server
    std::string thisServerIP()
    {
        struct ifaddrs * ifAddrStruct = nullptr;
        struct ifaddrs * ifa = nullptr;
        void * tmpAddrPtr = nullptr;
        char addressBuffer[INET_ADDRSTRLEN];
        std::string addressBufferResponse;
        int counter = 0;

        getifaddrs(&ifAddrStruct);

        for (ifa = ifAddrStruct; ifa != nullptr; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr) {
                continue;
            }
            if (ifa->ifa_addr->sa_family == AF_INET) { // Check if it is IP4
                tmpAddrPtr = &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
                inet_ntop(AF_INET, tmpAddrPtr, addressBuffer, INET_ADDRSTRLEN);
                if(counter == 1)
                {
                    addressBufferResponse = addressBuffer;
                }
                std::cout << ifa->ifa_name << ": " << addressBuffer << std::endl;
                counter++;
            } 
        }
        if (ifAddrStruct != nullptr) freeifaddrs(ifAddrStruct);
        return addressBufferResponse;
    }

    void setServerIP(std::string ip)
    {
        this->ip = ip;
    }

    // Increment the number of messages waiting for a socket
    void incrementMessagesWaiting(std::map<int, int> messages_waiting, const int socket, const int numberOfMessages)
    {
        messages_mutex.lock();
        messages_waiting[socket] += numberOfMessages;
        messages_mutex.unlock();
    }

    // Set the number of messages waiting for a socket to a number
    void setMessagesWaiting(std::map<int, int> messages_waiting, const int socket, const int numberOfMessages)
    {
        messages_mutex.lock();
        messages_waiting[socket] = numberOfMessages;
        messages_mutex.unlock();
    }

    // Send keep alive message every minute to all connected servers
    void keepAliveTracker()
    {
        while(true)
        {
            std::this_thread::sleep_for(std::chrono::minutes(1));
            for(const auto& connection : server_connections)
            {
                sendKeepAlive(connection);
            }
        }
    }
};

int main(int argc, char *argv[])
{

    if (argc != 2)
    {
        printf("Usage: ./server <port>\n");
        exit(0);
    }

    int port = atoi(argv[1]);
    Server server(port);
    server.setServerIP(server.thisServerIP());

    // Setup the server for listening
    server.createSocket();
    server.bindSocket();
    server.listenSocket();


    // Thread for accepting connections
    std::thread accept_connection_thread(&Server::acceptConnection, &server);

    // Thread to send keep alive messages every minute to all connected servers
    std::thread keep_alive_thread(&Server::keepAliveTracker, &server);
    keep_alive_thread.detach();

    // Connect to instructor server to start
    server.connectToServer("130.208.243.61", 4001, "Instr_1");
    //server.connectToServer("130.208.243.61", 4003, "Instr_3");

    accept_connection_thread.join();

    return 0;
}