// In-memory filesystem with the subset of the Arduino fs::FS / File API used by config.cpp.
// It can simulate a power cut: hostFsCutAfterBytes(n) makes writes stop after n more bytes,
// leaving the file truncated exactly as a real power loss would.
#pragma once
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <Arduino.h>

#define FILE_READ "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a"

struct HostFsState
{
    std::map<std::string, std::shared_ptr<std::string>> files;
    long cutAfter = -1;    // bytes still allowed before the simulated power cut (-1 = no cut)
    long renamesLeft = -1; // renames still allowed before the power cut (-1 = no cut)
};
HostFsState &hostFs();
inline void hostFsReset() { hostFs() = HostFsState(); }
inline void hostFsCutAfterBytes(long n) { hostFs().cutAfter = n; }
inline void hostFsCutAfterRenames(long n) { hostFs().renamesLeft = n; }
inline void hostFsPowerOn() { hostFs().cutAfter = -1; hostFs().renamesLeft = -1; }

namespace fs
{
class File
{
public:
    File() {}
    File(std::shared_ptr<std::string> d, bool w) : data(d), writable(w) {}
    explicit operator bool() const { return (bool)data; }
    size_t write(uint8_t c) { return write(&c, 1); }
    size_t write(const uint8_t *buf, size_t n)
    {
        if (!data || !writable)
            return 0;
        HostFsState &fs = hostFs();
        if (fs.cutAfter >= 0)
        {
            if ((long)n > fs.cutAfter)
                n = (size_t)fs.cutAfter;
            fs.cutAfter -= (long)n;
        }
        data->append((const char *)buf, n);
        return n;
    }
    int read()
    {
        if (!data || pos >= data->size())
            return -1;
        return (uint8_t)(*data)[pos++];
    }
    size_t readBytes(char *buf, size_t n)
    {
        size_t k = 0;
        while (k < n && data && pos < data->size())
            buf[k++] = (*data)[pos++];
        return k;
    }
    int available() { return data ? (int)(data->size() - pos) : 0; }
    int peek() { return (data && pos < data->size()) ? (uint8_t)(*data)[pos] : -1; }
    size_t size() const { return data ? data->size() : 0; }
    void close() { data.reset(); }

private:
    std::shared_ptr<std::string> data;
    size_t pos = 0;
    bool writable = false;
};

class FS
{
public:
    File open(const char *path, const char *mode = FILE_READ)
    {
        HostFsState &fs = hostFs();
        if (fs.cutAfter == 0)
            return File(); // "power is off"
        if (std::strcmp(mode, FILE_WRITE) == 0)
        {
            auto d = std::make_shared<std::string>();
            fs.files[path] = d; // FILE_WRITE truncates, like LittleFS
            return File(d, true);
        }
        auto it = fs.files.find(path);
        if (it == fs.files.end())
            return File();
        return File(it->second, false);
    }
    File open(const String &p, const char *mode = FILE_READ) { return open(p.c_str(), mode); }
    bool exists(const char *path) { return hostFs().files.count(path) > 0; }
    bool exists(const String &p) { return exists(p.c_str()); }
    bool remove(const char *path)
    {
        if (hostFs().cutAfter == 0)
            return false;
        return hostFs().files.erase(path) > 0;
    }
    bool remove(const String &p) { return remove(p.c_str()); }
    bool rename(const char *from, const char *to)
    {
        HostFsState &fs = hostFs();
        if (fs.renamesLeft == 0)
            fs.cutAfter = 0; // power goes off now
        if (fs.cutAfter == 0)
            return false;
        if (fs.renamesLeft > 0)
            fs.renamesLeft--;
        auto it = fs.files.find(from);
        if (it == fs.files.end())
            return false;
        fs.files[to] = it->second;
        fs.files.erase(from);
        return true;
    }
    bool rename(const String &a, const String &b) { return rename(a.c_str(), b.c_str()); }
    bool rename(const char *a, const String &b) { return rename(a, b.c_str()); }
    bool rename(const String &a, const char *b) { return rename(a.c_str(), b); }
};
} // namespace fs
using fs::File;
