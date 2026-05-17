#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Raft RPC structs extracted from Raft class so IRaftTransport can reference them
// without a circular include dependency.  raft.hpp re-exports them as type aliases.

struct LogEntry 
{
    std::string command;
    uint32_t    term;
};

struct AppendEntriesArgs 
{
    uint32_t              term;
    int32_t               leaderId;
    uint64_t              preLogIndex;
    uint32_t              preLogTerm;
    std::vector<LogEntry> entries;
    uint64_t              leaderCommit;
};

struct AppendEntriesReply 
{
    uint32_t term;
    bool     success;
    uint64_t conflictIndex;
    uint32_t conflictTerm;
};

struct RequestVoteArgs 
{
    uint32_t term;
    int32_t  candidateId;
    uint64_t lastLogIndex;
    uint32_t lastLogTerm;
};

struct RequestVoteReply 
{
    uint32_t term;
    bool     voteGranted;
};

struct InstallSnapshotArgs 
{
    uint32_t             term;
    int32_t              leaderId;
    uint64_t             lastIncludedIndex;
    uint32_t             lastIncludedTerm;
    int                  offset;
    std::vector<uint8_t> data;
    bool                 done;
};

struct InstallSnapshotReply 
{
    uint32_t term;
};
