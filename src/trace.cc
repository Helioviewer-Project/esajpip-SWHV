#include "trace.h"

#include <sys/stat.h>
#include <sys/time.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <fcntl.h>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <utility>

using namespace std;

namespace {

#ifndef ESAJPIP_LOG_LIMIT
const size_t LOG_LIMIT = 1024ULL * 1024 * 1024;
#else
const size_t LOG_LIMIT = ESAJPIP_LOG_LIMIT;
#endif

#ifndef ESAJPIP_LOG_QUEUE_LIMIT
const size_t QUEUE_LIMIT = 64 * 1024;
#else
const size_t QUEUE_LIMIT = ESAJPIP_LOG_QUEUE_LIMIT;
#endif

const size_t MAX_MESSAGE = 1900;
struct Record {
    int64_t seconds;
    int32_t microseconds;
    bool truncated;
    unsigned long dropped;
    string message;

    size_t Size() const {
        return sizeof(Record) + message.size();
    }
};

mutex queue_mutex;
condition_variable queue_ready;
condition_variable queue_drained;
deque<Record> records;
size_t queued_size = 0;
bool writing = false;
bool stopping = false;
thread logger;

atomic<bool> accepting(false);
atomic<bool> output_enabled(false);
atomic<unsigned long> dropped(0);

int output_fd = STDOUT_FILENO;
bool file_output = false;
size_t output_size = 0;
string output_name;

bool WriteAll(int fd, const char *data, size_t length) {
    while (length > 0) {
        ssize_t written = write(fd, data, length);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            return false;
        data += written;
        length -= written;
    }
    return true;
}

bool DisableOutput() {
    if (file_output && output_fd >= 0)
        close(output_fd);
    output_fd = -1;
    file_output = false;
    output_enabled.store(false, memory_order_release);
    return false;
}

bool Rotate() {
    string backup = output_name + ".1";
    if (unlink(backup.c_str()) != 0 && errno != ENOENT)
        return DisableOutput();
    if (rename(output_name.c_str(), backup.c_str()) != 0)
        return DisableOutput();

    int replacement_fd = open(output_name.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (replacement_fd < 0)
        return DisableOutput();

    close(output_fd);
    output_fd = replacement_fd;
    output_size = 0;
    return true;
}

bool WriteLine(const Record &record, const string &message, bool truncated) {
    time_t seconds = record.seconds;
    tm local;
    char timestamp[25];
    if (!localtime_r(&seconds, &local) ||
        !strftime(timestamp, sizeof timestamp, "%Y-%m-%d %H:%M:%S", &local))
        return DisableOutput();

    char prefix[32];
    int prefix_length = snprintf(prefix, sizeof prefix, "%s,%03d: ", timestamp,
                                 record.microseconds / 1000);
    if (prefix_length < 0 || static_cast<size_t>(prefix_length) >= sizeof prefix)
        return DisableOutput();

    string line(prefix, prefix_length);
    line += message;
    if (truncated)
        line += "...";
    line += '\n';
    if (!WriteAll(output_fd, line.data(), line.size()))
        return DisableOutput();

    if (file_output) {
        output_size += line.size();
        if (output_size >= LOG_LIMIT)
            return Rotate();
    }
    return true;
}

void WriteRecord(const Record &record) {
    if (!output_enabled.load(memory_order_acquire))
        return;
    if (record.dropped != 0) {
        string summary = to_string(record.dropped) + " log message" +
                (record.dropped == 1 ? "" : "s") + " dropped";
        if (!WriteLine(record, summary, false))
            return;
    }
    WriteLine(record, record.message, record.truncated);
}

void RunLogger() {
    for (;;) {
        unique_lock<mutex> lock(queue_mutex);
        queue_ready.wait(lock, [] { return stopping || !records.empty(); });
        if (records.empty())
            break;

        Record record = std::move(records.front());
        queued_size -= record.Size();
        records.pop_front();
        writing = true;
        lock.unlock();

        WriteRecord(record);

        lock.lock();
        writing = false;
        if (records.empty())
            queue_drained.notify_all();
    }
}

void CloseOutput() {
    if (file_output && output_fd >= 0)
        close(output_fd);
    output_fd = STDOUT_FILENO;
    file_output = false;
    output_size = 0;
    output_name.clear();
    output_enabled.store(false, memory_order_release);
}

}

namespace trace {

bool Initialize(const string &file_name) {
    if (logger.joinable())
        return false;

    if (!file_name.empty()) {
        char timestamp[20];
        time_t now = time(NULL);
        tm local;
        if (!localtime_r(&now, &local) ||
            !strftime(timestamp, sizeof timestamp, "%Y%m%d.%H%M%S", &local))
            return false;

        output_name = file_name + "." + timestamp + ".log";
        output_fd = open(output_name.c_str(), O_CREAT | O_APPEND | O_WRONLY, 0644);
        if (output_fd < 0)
            return false;

        struct stat status;
        if (fstat(output_fd, &status) != 0) {
            close(output_fd);
            output_fd = STDOUT_FILENO;
            output_name.clear();
            return false;
        }
        output_size = status.st_size;
        file_output = true;
    }

    {
        lock_guard<mutex> lock(queue_mutex);
        records.clear();
        queued_size = 0;
        writing = false;
        stopping = false;
    }
    dropped.store(0, memory_order_relaxed);
    output_enabled.store(true, memory_order_release);
    try {
        logger = thread(RunLogger);
    } catch (const system_error &) {
        CloseOutput();
        return false;
    }
    accepting.store(true, memory_order_release);
    return true;
}

void Write(const string &message) {
    if (!Enabled())
        return;

    unique_lock<mutex> lock(queue_mutex);
    if (!accepting.load(memory_order_relaxed) ||
        !output_enabled.load(memory_order_relaxed))
        return;

    timeval now;
    gettimeofday(&now, NULL);
    Record record;
    record.seconds = now.tv_sec;
    record.microseconds = static_cast<int32_t>(now.tv_usec);
    record.truncated = message.size() > MAX_MESSAGE;
    record.message.assign(message, 0, min(message.size(), MAX_MESSAGE));

    record.dropped = dropped.exchange(0, memory_order_relaxed);
    if (record.Size() > QUEUE_LIMIT ||
        queued_size > QUEUE_LIMIT - record.Size()) {
        dropped.fetch_add(record.dropped + 1, memory_order_relaxed);
        return;
    }
    queued_size += record.Size();
    records.push_back(std::move(record));
    lock.unlock();
    queue_ready.notify_one();
}

bool Enabled() {
    return accepting.load(memory_order_acquire) &&
           output_enabled.load(memory_order_acquire);
}

void Flush() {
    unique_lock<mutex> lock(queue_mutex);
    queue_drained.wait(lock, [] { return records.empty() && !writing; });
}

void Drain() {
    accepting.store(false, memory_order_release);
    {
        lock_guard<mutex> lock(queue_mutex);
        stopping = true;
    }
    queue_ready.notify_one();
    if (logger.joinable())
        logger.join();
    CloseOutput();
}

}
