#include <iostream>
#include <windows.h>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <ctime>

using namespace std;
using namespace chrono;

vector<int> generate_data(int size) {
    vector<int> data;
    for (int i = 0; i < size; ++i) {
        data.push_back(rand() % 1001);
    }
    return data;
}

int sequential(const vector<int>& data) {
    int result = 0;
    for (int val : data) {
        if (val % 7 == 0) {
            result ^= val;
        }
    }
    return result;
}

void process_mutex(const vector<int>& data, int start, int end, int& result, mutex& mtx) {
    for (int i = start; i < end; ++i) {
        if (data[i] % 7 == 0) {
            lock_guard<mutex> lock(mtx);
            result ^= data[i];
        }
    }
}

int parallel_mutex(const vector<int>& data) {
    int result = 0;
    mutex mtx;
    int mid = data.size() / 2;

    thread t1(process_mutex, cref(data), 0, mid, ref(result), ref(mtx));
    thread t2(process_mutex, cref(data), mid, data.size(), ref(result), ref(mtx));
    t1.join(); t2.join();

    return result;
}

void process_atomic(const vector<int>& data, int start, int end, atomic<int>& result) {
    for (int i = start; i < end; ++i) {
        if (data[i] % 7 == 0) {
            int current = result.load(memory_order_relaxed);
            while (!result.compare_exchange_weak(current, current ^ data[i], memory_order_relaxed));
        }
    }
}

int parallel_atomic(const vector<int>& data) {
    atomic<int> result(0);
    int mid = data.size() / 2;

    thread t1(process_atomic, cref(data), 0, mid, ref(result));
    thread t2(process_atomic, cref(data), mid, data.size(), ref(result));
    t1.join(); t2.join();

    return result.load();
}

void test_size(int size) {
    cout << "\nРозмір масиву: " << size << " елементів\n";
    vector<int> data = generate_data(size);

    auto start = high_resolution_clock::now();
    int res1 = sequential(data);
    auto end = high_resolution_clock::now();
    cout << "Послідовно:\tXOR = " << res1
         << ", час = " << duration<double>(end - start).count() << " с\n";

    start = high_resolution_clock::now();
    int res2 = parallel_mutex(data);
    end = high_resolution_clock::now();
    cout << "З м'ютексом:\tXOR = " << res2
         << ", час = " << duration<double>(end - start).count() << " с\n";

    start = high_resolution_clock::now();
    int res3 = parallel_atomic(data);
    end = high_resolution_clock::now();
    cout << "З CAS:\t\tXOR = " << res3
         << ", час = " << duration<double>(end - start).count() << " с\n";
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    srand(time(0));

    vector<int> sizes = {10000, 100000, 1000000, 10000000, 100000000};
    for (int i = 0; i < sizes.size(); ++i) {
        test_size(sizes[i]);
    }

    return 0;
}