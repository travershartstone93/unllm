// Two hand-wired diagnostic callers. The catalog candidate is keyed_latch.hpp alone.
#include "keyed_latch.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <json-c/json.h>

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    std::ifstream source(argv[1]);
    if (!source) return 1;
    unllm::KeyedLatch latch;
    json_object* result = json_object_new_array();
    std::string line;
#ifdef SENSORS
    int row = 0;
#endif
    while (std::getline(source, line)) {
        std::istringstream fields(line);
        std::string key;
        int value;
        if (!std::getline(fields, key, '\t') || !(fields >> value)) {
            json_object_put(result);
            return 1;
        }
        unllm::LatchEvent event = unllm::LatchEvent::hold;
#if defined(INVENTORY)
        if (value <= 2) event = unllm::LatchEvent::trigger;
        if (value >= 5) event = unllm::LatchEvent::rearm;
        if (latch.observe(key, event)) json_object_array_add(result, json_object_new_string(key.c_str()));
#elif defined(SENSORS)
        ++row;
        if (value >= 30) event = unllm::LatchEvent::trigger;
        if (value <= 25) event = unllm::LatchEvent::rearm;
        if (latch.observe(key, event)) json_object_array_add(result, json_object_new_int(row));
#else
#error "Select a diagnostic caller"
#endif
    }
    std::ofstream dest(argv[2]);
    dest << json_object_to_json_string(result);
    dest.close();
    json_object_put(result);
    return dest ? 0 : 1;
}
