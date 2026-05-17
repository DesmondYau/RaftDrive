#pragma once
#include <memory>
#include <random>
#include <atomic>
#include "backend_abstract.hpp"
#include "kvserver/kvserver.hpp"
#include "kvserver/models.hpp"

class DistributedRaftBackend : public IKVBackend {
public:
    explicit DistributedRaftBackend(std::shared_ptr<KVServer> kvServer)
        : m_kvServer(std::move(kvServer))
    {
        std::mt19937_64 eng(std::random_device{}());
        m_clientId = std::uniform_int_distribution<uint64_t>{}(eng);
    }

    std::string get(const std::string& key) override
    {
        models::GetArgs args;
        args.m_key      = key;
        args.m_clientID = m_clientId;
        args.m_seq      = ++m_seq;

        models::GetReply reply;
        m_kvServer->get(args, reply);

        if (reply.m_err == "ErrWrongLeader")
            throw WrongLeaderError{};

        if (reply.m_err == "ErrNoKey")
            return "";

        return reply.m_value;
    }

    void put(const std::string& key, const std::string& value) override
    {
        models::PutAppendArgs args;
        args.m_key       = key;
        args.m_value     = value;
        args.m_operation = "Put";
        args.m_clientID  = m_clientId;
        args.m_seq       = ++m_seq;

        models::PutAppendReply reply;
        m_kvServer->putAppend(args, reply);

        if (reply.m_err == "ErrWrongLeader")
            throw WrongLeaderError{};
    }

    void append(const std::string& key, const std::string& value) override
    {
        models::PutAppendArgs args;
        args.m_key       = key;
        args.m_value     = value;
        args.m_operation = "Append";
        args.m_clientID  = m_clientId;
        args.m_seq       = ++m_seq;

        models::PutAppendReply reply;
        m_kvServer->putAppend(args, reply);

        if (reply.m_err == "ErrWrongLeader")
            throw WrongLeaderError{};
    }

    bool isLeader() const override
    {
        return m_kvServer->getRaft()->isLeader();
    }

    bool isHealthy() const override
    {
        return !m_dead;
    }

    void shutdown() override
    {
        m_dead = true;
        m_kvServer->kill();
    }

private:
    std::shared_ptr<KVServer> m_kvServer;
    uint64_t                  m_clientId;
    std::atomic<int>          m_seq{0};
    std::atomic<bool>         m_dead{false};
};
