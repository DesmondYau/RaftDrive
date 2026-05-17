#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <vector>
#include <string>
#include <atomic>
#include <cstdint>

// Extracted from Lab3 config.hpp / Lab4 kvserver.hpp so both raft.hpp and
// kvserver.hpp can share a single definition.

struct ApplyMsg 
{
    bool        CommandValid;
    uint64_t    CommandIndex;
    uint32_t    CommandTerm;
    std::string Command;

    bool                 SnapshotValid;
    std::vector<uint8_t> Snapshot;
    uint64_t             LastIncludedIndex;
    uint32_t             lastIncludedTerm;
};

class ApplyChannel 
{
public:
    void push(const ApplyMsg& msg) 
    {
        std::lock_guard<std::mutex> lk(m_mu);
        m_q.push(msg);
        m_cv.notify_one();
    }

    std::optional<ApplyMsg> pop() 
    {
        std::unique_lock<std::mutex> lk(m_mu);
        m_cv.wait(lk, [this]{ 
            return !m_q.empty() || m_closed; 
        });

        if (m_q.empty() && m_closed) 
            return std::nullopt;
        
        auto msg = m_q.front();
        m_q.pop();
        return msg;
    }

    void close() 
    {
        std::lock_guard<std::mutex> lk(m_mu);
        m_closed = true;
        m_cv.notify_all();
    }

private:
    std::queue<ApplyMsg>    m_q;
    std::mutex              m_mu;
    std::condition_variable m_cv;
    bool                    m_closed{false};
};
