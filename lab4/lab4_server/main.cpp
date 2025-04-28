#include <winsock2.h>
#include <windows.h>
#include <iostream>
#include <vector>
#include <thread>
#include <stdexcept>
#include <chrono>
#include <mutex>

using namespace std;

constexpr uint16_t CMD_INIT   = 0x01;
constexpr uint16_t CMD_START  = 0x02;
constexpr uint16_t CMD_STATUS = 0x03;
constexpr uint16_t RSP_INIT   = 0x11;
constexpr uint16_t RSP_START  = 0x12;
constexpr uint16_t RSP_STATUS = 0x13;

mutex cout_mutex;

void printError(const char* msg) {
    lock_guard<mutex> lock(cout_mutex);
    cerr << msg << " не вдався через помилку: " << WSAGetLastError() << endl;
}

void recvAll(SOCKET sock, char* buf, int len) {
    int received = 0;
    while (received < len) {
        int r = recv(sock, buf + received, len - received, 0);
        if (r < 0) throw runtime_error("recv не вдався");
        if (r == 0) throw runtime_error("З'єднання роз'єднано клієнтом");
        received += r;
    }
}

void sendAll(SOCKET sock, const char* buf, int len) {
    int sent = 0;
    while (sent < len) {
        int s = send(sock, buf + sent, len - sent, 0);
        if (s == SOCKET_ERROR) throw runtime_error("відправка не вдалася");
        sent += s;
    }
}

void parallelColumnMax(const vector<vector<uint32_t>>& mat, uint32_t N, vector<uint32_t>& result, uint32_t start, uint32_t end) {
    for (uint32_t j = start; j < end; ++j) {
        uint32_t mx = mat[0][j];
        for (uint32_t i = 1; i < N; ++i) {
            if (mat[i][j] > mx) mx = mat[i][j];
        }
        result[j] = mx;
    }
}

void computeColumnMaxParallel(const vector<vector<uint32_t>>& mat, uint32_t N, vector<uint32_t>& result, uint32_t T) {
    result.assign(N, 0);
    vector<thread> threads;
    threads.reserve(T);

    uint32_t base = N / T;
    uint32_t rem  = N % T;
    uint32_t start = 0;
    for (uint32_t t = 0; t < T; ++t) {
        uint32_t extra = (t < rem) ? 1 : 0;
        uint32_t end   = start + base + extra;
        threads.emplace_back(parallelColumnMax, cref(mat), N, ref(result), start, end);
        start = end;
    }
    for (auto &th : threads) th.join();
}

void handleClient(SOCKET client) {
    {
        lock_guard<mutex> lock(cout_mutex);
        cout << "[Сервер] Клієнт під'єднався: " << client << endl;
    }
    try {
        uint16_t cmd;
        uint32_t N = 0;
        vector<vector<uint32_t>> mat;
        vector<uint32_t> result;
        bool dataReady = false;

        while (true) {
            recvAll(client, reinterpret_cast<char*>(&cmd), sizeof(cmd));
            cmd = ntohs(cmd);
            if (cmd == CMD_INIT) {
                recvAll(client, reinterpret_cast<char*>(&N), sizeof(N));
                N = ntohl(N);
                mat.assign(N, vector<uint32_t>(N));
                for (uint32_t i = 0; i < N; ++i) {
                    for (uint32_t j = 0; j < N; ++j) {
                        uint32_t v;
                        recvAll(client, reinterpret_cast<char*>(&v), sizeof(v));
                        mat[i][j] = ntohl(v);
                    }
                }
                dataReady = true;
                {
                    lock_guard<mutex> lock(cout_mutex);
                    cout << "[Сервер] INIT отримано: N = " << N << endl;
                }
                uint16_t rsp = htons(RSP_INIT);
                sendAll(client, reinterpret_cast<char*>(&rsp), sizeof(rsp));

            } else if (cmd == CMD_START) {
                if (!dataReady) throw runtime_error("Дані не ініціалізовано");
                uint32_t netT;
                recvAll(client, reinterpret_cast<char*>(&netT), sizeof(netT));
                uint32_t T = ntohl(netT);
                {
                    lock_guard<mutex> lock(cout_mutex);
                    cout << "[Сервер] START отрмано: потоки = " << T << endl;
                }

                auto t1 = chrono::high_resolution_clock::now();
                computeColumnMaxParallel(mat, N, result, T);
                for (uint32_t i = 0; i < N; ++i) {
                    mat[i][i] = result[i];
                }
                auto t2 = chrono::high_resolution_clock::now();
                double dur = chrono::duration<double>(t2 - t1).count();

                uint16_t rsp = htons(RSP_START);
                sendAll(client, reinterpret_cast<char*>(&rsp), sizeof(rsp));
                sendAll(client, reinterpret_cast<char*>(&dur), sizeof(dur));

            } else if (cmd == CMD_STATUS) {
                if (!dataReady) throw runtime_error("Дані не ініціалізовано");
                {
                    lock_guard<mutex> lock(cout_mutex);
                    cout << "[Сервер] запит STATUS, відправка результатів..." << endl;
                }
                uint16_t rsp = htons(RSP_STATUS);
                sendAll(client, reinterpret_cast<char*>(&rsp), sizeof(rsp));
                break;

            } else {
                lock_guard<mutex> lock(cout_mutex);
                cerr << "[Сервер] Незрозуміла команда: " << cmd << endl;
                break;
            }
        }
    } catch (const exception& e) {
        lock_guard<mutex> lock(cout_mutex);
        cerr << "[Сервер] Помилка клієнта " << client << ": " << e.what() << endl;
    }
    closesocket(client);
    {
        lock_guard<mutex> lock(cout_mutex);
        cout << "[Сервер] Клієнт " << client << " від'єднався" << endl;
    }
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        printError("WSAStartup");
        return 1;
    }

    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock == INVALID_SOCKET) {
        printError("socket");
        WSACleanup();
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(1234);
    if (bind(listenSock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR ||
        listen(listenSock, SOMAXCONN) == SOCKET_ERROR) {
        printError("bind/listen");
        closesocket(listenSock);
        WSACleanup();
        return 1;
    }

    {
        lock_guard<mutex> lock(cout_mutex);
        cout << "[Сервер] Працює на порті 1234..." << endl;
    }

    while (true) {
        SOCKET client = accept(listenSock, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            printError("accept");
            continue;
        }
        thread(handleClient, client).detach();
    }

    closesocket(listenSock);
    WSACleanup();
    return 0;
}