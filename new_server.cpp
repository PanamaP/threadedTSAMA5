//
// Simple botnet server for TSAM-409 Assignment 5
//
// Compile: g++ -Wall -std=c++11 new_server.cpp -o tsamgroup37
//
// Command line: ./tsamgroup <port>
//
// Authors: Elfar Snær Arnarson (elfar21@ru.is), Kristján Þór Matthíasson (kristjanm20@ru.is)
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
#define MAX_CONNECTED_SERVERS 10

/*
* Class for storing messages.
*/
class Message {
public:
    std::string fromGroupID;
    std::string content;

    Message(std::string fromGroupID, std::string content) : fromGroupID(fromGroupID), content(content) {}
    ~Message() {} // destructor
};

/*
* Class for storing connections.
*/
class Connection {
public:
    int csocket;
    std::string ip;
    int port;
    std::string groupID;

    // For comparing connections
    bool operator==(const Connection& other) const {
        return csocket == other.csocket && ip == other.ip && port == other.port && groupID == other.groupID;
    }

    Connection(int csocket, std::string ip, int port, std::string groupID) 
        : csocket(csocket), ip(ip), port(port), groupID(groupID) {}
    
    ~Connection() {} // destructor
};

/*
* Base class for the server.
*/
class Server {
private:
    int sock;               // Socket for connections to server
    int port;               // Port to listen on
    std::string groupID;    // Group ID
    std::string ip;         // IP address of this server
    const int BACKLOG = 10; // Number of server to server connections to queue

    std::vector<Connection> server_connections; // Connected servers
    std::vector<Connection> client_connections; // Connected clients
    std::map<int, int> messages_waiting;        // sockfd -> number of messages waiting
    std::map<std::string, std::vector<Message>> messages_for_groups; // Messages stored for each group, groupID -> messages

    std::mutex server_connections_mutex; // Mutex for server_connections
    std::mutex client_connections_mutex; // Mutex for client_connections
    std::mutex messages_mutex;           // Mutex for messages_for_groups

public:
    // Constructor
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
        const int BUFFER_SIZE = 5000;       // Size of buffer for reading data
        char buffer[BUFFER_SIZE];           // Buffer for reading data
        int nread;                          // Number of bytes read
        memset(buffer, 0, sizeof(buffer));  // Initialize buffer

        // While loop to accept connections
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

            nread = recv(newsock, buffer, sizeof(buffer), 0);
            std::string message(buffer, nread);

            if (message.find("QUERYSERVERS,") != std::string::npos)
            {
                // Create new server connection
                std::string connectingIP = inet_ntoa(sk_addr.sin_addr);                     // Get connecting IP
                int connectingPort = ntohs(sk_addr.sin_port);                               // Get connecting port
                std::string groupID = cleanString(message.substr(message.find(",") + 1));   // Get groupID

                Connection newConnection(newsock, connectingIP, connectingPort, groupID); // Create new connection
                std::cout << "New server connection accepted" << "IP: " << connectingIP << " Port: " << connectingPort << std::endl;

                // Lock the thread and add new connection
                server_connections_mutex.lock();
                server_connections.push_back(newConnection);
                server_connections_mutex.unlock();

                // New thread to handle messages from this server
                std::thread handleServerMessagesThread(&Server::handleServerMessages, this, newConnection);
                handleServerMessagesThread.detach(); // Detach so main doesnt wait for it
            }
            else if (message.find("CLIENT_CONNECT") != std::string::npos)
            {
                std::string connectingIP = inet_ntoa(sk_addr.sin_addr);                     // Get connecting IP
                int connectingPort = ntohs(sk_addr.sin_port);                               // Get connecting port
                Connection newConnection(newsock, connectingIP, connectingPort, "client");  // Create new connection

                std::cout << "New client connection accepted " << "IP: " << connectingIP << " Port: " << connectingPort << " User: " << newConnection.groupID << std::endl;

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
                sendMessageToServer(Connection(newsock, "", 0, ""), "ERROR: Did not receive handshake");
                std::cout << "Did not receive handshake from connection" << std::endl;
                close(newsock);
            }
        }
    }

    // Handle server messages
    void handleServerMessages(Connection connection) 
    {
        const int BUFFER_SIZE = 5000;   // Size of buffer for reading data
        char buffer[BUFFER_SIZE];       // Buffer for reading data
        int nread;                      // Number of bytes read

        while(true)
        {
            memset(buffer, 0, sizeof(buffer));

            //nread = recv(connection.socket, buffer, sizeof(buffer), 0);
            nread = read(connection.csocket, buffer, sizeof(buffer));

            if(nread <= 0){
                // Connection closed
                std::cout << "Server connection closed. " << "IP: " << connection.ip << " Port: " << connection.port << " GroupID: " << connection.groupID << std::endl;
                server_connections_mutex.lock();
                
                // Remove connection from server_connections
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
            
            if (message.find("QUERYSERVERS,") != std::string::npos)
            {
                // Send list of servers to server
                process_QUERYSERVERS(connection);
            }
            else if (message.find("SERVERS,") != std::string::npos)
            {
                // Add servers to server_connections
                std::string serverList = message;
                std::string delimiter = ";";
                size_t pos = 0;
                std::string token;
                while ((pos = serverList.find(delimiter)) != std::string::npos) {
                    token = cleanString(serverList.substr(0, pos));        

                    if(token.find("SERVERS,") != std::string::npos)
                    {
                        server_connections_mutex.lock();
                        // Get the groupID
                        std::string groupID = cleanString(token.substr(token.find(",") + 1));
                        groupID = cleanString(groupID.substr(0, groupID.find(",")));
                        // Get the IP
                        std::string ip = token.substr(token.find(",") + 1);
                        ip = ip.substr(ip.find(",") + 1);
                        ip = ip.substr(0, ip.find(","));
                        // Get the port
                        std::string port = token.substr(token.find(",") + 1);
                        port = port.substr(port.find(",") + 1);
                        port = port.substr(port.find(",") + 1);

                        // If input is read wrong, skip this server. Some servers send extra STX and ETX, this take care of most of them.
                        if(groupID.find("\002") != std::string::npos || groupID.find("\003") != std::string::npos)
                        {
                            server_connections_mutex.unlock();
                            break;
                        }

                        // If the server is already in the list, update it
                        for (size_t i = 0; i < server_connections.size(); ++i) {
                            if (server_connections[i] == connection) {
                                Connection editConnection = server_connections[i];
                                editConnection.groupID = groupID;
                                editConnection.ip = ip;
                                
                                try {
                                    int newPort = std::stoi(port);
                                    editConnection.port = newPort;
                                } catch (const std::invalid_argument& ia) {
                                    server_connections_mutex.unlock();
                                    break;
                                }
                                server_connections[i] = editConnection;
                                break;
                            }
                        }
                        server_connections_mutex.unlock();
                    }
                    else if(token.find("P3_GROUP_") != std::string::npos || token.find("Instr_") != std::string::npos || token.find("ORACLE") != std::string::npos)
                    {                        
                        std::string groupID = token.substr(0, token.find(","));
                        std::string port = token.substr(token.find(",") + 1);

                        port = port.substr(port.find(",") + 1);
                        port = port.substr(0, port.find(","));

                        std::string ip = token.substr(token.find(",") + 1);
                        ip = ip.substr(0, ip.find(","));

                        // If input is read wrong, skip this erver.
                        if(groupID != this->groupID && port != "-1" && server_connections.size() < MAX_CONNECTED_SERVERS)
                        {
                            server_connections_mutex.lock();
                            bool connected = false;
                            for (size_t i = 0; i < server_connections.size(); ++i) {
                                if (server_connections[i].groupID == groupID && server_connections[i].ip == ip) {
                                    connected = true;
                                    break;
                                }
                            }
                            if(!connected)
                            {   
                                try {
                                    int newPort = std::stoi(port);
                                    server_connections_mutex.unlock();
                                    connectToServer(ip, newPort, groupID);
                                } catch (const std::invalid_argument& ia) {
                                    server_connections_mutex.unlock();
                                    break;
                                }
                            }
                            else
                            {
                                server_connections_mutex.unlock();
                            }
                        }

                    }
                    serverList.erase(0, pos + delimiter.length());
                }
            }
            else if (message.find("KEEPALIVE,") != std::string::npos)
            {
                messages_mutex.lock();
                std::string numberOfMessageStr = cleanString(message.substr(message.find(",") + 1));
                try {
                    int numberOfMessages = std::stoi(numberOfMessageStr);
                    std::cout << "Received KEEPALIVE from server: " << connection.groupID << " with number of messages: " << numberOfMessages << std::endl;
                    messages_waiting[connection.csocket] += numberOfMessages;

                    // If there are messages waiting for this server, fetch them
                    if(numberOfMessages > 0){
                        sendMessageToServer(connection, "FETCH_MSGS," + this->groupID ); 
                    }
                    messages_mutex.unlock();
                } catch (const std::invalid_argument& ia) {
                    std::cerr << "Invalid numberOfMessages: " << ia.what() << std::endl;
                    messages_mutex.unlock();
                    break;
                }
            }
            else if (message.find("FETCH_MSGS,") != std::string::npos)
            {
                std::string groupID = cleanString(message.substr(message.find(",") + 1));
                groupID = (groupID.substr(0, groupID.find("\n")));

                // Check if there are messages for this group
                if(messages_for_groups.find(groupID) != messages_for_groups.end())
                {
                    for(const auto& messageToGroup : messages_for_groups[groupID])
                    {
                        std::string sendmsg = "SEND_MSG," + groupID + "," + messageToGroup.fromGroupID + "," + messageToGroup.content;
                        sendMessageToServer(connection, sendmsg);
                        for(const auto& client : client_connections)
                        {
                            std::string resp = "Sent message to server. GroupID: " + groupID + " From: " + messageToGroup.fromGroupID + " Message: " + messageToGroup.content + "\n";
                            sendMessageToClient(client, resp);
                            
                        }
                        std::cout << "Sent message to server. GroupID: " << groupID << " From: " << messageToGroup.fromGroupID << " Message: " << messageToGroup.content << std::endl;
                    }
                    messages_mutex.lock();
                    messages_for_groups[groupID].clear();
                    messages_mutex.unlock();
                }
            } 
            else if (message.find("SEND_MSG,") != std::string::npos)
            {
                std::string sendmsg = cleanString(message);
                process_SEND_MSG(connection, sendmsg);
            }
            else if (message.find("STATUSREQ,") != std::string::npos)
            {
                std::string fromGroupID = cleanString(message.substr(message.find(",") + 1));
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
            else
            {
                std::cout << "UNRECOGNIZED message from server " << connection.groupID << ", message: " << message << std::endl;

            }
        }
    }

    // Handle client messages
    void handleClientMessages(Connection connection) {
        const int BUFFER_SIZE = 5000;   // Size of buffer for reading data
        char buffer[BUFFER_SIZE];       // Buffer for reading data
        int nread;                      // Number of bytes read

        while(true)
        {
            memset(buffer, 0, sizeof(buffer));
            nread = read(connection.csocket, buffer, sizeof(buffer));

            if(nread <= 0){
                // Connection closed
                std::cout << "Client connection closed. " << "IP: " << connection.ip << " Port: " << connection.port << std::endl;
                client_connections_mutex.lock();
                
                // Remove connection from client_connections
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
                messages_mutex.lock();

                // Check if there are messages for this group, grab the first one
                if(messages_for_groups.find(groupID) != messages_for_groups.end())
                {
                    sendMessageToClient(connection, messages_for_groups[groupID][0].content);
                    messages_for_groups[groupID].erase(messages_for_groups[groupID].begin());
                    messages_waiting[connection.csocket] -= 1;
                }
                else
                {
                    if (groupID == this->groupID)
                    {
                        for(const auto& pair : messages_waiting){
                            // If there is a message for us, fetch it
                            if(pair.second > 0)
                            {
                                for(Connection serverConnection : server_connections)
                                {
                                    if(serverConnection.csocket == pair.first)
                                    {
                                        sendMessageToServer(serverConnection, "FETCH_MSGS," + this->groupID);
                                    }
                                }
                            }
                        }                                
                    }
                    else
                    {
                        sendMessageToClient(connection, "No messages for group: " + groupID);
                    }
                }
                messages_mutex.unlock();
                
            }
            else if (message.find("SENDMSG,") != std::string::npos)
            {
                size_t toGroupIdPos = message.find(",");
                size_t messagePos = message.find(",", toGroupIdPos + 1);

                // If it failed to extract any of the tokens
                if(toGroupIdPos == std::string::npos || messagePos == std::string::npos)
                {
                    sendMessageToClient(connection, "SEND MSG,ERROR. Format should be: SEND_MSG,<TO GROUP ID>,<Message content>");
                    return;
                }
                // Get each token by pos in message
                std::string toGroupID = message.substr(toGroupIdPos + 1, messagePos - toGroupIdPos - 1);
                std::string content = message.substr(messagePos + 1);
                std::string messageToServer = "SEND_MSG," + toGroupID + "," + this->groupID + "," + content;
                
                process_SEND_MSG(connection, messageToServer);
            }
            else if (message.find("LISTSERVERS") != std::string::npos)
            {
                // Send list of servers to server
                process_QUERYSERVERS(connection);
            }
            else if (message.find("GETALLMSG,") != std::string::npos)
            {
                std::string groupID = message.substr(message.find(",") + 1);

                // Check if there are messages for this group
                if(messages_for_groups.find(groupID) != messages_for_groups.end())
                {
                    for(const auto& messageToGroup : messages_for_groups[groupID])
                    {
                        sendMessageToClient(connection, messageToGroup.content);
                    }
                    messages_mutex.lock();
                    messages_for_groups[groupID].clear();
                    messages_waiting[connection.csocket] = 0;
                    messages_mutex.unlock();
                }

                if (groupID == this->groupID)
                {
                    for(const auto& pair : messages_waiting){
                        // If there is a message for us, fetch it
                        if(pair.second > 0)
                        {
                            for(Connection serverConnection : server_connections)
                            {
                                if(serverConnection.csocket == pair.first)
                                {
                                    sendMessageToServer(serverConnection, "FETCH_MSGS," + this->groupID);
                                }
                            }
                        }
                    }                                
                }
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
            return;
        }

        if (connect(connectionSocket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
        {
            close(connectionSocket);
            return;
        }


        // Lock the thread and add new connection
        server_connections_mutex.lock();
        Connection newConnection(connectionSocket, targetIP, targetPort, groupID);
        bool exists = false;
        for(const auto& server : server_connections)
        {
            if(server.groupID == groupID && server.ip == targetIP)
            {
                exists = true;
                break;
            }
        }
        if(!exists)
        {
            server_connections.push_back(newConnection);
            server_connections_mutex.unlock();
        }
        else
        {
            server_connections_mutex.unlock();
            close(connectionSocket);
            return;
        }

        std::cout << "Connected to server " << groupID << " IP: " << targetIP << " Port: " << targetPort << std::endl;
        
        sendHandshakeMesage(newConnection);

        // New thread to handle messages from this server
        std::thread handleConnectMessagesThread(&Server::handleServerMessages, this, newConnection);
        handleConnectMessagesThread.detach(); // Detach so main doesnt wait for it
    }

    // Send handshake message to server
    void sendHandshakeMesage(const Connection& serverConnection)
    {
        sendMessageToServer(serverConnection, "QUERYSERVERS," + this->groupID);
    }

    // Send keep alive message to connection
    void sendKeepAlive(const Connection& connection)
    {
        int count = 0;
        // If connection socket doesn't exist it will create it with value 0
        for(const auto& pair : messages_for_groups)
        {
            if(connection.groupID == pair.first)
            {
                count = pair.second.size();
                break;
            }
        }

        if(sendMessageToServer(connection, "KEEPALIVE," + std::to_string(count)))
        {
            std::cout << "Sent KEEPALIVE to server: " << connection.groupID << " with number of messages: " << count << std::endl;
        }
        else
        {
            std::cout << "Failed to send KEEPALIVE to server: " << connection.groupID << std::endl;
            close(connection.csocket);
            server_connections_mutex.lock();
            for (size_t i = 0; i < server_connections.size(); ++i) {
                if (server_connections[i] == connection) {
                    server_connections.erase(server_connections.begin() + i);
                    break;
                }
            }
            server_connections_mutex.unlock();
        }
    }

    // Send message to server with STX and ETX
    bool sendMessageToServer(const Connection& serverConnection, std::string message) 
    {
        message = std::string(1, STX) + message + std::string(1, ETX);
        if(send(serverConnection.csocket, message.c_str(), message.size(), MSG_NOSIGNAL) < 0)
        {
            std::cout << "Failed to send message to server: " << serverConnection.groupID << std::endl;
            return false;
        }
        return true;
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

    // Clean string of STX and ETX
    std::string cleanString(std::string str)
    {   
        if (!str.empty() && str[0] == '\002') { // Check for STX
            str.erase(0, 1);
        }
        size_t pos = str.find('\003'); // Check for ETX
        if (pos != std::string::npos) {
            str.erase(pos, 1);
        }
        return str;
    }

    // Process the received QUERYSERVERS command
    void process_QUERYSERVERS(const Connection connection)
    {
        // Send list of servers to server
        server_connections_mutex.lock();
        std::string serverList = "SERVERS," + this->groupID + "," + ip + "," + std::to_string(port) + ";";
        for (size_t i = 0; i < server_connections.size(); i++)
        {
            serverList += server_connections[i].groupID + "," + server_connections[i].ip + "," + std::to_string(server_connections[i].port) + ";";
        }
        server_connections_mutex.unlock();

        if (connection.groupID != "client") 
        {
            sendMessageToServer(connection, serverList);
            sendHandshakeMesage(connection);
        }
        else
        {
            sendMessageToClient(connection, serverList);
        }
    }

    // Process the received SEND_MSG command
    void process_SEND_MSG(const Connection connection, const std::string& message)
    {
        // Get the position of all the commas
        size_t toGroupIdPos = message.find(",");
        size_t fromGroupIdPOs = message.find(",", toGroupIdPos + 1);
        size_t messagePos = message.find(",", fromGroupIdPOs + 1);


        // If it failed to extract any of the tokens
        if(toGroupIdPos == std::string::npos || fromGroupIdPOs == std::string::npos || messagePos == std::string::npos)
        {
            sendMessageToServer(connection, "SEND MSG,ERROR. Format should be: SEND_MSG,<TO GROUP ID>,<FROM GROUP ID>,<Message content>");
            return;
        }

        // Get each token by pos in message
        std::string toGroupID = message.substr(toGroupIdPos + 1, fromGroupIdPOs - toGroupIdPos - 1);
        std::string fromGroupID = message.substr(fromGroupIdPOs + 1, messagePos - fromGroupIdPOs - 1);
        std::string content = message.substr(messagePos + 1);

        if(toGroupID == this->groupID)
        {
            // If the message is for this server, send it to all clients
            for(const auto& client : client_connections)
            {
                std::string response = "Message from " + fromGroupID + ": " + content;
                sendMessageToClient(client, response);
            }
        }
        else
        {
            // If the message is not for this server, store it
            messages_mutex.lock();
            messages_for_groups[toGroupID].push_back(Message(fromGroupID, content));
            messages_mutex.unlock();

            std::cout << "Message stored on server for group: " << toGroupID << std::endl;
            sendMessageToClient(connection,"Message stored on server for group: " + toGroupID);
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
                counter++;
            } 
        }
        if (ifAddrStruct != nullptr) freeifaddrs(ifAddrStruct);
        return addressBufferResponse;
    }

    // Set the IP of this server
    void setServerIP(std::string ip)
    {
        this->ip = ip;
    }

    // Set the groupID of this server
    void setGroupID(std::string groupID)
    {
        this->groupID = groupID;
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

    // Send handshake every minute to all servers if max connection is below 10
    void handshakeTracker()
    {
        while(true)
        {
            std::this_thread::sleep_for(std::chrono::minutes(1));
            std::cout << "CONNECTED SERVERS OUT OF MAX: " << server_connections.size() << "/" << MAX_CONNECTED_SERVERS << std::endl;

            if(server_connections.size() < MAX_CONNECTED_SERVERS)
            {
                for(const auto& connection : server_connections)
                {
                    sendHandshakeMesage(connection);
                }
            }

            // Hardcode failsafe, if we lose connection to all servers, connect to instructor server
            if(server_connections.size() == 0)
            {
                connectToServer("130.208.243.61", 4003, "Instr_3");
            }
        }
    }
};

int main(int argc, char *argv[])
{
    // Check for correct number of arguments
    if (argc != 2)
    {
        printf("Usage: ./server <port>\n");
        exit(0);
    }

    int port = atoi(argv[1]);                   // Port number for server
    Server server(port);               
    server.setServerIP(server.thisServerIP());  // Set the IP of this server
    server.setGroupID("P3_GROUP_37");           // Set the groupID of this server

    // Setup the server for listening
    server.createSocket();
    server.bindSocket();
    server.listenSocket();


    // Thread for accepting connections
    std::thread accept_connection_thread(&Server::acceptConnection, &server);

    // Thread to send keep alive messages every minute to all connected servers
    std::thread keep_alive_thread(&Server::keepAliveTracker, &server);
    keep_alive_thread.detach();

    // Thread to send handshake every 5 minutes to all servers if max connection is below 10
    std::thread handshake_thread(&Server::handshakeTracker, &server);
    handshake_thread.detach();

    // Connect to instructor server to start
    //server.connectToServer("130.208.243.61", 4001, "Instr_1");
    server.connectToServer("130.208.243.61", 4003, "Instr_3");

    accept_connection_thread.join();

    return 0;
}