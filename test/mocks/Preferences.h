// Preferences (NVS) finto: un dizionario in memoria più il log delle scritture.
#ifndef FAKE_PREFERENCES_H
#define FAKE_PREFERENCES_H

#include <map>
#include <string>
#include <vector>
#include <stdint.h>

struct FakeNvs {
    std::map<std::string, uint64_t> values;
    std::vector<std::string> writes; // "chiave=valore" in ordine
};
extern FakeNvs* fake_nvs; // NVS attiva (i test possono scambiarla)

class Preferences {
    std::string ns_;
    std::string k(const char* key) const { return ns_ + "/" + key; }
    uint64_t get(const char* key, uint64_t def) const {
        auto it = fake_nvs->values.find(k(key));
        return it == fake_nvs->values.end() ? def : it->second;
    }
    size_t put(const char* key, uint64_t v) {
        fake_nvs->values[k(key)] = v;
        fake_nvs->writes.push_back(k(key) + "=" + std::to_string(v));
        return 1;
    }
public:
    bool begin(const char* name, bool) { ns_ = name; return true; }
    void end() {}
    uint8_t getUChar(const char* key, uint8_t def) { return (uint8_t)get(key, def); }
    uint16_t getUShort(const char* key, uint16_t def) { return (uint16_t)get(key, def); }
    uint32_t getULong(const char* key, uint32_t def) { return (uint32_t)get(key, def); }
    bool getBool(const char* key, bool def) { return get(key, def) != 0; }
    size_t putUChar(const char* key, uint8_t v) { return put(key, v); }
    size_t putUShort(const char* key, uint16_t v) { return put(key, v); }
    size_t putULong(const char* key, uint32_t v) { return put(key, v); }
    size_t putBool(const char* key, bool v) { return put(key, v ? 1 : 0); }
};

#endif
