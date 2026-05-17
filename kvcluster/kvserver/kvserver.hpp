#pragma once

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <thread>
#include <future>
#include <memory>

#include "raft/raft.hpp"
#include "raft/logger.hpp"
#include "raft/persister.hpp"
#include "raft/apply_channel.hpp"
#include "transport/client_abstract.hpp"
#include "models.hpp"


class KVServer : public std::enable_shared_from_this<KVServer>
{
public:
    KVServer(int id, int maxRaftState, std::shared_ptr<Persister> persister,
             std::vector<std::shared_ptr<IRaftTransport>> transports);
    ~KVServer();

    void get(const models::GetArgs& args, models::GetReply& reply);
    void putAppend(const models::PutAppendArgs& args, models::PutAppendReply& reply);
    void kill();
    void applierLoop();
    Raft* getRaft();

private:
    int m_id;
    int m_maxRaftState {0};
    std::atomic<bool> m_dead {false};
    std::unique_ptr<Raft> m_raft;
    std::shared_ptr<Logger> m_logger;
    std::shared_ptr<ApplyChannel> m_applyChannel;
    std::unordered_map<std::string, std::string> m_kvStore;
    std::map<uint64_t, int> m_clientSeqMap;
    std::map<uint64_t, std::promise<models::Op>> m_waitMap;
    std::thread m_applierThread;
    std::mutex m_mu;
};
