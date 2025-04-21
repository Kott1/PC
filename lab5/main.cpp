#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

using namespace std;

#define PORT 8080
const string PAGE_DIR = "pages";

void sendResponse(SOCKET clientSock, const string& status, const string& contentType, const string& body) {
    ostringstream oss;
    oss << "HTTP/1.1 " << status << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Content-Type: "  << contentType << "\r\n"
        << "Connection: close\r\n\r\n"
        << body;
    auto resp = oss.str();
    send(clientSock, resp.c_str(), (int)resp.size(), 0);
}

void handleClient(SOCKET clientSock) {
    char buffer[4096];
    int bytes = recv(clientSock, buffer, sizeof(buffer) - 1, 0);
    if (bytes <= 0) {
        closesocket(clientSock);
        return;
    }
    buffer[bytes] = '\0';

    istringstream req(buffer);
    string method, path, version;
    req >> method >> path >> version;

    if (method != "GET") {
        sendResponse(clientSock, "405 Method Not Allowed",
                     "text/plain", "Method Not Allowed");
        closesocket(clientSock);
        return;
    }

    if (path == "/") path = "/home.html";
    string filePath = PAGE_DIR + path;

    ifstream ifs(filePath, ios::binary);
    if (!ifs) {
        string body =
            "<!DOCTYPE html>"
            "<html><head><meta charset=\"utf-8\">"
            "<title>404 Not Found</title>"
            "<style>"
            "body { font-family: Arial, sans-serif; text-align: center; padding-top: 50px; }"
            "h1 { font-size: 48px; color: #cc0000; }"
            "p  { font-size: 24px; color: #555; }"
            "</style>"
            "</head><body>"
            "<h1>404 Not Found</h1>"
            "</body></html>";
        sendResponse(clientSock, "404 Not Found", "text/html", body);
    } else {
        string body((istreambuf_iterator<char>(ifs)), {});
        string ct = (path.find(".html") != string::npos)
                       ? "text/html"
                       : "application/octet-stream";
        sendResponse(clientSock, "200 OK", ct, body);
    }

    closesocket(clientSock);
}

int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
        cerr << "WSAStartup failed\n";
        return 1;
    }

    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock == INVALID_SOCKET) {
        cerr << "socket() failed\n";
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_addr.S_un.S_addr = INADDR_ANY;

    int opt = 1;
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    if (bind(listenSock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        cerr << "bind() failed\n";
        closesocket(listenSock);
        WSACleanup();
        return 1;
    }

    if (listen(listenSock, SOMAXCONN) == SOCKET_ERROR) {
        cerr << "listen() failed\n";
        closesocket(listenSock);
        WSACleanup();
        return 1;
    }

    cout << "Listening on port " << PORT << "...\n";
    while (true) {
        sockaddr_in clientAddr;
        int addrLen = sizeof(clientAddr);

        SOCKET clientSock = accept(listenSock, (sockaddr*)&clientAddr, &addrLen);
        if (clientSock == INVALID_SOCKET) {
            cerr << "accept() failed\n";
            continue;
        }

        thread(handleClient, clientSock).detach();
    }

    closesocket(listenSock);
    WSACleanup();
    return 0;
}