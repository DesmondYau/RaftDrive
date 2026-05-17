#pragma once
#include "raft/raft_types.hpp"

// 
// 

/**
 * @brief Point-to-point transport abstraction — one instance per peer.
 * 
 * Think of it as the Enpoint Client which calls RequestVote/AppendEntries/InstallSnapshot service on another Raft node
 * GrpcRaftTransport implements this over a real gRPC connection.
 * All send* methods return true on success, false on network failure / timeout.
 */
class IRaftTransport 
{
public:
    virtual ~IRaftTransport() = default;
    virtual bool sendRequestVote(const RequestVoteArgs& args, RequestVoteReply& reply) = 0;
    virtual bool sendAppendEntries(const AppendEntriesArgs& args, AppendEntriesReply& reply) = 0;
    virtual bool sendInstallSnapshot(const InstallSnapshotArgs& args, InstallSnapshotReply& reply) = 0;
};
