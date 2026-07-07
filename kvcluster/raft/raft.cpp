#include <random>
#include <chrono>
#include <tuple>
#include <iostream>
#include "raft.hpp"
#include "persister.hpp"
#include "logger.hpp"
#include "json.hpp"

// ============================================================
// Constructor / Destructor
// ============================================================

Raft::Raft(std::vector<std::shared_ptr<RaftClient>> peers, int32_t id,
           std::shared_ptr<Persister> persister,
           std::shared_ptr<ApplyChannel> applyChannel,
           std::shared_ptr<Logger> logger)
    : m_peers       { std::move(peers) }
    , m_id          { id }
    , m_votedFor    { -1 }
    , m_currentTerm { 0 }
    , m_commitIndex { 0 }
    , m_lastApplied { 0 }
    , m_state       { State::FOLLOWER }
    , m_persister   { persister }
    , m_applyChannel{ applyChannel }
    , m_logger      { logger }
    , m_nextindex   (m_peers.size(), 0)
    , m_matchIndex  (m_peers.size(), 0)
    , m_threadPool  { m_peers.size() * 2 }
{
    {
        std::lock_guard<std::mutex> lock(m_mu);
        m_lastHeartbeat  = std::chrono::steady_clock::now();
        m_electionTimeout = std::chrono::milliseconds(helperGenerateTimeout());
        helperReadPersist();
    }
    m_raftThread = std::thread(&Raft::startRaft, this);
}

Raft::~Raft()
{
    m_threadPool.shutdown();

    {
        std::lock_guard<std::mutex> lock(m_mu);
        m_dead.store(true);
    }

    m_cv.notify_all();
    if (m_raftThread.joinable())
    {
        m_raftThread.join();
    }
}

// ============================================================
// Main loop
// ============================================================

void Raft::startRaft()
{
    std::unique_lock<std::mutex> lock(m_mu);
    while (!m_dead.load())
    {
        if (m_state == Raft::State::FOLLOWER)
        {
            m_cv.wait_for(lock, std::chrono::milliseconds(20), [this] {
                return m_dead.load() || (std::chrono::steady_clock::now() > m_lastHeartbeat + m_electionTimeout);
            });

            if (m_dead.load()) 
                return;
            else if (std::chrono::steady_clock::now() > m_lastHeartbeat + m_electionTimeout)
            {
                helperPromoteToCandidate();

                lock.unlock();
                startElection();
                lock.lock();
            }
        }
        else if (m_state == Raft::State::CANDIDATE)
        {
            m_cv.wait_for(lock, std::chrono::milliseconds(50), [this] {
                return (m_dead.load() || m_state != State::CANDIDATE || m_votesGranted > static_cast<int>(m_peers.size()/2));
            });

            if (m_dead.load()) 
                return;

            if (m_state == State::FOLLOWER) 
                continue;
            else if (m_votesGranted > static_cast<int>(m_peers.size()/2))
            {
                helperPromoteToLeader();
                auto lastLogIndex = m_lastIncludedIndex + static_cast<uint64_t>(m_logs.size()-1);
                std::fill(m_nextindex.begin(),  m_nextindex.end(),  lastLogIndex + 1);
                std::fill(m_matchIndex.begin(), m_matchIndex.end(), 0);
            }
            else if (std::chrono::steady_clock::now() > m_lastHeartbeat + m_electionTimeout)
            {
                lock.unlock();
                startElection();
                lock.lock();
            }
        }
        else if (m_state == Raft::State::LEADER)
        {
            m_cv.wait_for(lock, std::chrono::milliseconds(100), [this] {
                return m_dead.load() || m_state != State::LEADER || m_newLogArrive;
            });

            m_newLogArrive = false;

            if (m_dead.load() || m_state != State::LEADER) 
                continue;

            lock.unlock();
            broadcastAppendEntries();
            lock.lock();
        }
        else
        {
            LogEvent event(LogEvent::Type::ERROR, m_id, m_currentTerm, "Invalid State!");
            m_logger->logRaft(LogLevel::ERROR, event);
            return;
        }
    }
}

// ============================================================
// Election
// ============================================================

void Raft::startElection()
{
    {
        std::lock_guard<std::mutex> lock(m_mu);
        if (m_dead.load() || m_state != State::CANDIDATE) return;
        m_currentTerm++;
        m_votedFor    = m_id;
        m_votesGranted = 1;
        m_lastHeartbeat = std::chrono::steady_clock::now();
        m_electionTimeout = std::chrono::milliseconds(helperGenerateTimeout());
        LogEvent event(LogEvent::Type::ELECTION, m_id, m_currentTerm, "Starting Election with term " + std::to_string(m_currentTerm));
        m_logger->logRaft(LogLevel::INFO, event);
        helperPersist();
    }

    for (size_t id{0}; id < m_peers.size(); id++)
    {
        if (static_cast<int32_t>(id) == m_id) continue;
        RequestVoteArgs args;
        {
            std::lock_guard<std::mutex> lock(m_mu);
            if (m_dead.load() || m_state != State::CANDIDATE) return;
            uint64_t lastLogIndex = static_cast<uint64_t>(m_logs.size() - 1) + m_lastIncludedIndex;
            uint32_t lastLogTerm  = m_logs.back()->term;
            args = { m_currentTerm, m_id, lastLogIndex, lastLogTerm };
        }
        m_threadPool.enqueue([this, id, args] {
            RequestVoteReply reply;
            bool received = sendRequestVoteRPC(static_cast<int32_t>(id), args, reply);
            if (received)
            {
                std::lock_guard<std::mutex> lock(m_mu);
                if (m_dead.load() || m_state != State::CANDIDATE) return;
                if (reply.voteGranted)
                {
                    m_votesGranted++;
                    LogEvent event(LogEvent::Type::ELECTION, m_id, m_currentTerm, "Received true vote from server " + std::to_string(id));
                    m_logger->logRaft(LogLevel::INFO, event);
                    m_cv.notify_all();
                }
                else if (!reply.voteGranted && reply.term > m_currentTerm)
                {
                    helperStepDownToFollower(reply.term);
                }
            }
        });
    }
}

// ============================================================
// Replication
// ============================================================

void Raft::broadcastAppendEntries()
{
    for (size_t id{0}; id < m_peers.size(); id++)
    {
        if (static_cast<int32_t>(id) == m_id) 
            continue;

        AppendEntriesArgs args;
        uint64_t lastAbsIndexSent{0};
        std::vector<LogEntry> logEntries;
        {
            std::lock_guard<std::mutex> lock(m_mu);
            if (m_dead.load() || m_state != State::LEADER) 
                return;

            if (helperNeedsSnapshot(id))
            {
                helperTriggerInstallSnapshot(id);
                continue;
            }

            if (m_dead.load()) {
                std::cerr << "!!! Thread still running after m_dead=true" << std::endl;
                return;
            }
            
            /*
            void* current_vector_addr = m_nextindex.data();
            if (m_last_vector_addr != nullptr && m_last_vector_addr != current_vector_addr)
                std::cerr << "!!! REAL CRITICAL: m_nextindex vector moved in memory!" << std::endl;
            m_last_vector_addr = current_vector_addr;
            if (m_nextindex[id] > 10000000) {
                std::cerr << "!!! DATA CORRUPTION: Found huge nextIndex value " << m_nextindex[id] << " for server " << id << std::endl;
                return;
            }
            if (id >= m_nextindex.size()) {
                std::cerr << "!!! OOB ACCESS: id=" << id << " size=" << m_nextindex.size() << std::endl;
                continue;
            }
            */

            uint64_t prevLogIndex   = m_nextindex[id] - 1;
            auto relPrevLogIndex    = helperGetRelativeIndex(prevLogIndex);
            auto relNextIndex       = helperGetRelativeIndex(m_nextindex[id]);
            uint32_t prevLogTerm    = m_logs[relPrevLogIndex]->term;

            for (size_t i = relNextIndex; i < m_logs.size(); i++)
            {
                if (i > 0)
                {
                    logEntries.emplace_back(m_logs[i]->command, m_logs[i]->term);
                    lastAbsIndexSent = std::max(lastAbsIndexSent, i + m_lastIncludedIndex);
                }
            }
            args = { m_currentTerm, m_id, prevLogIndex, prevLogTerm, logEntries, m_commitIndex };
        }

        LogEvent event 
        {
            logEntries.empty() ? LogEvent::Type::HEARTBEAT : LogEvent::Type::REPLICATION,
            m_id, m_currentTerm,
            logEntries.empty() ? "Broadcasting Heartbeat to " + std::to_string(id)
                               : "Broadcasting AppendEntries to " + std::to_string(id)
        };
        m_logger->logRaft(LogLevel::DEBUG, event);

        uint64_t termStarted;
        {
            std::lock_guard<std::mutex> lock(m_mu);
            termStarted = m_currentTerm;
        }
        m_threadPool.enqueue([this, id, lastAbsIndexSent, termStarted, args = std::move(args)] {
            AppendEntriesReply reply{};
            bool received = sendAppendEntriesRPC(static_cast<int32_t>(id), args, reply);

            if (received)
            {
                {
                    std::lock_guard<std::mutex> lock(m_mu);

                    if (m_dead.load() || m_state != State::LEADER || m_currentTerm != termStarted) 
                        return;

                    if (reply.term > m_currentTerm)
                    {
                        helperStepDownToFollower(reply.term);
                        return;
                    }
                }

                if (reply.success)
                {
                    std::lock_guard<std::mutex> lock(m_mu);

                    if (m_dead.load() || m_state != State::LEADER) 
                        return;

                    LogEvent ev(LogEvent::Type::REPLICATION, m_id, m_currentTerm, "Node " + std::to_string(id) + " ACKED index " + std::to_string(args.preLogIndex + args.entries.size()));
                    m_logger->logRaft(LogLevel::INFO, ev);

                    uint64_t matchIndex = args.preLogIndex + args.entries.size();
                    m_matchIndex[id] = std::max(m_matchIndex[id], matchIndex);
                    m_nextindex[id]  = m_matchIndex[id] + 1;

                    helperUpdateLeaderCommitIndex();

                    while (m_commitIndex > m_lastApplied)
                    {
                        m_lastApplied++;
                        m_applyChannel->push(ApplyMsg{true, m_lastApplied,
                            m_logs[m_lastApplied - m_lastIncludedIndex]->term,
                            m_logs[m_lastApplied - m_lastIncludedIndex]->command,
                            false, {}, 0, 0});

                        LogEvent ae(LogEvent::Type::APPLY, m_id, m_currentTerm, "Apply log with index: " + std::to_string(m_lastApplied));
                        m_logger->logRaft(LogLevel::INFO, ae);
                    }
                }
                else
                {
                    std::lock_guard<std::mutex> lock(m_mu);

                    LogEvent ev(LogEvent::Type::REPLICATION, m_id, m_currentTerm, "Node " + std::to_string(id) + " REJECT");
                    m_logger->logRaft(LogLevel::INFO, ev);

                    if (m_dead.load() || m_state != State::LEADER) 
                        return;

                    if (reply.conflictIndex > 0)
                    {
                        m_nextindex[id] = reply.conflictIndex;
                    }
                    else if (m_nextindex[id] > m_lastIncludedIndex + 1)
                    {
                        m_nextindex[id]--;
                    }
                    if (m_nextindex[id] <= m_lastIncludedIndex)
                    {
                        helperTriggerInstallSnapshot(id);
                        return;
                    }
                    m_cv.notify_all();
                }
            }
            else
            {
                LogEvent ev(LogEvent::Type::REPLICATION, m_id, m_currentTerm, "RPC FAILED/TIMEOUT to Node " + std::to_string(id));
                m_logger->logRaft(LogLevel::ERROR, ev);
            }
        });
    }
}

// ============================================================
// RPC handlers (called by RaftServiceImpl)
// ============================================================

void Raft::appendEntries(const AppendEntriesArgs& args, AppendEntriesReply& reply)
{
    {
        std::lock_guard<std::mutex> lock(m_mu);
        m_logger->logRaft(LogLevel::INFO, LogEvent(LogEvent::Type::REPLICATION, m_id, m_currentTerm, "RECEIVED appendEntries from " + std::to_string(args.leaderId)));
        reply.term    = m_currentTerm;
        reply.success = false;
        if (m_currentTerm > args.term) return;
        if (m_currentTerm < args.term) helperStepDownToFollower(args.term);
    }
    {
        std::lock_guard<std::mutex> lock(m_mu);
        if (args.preLogIndex < m_lastIncludedIndex) { reply.success = false; return; }
        uint64_t relPreLogIndex = args.preLogIndex - m_lastIncludedIndex;
        if (relPreLogIndex >= m_logs.size() || m_logs[relPreLogIndex]->term != args.preLogTerm)
        {
            if (relPreLogIndex >= m_logs.size()) {
                reply.conflictIndex = (m_logs.size() - 1) + m_lastIncludedIndex;
                reply.conflictTerm  = -1;
            } else {
                reply.conflictTerm  = m_logs[relPreLogIndex]->term;
                reply.conflictIndex = args.preLogIndex;
                uint64_t relConflictIndex = reply.conflictIndex - m_lastIncludedIndex;
                while (relConflictIndex > 0 && m_logs[relConflictIndex-1]->term == reply.conflictTerm) {
                    std::cout << "follower " << m_id << " stuck in while loop" << std::endl;
                    reply.conflictIndex--;
                    relConflictIndex--;
                }
            }
            reply.success = false;
            return;
        }
    }
    {
        std::lock_guard<std::mutex> lock(m_mu);
        reply.success = true;
        m_lastHeartbeat   = std::chrono::steady_clock::now();
        m_electionTimeout = std::chrono::milliseconds(helperGenerateTimeout());
        size_t adj{args.preLogIndex + 1 - m_lastIncludedIndex};
        size_t mark{0};
        for (size_t i{0}; i < args.entries.size(); i++)
        {
            if (i + adj < m_logs.size())
            {
                if (m_logs[i + adj]->term != args.entries[i].term) {
                    m_logs.resize(i + adj);
                    mark = i;
                    LogEvent ev(LogEvent::Type::DELETION, m_id, m_currentTerm, "Resizing logs to index:" + std::to_string(m_logs.size()-1 + m_lastIncludedIndex));
                    m_logger->logRaft(LogLevel::INFO, ev);
                    break;
                } else if (m_logs[i + adj]->command == args.entries[i].command) {
                    mark++;
                }
            }
        }
        for (size_t i = mark; i < args.entries.size(); i++)
        {
            m_logs.push_back(std::make_shared<LogEntry>(args.entries[i].command, args.entries[i].term));
            LogEvent ev(LogEvent::Type::REPLICATION, m_id, m_currentTerm,
                "Append log entry command:" + m_logs.back()->command + " term:" + std::to_string(m_logs.back()->term));
            m_logger->logRaft(LogLevel::INFO, ev);
        }
        helperPersist();
    }
    {
        std::lock_guard<std::mutex> lock(m_mu);
        if (args.leaderCommit > m_commitIndex)
        {
            uint64_t lastNewEntryIndex = args.preLogIndex + args.entries.size();
            m_commitIndex = std::min(args.leaderCommit, lastNewEntryIndex);
        }
        while (m_commitIndex > m_lastApplied)
        {
            m_lastApplied++;
            m_applyChannel->push(ApplyMsg{true, m_lastApplied,
                m_logs[m_lastApplied - m_lastIncludedIndex]->term,
                m_logs[m_lastApplied - m_lastIncludedIndex]->command,
                false, {}, 0, 0});
            LogEvent ev(LogEvent::Type::APPLY, m_id, m_currentTerm, "Apply log with index:" + std::to_string(m_lastApplied));
            m_logger->logRaft(LogLevel::INFO, ev);
        }
    }
}

void Raft::requestVote(const RequestVoteArgs& args, RequestVoteReply& reply)
{
    std::lock_guard<std::mutex> lock(m_mu);
    reply.term        = m_currentTerm;
    reply.voteGranted = false;
    if (m_currentTerm > args.term) {
        LogEvent ev(LogEvent::Type::ELECTION, m_id, m_currentTerm, "Reject requestVote: Candidate has lower term");
        m_logger->logRaft(LogLevel::DEBUG, ev);
        return;
    }
    if (m_currentTerm < args.term) helperStepDownToFollower(args.term);
    uint64_t lastLogIndex = static_cast<uint64_t>(m_logs.size()-1) + m_lastIncludedIndex;
    uint32_t lastLogTerm  = m_logs.back()->term;
    bool logsUpToDate = (args.lastLogTerm > lastLogTerm) ||
                        (args.lastLogTerm == lastLogTerm && args.lastLogIndex >= lastLogIndex);
    if ((m_votedFor == -1 || m_votedFor == args.candidateId) && logsUpToDate)
    {
        reply.voteGranted = true;
        m_votedFor        = args.candidateId;
        m_lastHeartbeat   = std::chrono::steady_clock::now();
        m_electionTimeout = std::chrono::milliseconds(helperGenerateTimeout());
        LogEvent ev(LogEvent::Type::ELECTION, m_id, m_currentTerm, "Voted for " + std::to_string(m_votedFor));
        m_logger->logRaft(LogLevel::INFO, ev);
    }
    helperPersist();
}

void Raft::installSnapshot(const InstallSnapshotArgs& args, InstallSnapshotReply& reply)
{
    std::lock_guard<std::mutex> lock(m_mu);
    if (m_dead.load()) return;
    reply.term = m_currentTerm;
    if (args.term < m_currentTerm) return;
    if (args.term > m_currentTerm) helperStepDownToFollower(args.term);
    if (args.lastIncludedIndex <= m_lastIncludedIndex) return;
    m_lastIncludedIndex = args.lastIncludedIndex;
    m_lastIncludedTerm  = args.lastIncludedTerm;
    m_logs.clear();
    auto dummy = std::make_shared<LogEntry>();
    dummy->command = ""; dummy->term = m_lastIncludedTerm;
    m_logs.push_back(dummy);
    if (m_lastApplied < m_lastIncludedIndex) m_lastApplied = m_lastIncludedIndex;
    helperPersist();
    auto latestRaftState = m_persister->readRaftState();
    m_persister->saveStateAndSnapshot(latestRaftState, args.data);
    m_applyChannel->push(ApplyMsg{false, 0, 0, "", true, args.data,
        static_cast<uint64_t>(m_lastIncludedIndex),
        static_cast<uint32_t>(m_lastIncludedTerm)});
}

// ============================================================
// Client interface
// ============================================================

std::tuple<int, int, bool> Raft::start(const std::string& command)
{
    std::lock_guard<std::mutex> lock(m_mu);
    if (m_state != State::LEADER)
        return {-1, -1, false};

    LogEvent ev(LogEvent::Type::REPLICATION, m_id, m_currentTerm, "Appended new log entry: " + command);
    m_logger->logRaft(LogLevel::INFO, ev);
    
    m_logs.emplace_back(std::make_shared<LogEntry>(command, m_currentTerm));
    m_newLogArrive = true;
    helperPersist();
    m_cv.notify_all();
    return {static_cast<int>(m_logs.size()-1 + m_lastIncludedIndex), static_cast<int>(m_currentTerm), true};
}

bool Raft::isLeader()
{
    std::lock_guard<std::mutex> lock(m_mu);
    return m_state == State::LEADER;
}

void Raft::kill()
{
    std::lock_guard<std::mutex> lock(m_mu);
    m_dead.store(true);
    m_cv.notify_all();
}

std::pair<uint32_t, Raft::State> Raft::getTermState()
{
    std::lock_guard<std::mutex> lock(m_mu);
    return {m_currentTerm, m_state};
}

// ============================================================
// Snapshot
// ============================================================

void Raft::snapshot(uint64_t index, const std::string& snapshot)
{
    nlohmann::json j;
    std::lock_guard<std::mutex> lock(m_mu);
    if (index <= m_lastIncludedIndex) return;
    uint64_t relIndex = index - m_lastIncludedIndex;
    if (relIndex >= m_logs.size()) return;
    uint64_t keepFrom = index - m_lastIncludedIndex + 1;
    m_lastIncludedTerm  = m_logs[relIndex]->term;
    m_lastIncludedIndex = index;
    if (keepFrom < m_logs.size())
        m_logs.erase(m_logs.begin(), m_logs.begin() + keepFrom);
    else
        m_logs.clear();
    auto dummy = std::make_shared<LogEntry>();
    dummy->command = ""; dummy->term = m_lastIncludedTerm;
    m_logs.insert(m_logs.begin(), dummy);
    j["Term"]   = m_currentTerm;
    j["Voted"]  = m_votedFor;
    std::vector<nlohmann::json> logVector;
    for (size_t i=1; i<m_logs.size(); i++)
        logVector.push_back({{"Command", m_logs[i]->command}, {"Term", m_logs[i]->term}});
    j["Log"] = logVector;
    auto data = j.dump();
    std::vector<uint8_t> raftState(data.begin(), data.end());
    std::vector<uint8_t> snapshotByte(snapshot.begin(), snapshot.end());
    m_persister->saveStateAndSnapshot(raftState, snapshotByte);
}

// ============================================================
// Helpers
// ============================================================

int Raft::helperGenerateTimeout()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(700, 900);
    return dist(gen);
}

void Raft::helperUpdateLeaderCommitIndex()
{
    if (m_dead.load() || m_state != State::LEADER) return;
    if (m_logs.empty()) return;
    uint64_t lastLogIndex = m_lastIncludedIndex + m_logs.size() - 1;
    for (uint64_t N = lastLogIndex; N > m_commitIndex; --N)
    {
        uint64_t relIndex = N - m_lastIncludedIndex;
        if (relIndex >= m_logs.size() || relIndex < 0) continue;
        int replicated = 1;
        for (size_t i = 0; i < m_peers.size(); i++)
        {
            if (static_cast<int>(i) == m_id) continue;
            if (m_matchIndex[i] >= N) replicated++;
        }
        if (replicated > static_cast<int>(m_peers.size() / 2) && m_logs[relIndex]->term == m_currentTerm)
        {
            m_commitIndex = N;
            LogEvent ev(LogEvent::Type::REPLICATION, m_id, m_currentTerm, "Updated CommitIndex to: " + std::to_string(m_commitIndex));
            m_logger->logRaft(LogLevel::INFO, ev);
            break;
        }
    }
}

void Raft::helperPromoteToLeader()
{
    m_state = State::LEADER;
    m_cv.notify_all();
    LogEvent ev(LogEvent::Type::STATECHANGE, m_id, m_currentTerm, "State change to LEADER");
    m_logger->logRaft(LogLevel::INFO, ev);
}

void Raft::helperPromoteToCandidate()
{
    m_state = Raft::State::CANDIDATE;
    m_cv.notify_all();
    LogEvent ev(LogEvent::Type::STATECHANGE, m_id, m_currentTerm, "State change to CANDIDATE");
    m_logger->logRaft(LogLevel::INFO, ev);
}

void Raft::helperStepDownToFollower(uint32_t term)
{
    m_currentTerm     = term;
    m_votedFor        = -1;
    m_state           = State::FOLLOWER;
    m_lastHeartbeat   = std::chrono::steady_clock::now();
    m_electionTimeout = std::chrono::milliseconds(helperGenerateTimeout());
    LogEvent ev(LogEvent::Type::STATECHANGE, m_id, m_currentTerm, "State change to FOLLOWER");
    m_logger->logRaft(LogLevel::INFO, ev);
    m_cv.notify_all();
    helperPersist();
}

void Raft::helperPersist()
{
    nlohmann::json j;
    j["Term"]    = m_currentTerm;
    j["VotedFor"] = m_votedFor;
    std::vector<nlohmann::json> logVector;
    for (size_t i{1}; i < m_logs.size(); i++)
        logVector.push_back({{"Command", m_logs[i]->command}, {"Term", m_logs[i]->term}});
    j["Log"] = logVector;
    std::string data = j.dump();
    std::vector<uint8_t> state(data.begin(), data.end());
    m_persister->saveRaftState(state);
}

void Raft::helperReadPersist()
{
    std::vector<uint8_t> raftStateBytes = m_persister->readRaftState();
    std::vector<uint8_t> snapshotBytes  = m_persister->readSnapshot();
    if (raftStateBytes.empty())
    {
        m_lastIncludedIndex = 0;
        m_lastIncludedTerm  = 0;
        m_logs.clear();
        auto dummy = std::make_shared<LogEntry>();
        dummy->command = ""; dummy->term = 0;
        m_logs.push_back(dummy);
        LogEvent ev(LogEvent::Type::PERSISTER, m_id, m_currentTerm, "No previous state from persister");
        m_logger->logRaft(LogLevel::INFO, ev);
        return;
    }
    std::string raftStateStr(raftStateBytes.begin(), raftStateBytes.end());
    nlohmann::json j = nlohmann::json::parse(raftStateStr);
    m_currentTerm       = j.value("Term",    0u);
    m_votedFor          = j.value("VotedFor", -1);
    m_lastIncludedIndex = 0;
    m_lastIncludedTerm  = 0;
    if (!snapshotBytes.empty())
    {
        try {
            std::string snapStr(snapshotBytes.begin(), snapshotBytes.end());
            nlohmann::json snapJ = nlohmann::json::parse(snapStr);
            m_lastIncludedIndex = snapJ.value("LastIncludedIndex", 0ull);
            m_lastIncludedTerm  = snapJ.value("LastIncludedTerm",  0u);
        } catch(...) {
            LogEvent ev(LogEvent::Type::ERROR, m_id, m_currentTerm, "Failed to parse snapshot");
            m_logger->logRaft(LogLevel::INFO, ev);
        }
    }
    m_lastApplied = m_lastIncludedIndex;
    m_logs.clear();
    auto dummy = std::make_shared<LogEntry>();
    dummy->command = ""; dummy->term = m_lastIncludedTerm;
    m_logs.push_back(dummy);
    for (const auto& jsonEntry : j["Log"])
    {
        auto command = jsonEntry.value("Command", "");
        auto term    = jsonEntry.value("Term",    0u);
        m_logs.push_back(std::make_shared<LogEntry>(command, term));
    }
    LogEvent ev(LogEvent::Type::PERSISTER, m_id, m_currentTerm, "Restored state from persister");
    m_logger->logRaft(LogLevel::INFO, ev);
}

bool Raft::helperNeedsSnapshot(size_t peerId)
{
    return m_nextindex[peerId] <= m_lastIncludedIndex;
}

void Raft::helperTriggerInstallSnapshot(size_t id)
{
    InstallSnapshotArgs args;
    {
        if (m_dead.load() || m_state != State::LEADER) return;
        args.term              = m_currentTerm;
        args.leaderId          = m_id;
        args.lastIncludedIndex = m_lastIncludedIndex;
        args.lastIncludedTerm  = m_lastIncludedTerm;
        args.offset            = 0;
        args.data              = m_persister->readSnapshot();
        args.done              = true;
    }
    m_threadPool.enqueue([this, id, args]() {
        InstallSnapshotReply reply;
        bool received = sendInstallSnapshotRPC(static_cast<int32_t>(id), args, reply);
        if (received)
        {
            std::lock_guard<std::mutex> lock(m_mu);
            if (m_dead.load() || m_state != State::LEADER) return;
            if (reply.term > m_currentTerm) { helperStepDownToFollower(reply.term); return; }
            m_matchIndex[id] = m_lastIncludedIndex;
            m_nextindex[id]  = m_lastIncludedIndex + 1;
            LogEvent ev(LogEvent::Type::SNAPSHOT, m_id, m_currentTerm,
                "Follower " + std::to_string(id) + " installed snapshot");
            m_logger->logRaft(LogLevel::INFO, ev);
        }
    });
}

uint64_t Raft::helperGetRelativeIndex(uint64_t absoluteIndex) const
{
    return absoluteIndex - m_lastIncludedIndex;
}

// ============================================================
// Transport calls (replaced from labrpc::Endpoint::call)
// ============================================================

bool Raft::sendRequestVoteRPC(int32_t id, const RequestVoteArgs& args, RequestVoteReply& reply)
{
    LogEvent ev(LogEvent::Type::ELECTION, m_id, m_currentTerm, "Sending RequestVote to server " + std::to_string(id));
    m_logger->logRaft(LogLevel::DEBUG, ev);
    
    return m_peers[id]->sendRequestVote(args, reply);
}

bool Raft::sendAppendEntriesRPC(int32_t id, const AppendEntriesArgs& args, AppendEntriesReply& reply)
{
    return m_peers[id]->sendAppendEntries(args, reply);
}

bool Raft::sendInstallSnapshotRPC(int32_t id, const InstallSnapshotArgs& args, InstallSnapshotReply& reply)
{
    return m_peers[id]->sendInstallSnapshot(args, reply);
}
