#pragma once
#include <iostream>
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <thread>

#include "raft_types.hpp"
#include "apply_channel.hpp"
#include "threadpool.hpp"
#include "transport/client_abstract.hpp"

class Persister;
class Logger;

class Raft
{
public:
    enum class State { LEADER, CANDIDATE, FOLLOWER };

    // peers: one transport per peer (indexed 0..n-1, skip own id)
    Raft(std::vector<std::shared_ptr<IRaftTransport>> peers, int32_t id,
         std::shared_ptr<Persister> persister,
         std::shared_ptr<ApplyChannel> applyChannel,
         std::shared_ptr<Logger> logger);
    ~Raft();

    void startRaft();
    void appendEntries(const AppendEntriesArgs& args, AppendEntriesReply& reply);
    void requestVote(const RequestVoteArgs& args, RequestVoteReply& reply);
    void installSnapshot(const InstallSnapshotArgs& args, InstallSnapshotReply& reply);
    void broadcastAppendEntries();
    void startElection();
    bool isLeader();
    void snapshot(uint64_t lastIncludedIndex, const std::string& snapshot);
    void kill();

    std::tuple<int, int, bool> start(const std::string& command);
    std::pair<uint32_t, State> getTermState();

private:
    int helperGenerateTimeout();
    void helperUpdateLeaderCommitIndex();
    void helperPromoteToLeader();
    void helperPromoteToCandidate();
    void helperStepDownToFollower(uint32_t term);
    void helperPersist();
    void helperReadPersist();
    bool helperNeedsSnapshot(size_t peerId);
    void helperTriggerInstallSnapshot(size_t id);
    uint64_t helperGetRelativeIndex(uint64_t absoluteIndex) const;
    std::pair<AppendEntriesArgs, uint64_t> helperBuildAppendEntriesArgs(size_t peerId);

    bool sendRequestVoteRPC(int32_t id, const RequestVoteArgs& args, RequestVoteReply& reply);
    bool sendAppendEntriesRPC(int32_t id, const AppendEntriesArgs& args, AppendEntriesReply& reply);
    bool sendInstallSnapshotRPC(int32_t id, const InstallSnapshotArgs& args, InstallSnapshotReply& reply);

    int32_t  m_id;
    int32_t  m_votedFor      { -1 };
    int32_t  m_votesGranted  { 0 };
    uint32_t m_currentTerm   { 0 };
    uint64_t m_commitIndex   { 0 };
    uint64_t m_lastApplied   { 0 };
    uint64_t m_lastIncludedIndex { 0 };
    uint32_t m_lastIncludedTerm  { 0 };
    State    m_state         { State::FOLLOWER };
    std::vector<std::shared_ptr<IRaftTransport>>   m_peers    {};
    std::vector<std::shared_ptr<LogEntry>>         m_logs     {};
    std::vector<uint64_t>                          m_nextindex{};
    std::vector<uint64_t>                          m_matchIndex{};
    std::shared_ptr<Persister>                     m_persister{};
    std::shared_ptr<ApplyChannel>                  m_applyChannel{};
    std::shared_ptr<Logger>                        m_logger   {};
    std::chrono::steady_clock::time_point          m_lastHeartbeat{};
    std::chrono::milliseconds                      m_electionTimeout{};
    ThreadPool  m_threadPool;
    std::thread m_raftThread {};
    void*       m_last_vector_addr { nullptr };
    bool        m_newLogArrive     { false };

    std::atomic<bool>       m_dead { false };
    std::mutex              m_mu;
    std::condition_variable m_cv;
};
