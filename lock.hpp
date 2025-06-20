#ifndef LOCK_H
#define LOCK_H

#include <functional>

class ScopedLock {
public:
    ScopedLock(std::function<void()> lock, std::function<void()> unlock) 
        : unlock_(unlock) {
        lock();
    }
    ~ScopedLock() { unlock_(); }
private:
    std::function<void()> unlock_;
};

#endif // LOCK_H
