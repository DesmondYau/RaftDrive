#pragma once
#include <string>
#include <stdexcept>


struct WrongLeaderError : std::exception {
    const char* what() const noexcept override { return "ErrWrongLeader"; }
};


/**
 * @brief Abstract Class. The gRPC service layer only depends on this interface.
 */
class IKVBackend
{
public:
    virtual ~IKVBackend() = default;

    // Returns the stored value, or "" if the key does not exist.
    // Throws WrongLeaderError if this node is not the Raft leader.
    virtual std::string get(const std::string& key) = 0;

    // Overwrites the value at key.
    // Throws WrongLeaderError if this node is not the Raft leader.
    virtual void put(const std::string& key, const std::string& value) = 0;

    // Appends value to the existing value at key.
    // Throws WrongLeaderError if this node is not the Raft leader.
    virtual void append(const std::string& key, const std::string& value) = 0;

    virtual bool isLeader() const = 0;
    virtual bool isHealthy() const = 0;
    virtual void shutdown() = 0;
};
