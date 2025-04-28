#include <winsock2.h>
#include <windows.h>
#include <iostream>
#include <vector>
#include <thread>
#include <random>

using namespace std;

constexpr uint16_t CMD_INIT   = 0x01;
constexpr uint16_t CMD_START  = 0x02;
constexpr uint16_t CMD_STATUS = 0x03;
constexpr uint16_t RSP_INIT   = 0x11;
constexpr uint16_t RSP_START  = 0x12;
constexpr uint16_t RSP_STATUS = 0x13;

mutex cout_mutex;

int sendAll(SOCKET sock, const char* buf, int len) {
    int total = 0;
    while (total < len) {
        int sent = send(sock, buf + total, len - total, 0);
        if (sent == SOCKET_ERROR) return SOCKET_ERROR;
        total += sent;
    }
    return total;
}

int recvAll(SOCKET sock, char* buf, int len) {
    int total = 0;
    while (total < len) {
        int rec = recv(sock, buf + total, len - total, 0);
        if (rec <= 0) return rec;
        total += rec;
    }
    return total;
}

vector<vector<int>> createRandomMatrix(int n) {
    vector<vector<int>> mat(n, vector<int>(n));
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            mat[i][j] = rand() % 1001;
        }
    }
    return mat;
}

void run_client(int client_id, const string& server_ip, int port) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        lock_guard<mutex> lock(cout_mutex);
        cerr << "[Клієнт " << client_id << "] socket не вдався" << endl;
        return;
    }

    {
        lock_guard<mutex> lock(cout_mutex);
        cout << "[Клієнт " << client_id << "] під'єднується до сервера " << server_ip << ":" << port << "..." << endl;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr(server_ip.c_str());
    serverAddr.sin_port = htons(port);
    if (connect(sock, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
        lock_guard<mutex> lock(cout_mutex);
        cerr << "[Клієнт " << client_id << "] не вдалося під'єднатися" << endl;
        closesocket(sock);
        return;
    }

    try {
        const int N = 10000;
        auto matrix = createRandomMatrix(N);

        // INIT
        uint16_t cmd = htons(CMD_INIT);
        sendAll(sock, reinterpret_cast<char*>(&cmd), sizeof(cmd));
        uint32_t netN = htonl(N);
        sendAll(sock, reinterpret_cast<char*>(&netN), sizeof(netN));
        for (int i = 0; i < N; ++i) {
            for (int j = 0; j < N; ++j) {
                uint32_t v = htonl(static_cast<uint32_t>(matrix[i][j]));
                sendAll(sock, reinterpret_cast<char*>(&v), sizeof(v));
            }
        }
        recvAll(sock, reinterpret_cast<char*>(&cmd), sizeof(cmd));
        if (ntohs(cmd) != RSP_INIT) throw runtime_error("INIT не вдався");

        {
            lock_guard<mutex> lock(cout_mutex);
            cout << "[Клієнт " << client_id << "] INIT підтверджено" << endl;
        }

        // START
        for (uint32_t T : {1u, 16u}) {
            cmd = htons(CMD_START);
            sendAll(sock, reinterpret_cast<char*>(&cmd), sizeof(cmd));
            uint32_t netT = htonl(T);
            sendAll(sock, reinterpret_cast<char*>(&netT), sizeof(netT));

            recvAll(sock, reinterpret_cast<char*>(&cmd), sizeof(cmd));
            if (ntohs(cmd) != RSP_START) throw runtime_error("START не вдався");
            double dur;
            recvAll(sock, reinterpret_cast<char*>(&dur), sizeof(dur));
            {
                lock_guard<mutex> lock(cout_mutex);
                cout << "[Клієнт " << client_id << "] потоки = " << T
                     << ", час виконання: " << dur << " сек" << endl;
            }
        }

        // STATUS
        cmd = htons(CMD_STATUS);
        sendAll(sock, reinterpret_cast<char*>(&cmd), sizeof(cmd));
        recvAll(sock, reinterpret_cast<char*>(&cmd), sizeof(cmd));
        if (ntohs(cmd) != RSP_STATUS) throw runtime_error("STATUS не вдався");

        {
            lock_guard<mutex> lock(cout_mutex);
            cout << "[Клієнт " << client_id << "] STATUS підтверджено сервером" << endl;
        }

    } catch (const exception& e) {
        lock_guard<mutex> lock(cout_mutex);
        cerr << "[Клієнт " << client_id << "] Помилка: " << e.what() << endl;
    }

    closesocket(sock);
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        cerr << "WSAStartup не вдався" << endl;
        return 1;
    }

    string server_ip = "127.0.0.1";
    int port = 1234;

    thread c1(run_client, 1, server_ip, port);
    thread c2(run_client, 2, server_ip, port);

    c1.join();
    c2.join();

    WSACleanup();
    return 0;
}