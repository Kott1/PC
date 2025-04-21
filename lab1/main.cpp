#include <windows.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <iomanip>

using namespace std;

void printCPUInfo() {
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);

    cout << "=== Інформація про процесор ===\n";

    cout << "Архітектура процесора: ";
    switch (sysInfo.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64:
            cout << "x64 (AMD або Intel)\n";
            break;
        case PROCESSOR_ARCHITECTURE_INTEL:
            cout << "x86\n";
            break;
        case PROCESSOR_ARCHITECTURE_ARM:
            cout << "ARM\n";
            break;
        case PROCESSOR_ARCHITECTURE_ARM64:
            cout << "ARM64\n";
            break;
        default:
            cout << "Невідома архітектура\n";
            break;
    }

    cout << "Логічних процесорів: " << sysInfo.dwNumberOfProcessors << "\n";
    cout << "Розмір сторінки пам'яті: " << sysInfo.dwPageSize << " байт\n";
    cout << "Мінімальна адреса додатку: " << sysInfo.lpMinimumApplicationAddress << "\n";
    cout << "Максимальна адреса додатку: " << sysInfo.lpMaximumApplicationAddress << "\n";
    cout << "\n";
}

void printMemoryInfo() {
    MEMORYSTATUSEX statex;
    statex.dwLength = sizeof(statex);

    cout << "=== Інформація про пам'ять ===\n";
    if (GlobalMemoryStatusEx(&statex)) {
        double totalGB = statex.ullTotalPhys / (1024.0 * 1024 * 1024);
        double availGB = statex.ullAvailPhys / (1024.0 * 1024 * 1024);
        cout << "Загальна фізична пам'ять (RAM): " << fixed << setprecision(2)
                  << totalGB << " GB\n";
        cout << "Доступна фізична пам'ять (RAM): " << fixed << setprecision(2)
                  << availGB << " GB\n";
    } else {
        cerr << "Помилка отримання інформації про пам'ять.\n";
    }
    cout << "\n";
}

vector<vector<int>> createRandomMatrix(int n) {
    vector<vector<int>> mat(n, vector<int>(n));
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            mat[i][j] = rand() % 1001;
        }
    }
    return mat;
}

void nonParallelSolution(vector<vector<int>>& mat) {
    int n = static_cast<int>(mat.size());
    for (int j = 0; j < n; j++) {
        int maxVal = mat[0][j];
        for (int i = 1; i < n; i++) {
            if (mat[i][j] > maxVal)
                maxVal = mat[i][j];
        }
        mat[j][j] = maxVal;
    }
}

void parallelColumnMax(vector<vector<int>>& mat, int start, int end) {
    int n = static_cast<int>(mat.size());
    for (int j = start; j < end; j++) {
        int maxVal = mat[0][j];
        for (int i = 1; i < n; i++) {
            if (mat[i][j] > maxVal)
                maxVal = mat[i][j];
        }
        mat[j][j] = maxVal;
    }
}

void parallelSolution(vector<vector<int>>& mat, int numThreads) {
    int n = static_cast<int>(mat.size());
    vector<thread> threads;
    int columnsPerThread = n / numThreads;
    int remainder = n % numThreads;
    int start = 0;
    for (int t = 0; t < numThreads; t++) {
        int extra = (t < remainder) ? 1 : 0;
        int end = start + columnsPerThread + extra;
        threads.push_back(thread(parallelColumnMax, ref(mat), start, end));
        start = end;
    }
    for (auto& th : threads) {
        th.join();
    }
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    printCPUInfo();
    printMemoryInfo();

    srand(GetTickCount());

    vector<int> matrixSizes = {100, 1000, 10000, 50000};
    vector<int> threadCounts = {4, 8, 16, 32, 64, 128, 256};

    for (int n : matrixSizes) {
        cout << "\n=== Розмір матриці: " << n << " x " << n << " ===\n";

        vector<vector<int>> mat = createRandomMatrix(n);

        auto startTime = chrono::high_resolution_clock::now();
        nonParallelSolution(mat);
        auto endTime = chrono::high_resolution_clock::now();
        chrono::duration<double> duration = endTime - startTime;
        cout << "Послідовний час виконання: " << fixed << setprecision(6) << duration.count() << " секунд.\n";

        for (int threads : threadCounts) {
            vector<vector<int>> matParallel = mat;

            auto startTime = chrono::high_resolution_clock::now();
            parallelSolution(matParallel, threads);
            auto endTime = chrono::high_resolution_clock::now();

            chrono::duration<double> duration = endTime - startTime;
            cout << "Паралельний час (потоків " << threads << "): " << fixed << setprecision(6) << duration.count() << " секунд.\n";
        }
    }

    return 0;
}