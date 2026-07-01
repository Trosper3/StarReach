#pragma once
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

/// Global signal hub. Neither emitter nor subscriber needs a reference to the other.
/// Template overloads carry typed data; void overload fires with no payload.
class EventBus {
public:
    static EventBus& Get() {
        static EventBus instance;
        return instance;
    }

    template<typename T>
    void Subscribe(const std::string& event, std::function<void(const T&)> cb) {
        _listeners[event].push_back([cb](const void* data) {
            cb(*static_cast<const T*>(data));
        });
    }

    void Subscribe(const std::string& event, std::function<void()> cb) {
        _listeners[event].push_back([cb](const void*) { cb(); });
    }

    template<typename T>
    void Emit(const std::string& event, const T& data) {
        auto it = _listeners.find(event);
        if (it != _listeners.end())
            for (auto& cb : it->second) cb(&data);
    }

    void Emit(const std::string& event) {
        auto it = _listeners.find(event);
        if (it != _listeners.end())
            for (auto& cb : it->second) cb(nullptr);
    }

private:
    EventBus() = default;
    std::unordered_map<std::string, std::vector<std::function<void(const void*)>>> _listeners;
};
