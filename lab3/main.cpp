#include <iostream>
#include <queue>
#include <thread>
#include <vector>
#include <shared_mutex>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <chrono>
#include <atomic>
#include <random>
#include <windows.h>

using namespace std;
using Clock = chrono::steady_clock;
using read_write_lock = shared_mutex;
using read_lock = shared_lock<read_write_lock>;
using write_lock = unique_lock<read_write_lock>;

template<typename F>
class task_queue {
    queue<F> tasks;
    mutable read_write_lock rw;
public:
    bool empty() const {
        read_lock lk(rw);
        return tasks.empty();
    }

    size_t size() const {
        read_lock lk(rw);
        return tasks.size();
    }

    void clear() {
        write_lock lk(rw);
        while (!tasks.empty()) tasks.pop();
    }

    bool pop(F &f) {
        write_lock lk(rw);
        if (tasks.empty()) return false;
        f = move(tasks.front()); tasks.pop();
        return true;
    }

    bool push(F&& f, size_t cap) {
        write_lock lk(rw);
        if (tasks.size() >= cap) return false;
        tasks.push(move(f));
        return true;
    }

    F top() const {
        read_lock lk(rw);
        if (tasks.empty()) throw std::runtime_error("Черга пуста");
        return tasks.top();
    }
};

class thread_pool {
public:
    thread_pool(size_t workers = 6, size_t capacity = 15): WORKERS(workers), CAP(capacity) {
        for(size_t i = 0; i < WORKERS; ++i)
            workers_vec.emplace_back(&thread_pool::worker, this);
        created = WORKERS;
    }
    ~thread_pool() { shutdown(); }

    void addTask(function<void()> f) {
        attempted++;

        if (!q.push(move(f), CAP)) {
            rejected++;
            return;
        }

        accepted++;

        auto now = Clock::now();
        if (q.size() == CAP) {
            write_lock lg(metrics_mtx);
            if (!is_full) {
                is_full = true;
                full_start = now;
            }
        }

        cv.notify_all();
    }

    void shutdown() {
        stop = true;
        cv.notify_all();
        for(auto &t : workers_vec) {
            if (t.joinable()) t.join();
        }
    }

    void show_metrics() const {
        double avgWait = waitCycles ? (totalWaitNs / static_cast<double>(waitCycles) / 1e9) : 0;

        vector<double> durations_copy;
        {
            read_lock lg(metrics_mtx);
            durations_copy = full_durations;
        }

        double minFull = 0, maxFull = 0;
        if (!durations_copy.empty()) {
            minFull = *min_element(durations_copy.begin(), durations_copy.end());
            maxFull = *max_element(durations_copy.begin(), durations_copy.end());
        }

        cout << "Кількість робочих потоків: " << created << "\n";
        cout << "Спроб додати задач: " << attempted << "\n";
        cout << "Завершено задач: " << completed << "\n";
        cout << "Відкинуто задач: " << rejected << "\n";
        cout << "Середній час простою потоків (с): " << avgWait << "\n";
        cout << "Найкоротший час, коли черга була повністю заповнена (с): " << minFull << "\n";
        cout << "Найдовший час, коли черга була повністю заповнена (с): "   << maxFull << "\n";
    }

private:
    void worker() {
        while (true) {
            function<void()> task;
            {
                unique_lock<mutex> lk(cv_m);
                auto start_wait = Clock::now();
                cv.wait(lk, [&]{ return stop || !q.empty(); });
                auto end_wait = Clock::now();
                totalWaitNs += chrono::duration_cast<chrono::nanoseconds>(end_wait - start_wait).count();
                waitCycles++;
            }
            if (stop && q.empty()) break;

            if (q.pop(task)) {
                bool record = false;
                Clock::time_point start_tp;
                {
                    write_lock lg(metrics_mtx);
                    if (is_full) {
                        is_full = false;
                        start_tp = full_start;
                        record = true;
                    }
                }
                if (record) {
                    auto now2 = Clock::now();
                    double dt = chrono::duration<double>(now2 - start_tp).count();
                    write_lock lg(metrics_mtx);
                    full_durations.push_back(dt);
                }

                completed++;
                task();
            }
        }
    }

    const size_t WORKERS;
    const size_t CAP;
    task_queue<function<void()>> q;
    vector<thread> workers_vec;

    mutex cv_m;
    condition_variable_any cv;
    atomic<bool> stop{false};

    size_t created{0};
    atomic<size_t> attempted{0}, accepted{0}, completed{0}, rejected{0};

    mutable read_write_lock metrics_mtx;
    bool is_full{false};
    Clock::time_point full_start;
    vector<double> full_durations;

    atomic<uint64_t> totalWaitNs{0}, waitCycles{0};
};

int main() {
    SetConsoleOutputCP(CP_UTF8);

    const int PRODUCERS = 5;
    const int TASKS_PER_PRODUCER = 10;
    mutex cout_mtx;

    thread_pool pool(6, 15);

    vector<thread> producers;
    for(int p = 0; p < PRODUCERS; ++p) {
        producers.emplace_back([&, p](){
            mt19937 rng(random_device{}());
            uniform_int_distribution<int> dist(5, 10);
            for(int i = 0; i < TASKS_PER_PRODUCER; ++i) {
                int id = p * TASKS_PER_PRODUCER + i + 1;
                int dur = dist(rng);
                pool.addTask([id, dur, &cout_mtx](){
                    { lock_guard<mutex> lg(cout_mtx);
                      cout << "Завдання #" << id << " виконується " << dur << " сек\n"; }
                    this_thread::sleep_for(chrono::seconds(dur));
                    { lock_guard<mutex> lg(cout_mtx);
                      cout << "Завдання #" << id << " завершено\n"; }
                });
                this_thread::sleep_for(chrono::milliseconds(500));
            }
        });
    }
    for(auto &t : producers) t.join();

    pool.shutdown();
    pool.show_metrics();
    return 0;
}