#pragma once
#include <grpcpp/grpcpp.h>
#include "raft.grpc.pb.h"
#include "raft/raft.hpp"

class RaftServiceImpl final : public raftpb::RaftService::Service {
public:
    explicit RaftServiceImpl(Raft* raft) : m_raft(raft) {}

    grpc::Status RequestVote(grpc::ServerContext*, const raftpb::RequestVoteArgs* req, raftpb::RequestVoteReply* resp) override
    {
        RequestVoteArgs args;
        args.term         = req->term();
        args.candidateId  = req->candidate_id();
        args.lastLogIndex = req->last_log_index();
        args.lastLogTerm  = req->last_log_term();

        std::cerr << "[RaftService] RequestVote from candidate=" << args.candidateId
                  << " term=" << args.term << "\n";

        RequestVoteReply reply;
        m_raft->requestVote(args, reply);

        std::cerr << "[RaftService] RequestVote reply: granted=" << reply.voteGranted
                  << " term=" << reply.term << "\n";

        resp->set_term(reply.term);
        resp->set_vote_granted(reply.voteGranted);
        return grpc::Status::OK;
    }

    grpc::Status AppendEntries(grpc::ServerContext*,
                               const raftpb::AppendEntriesArgs* req,
                               raftpb::AppendEntriesReply* resp) override
    {
        AppendEntriesArgs args;
        args.term        = req->term();
        args.leaderId    = req->leader_id();
        args.preLogIndex = req->pre_log_index();
        args.preLogTerm  = req->pre_log_term();
        args.leaderCommit = req->leader_commit();

        for (const auto& pe : req->entries()) {
            LogEntry e;
            e.command = pe.command();
            e.term    = pe.term();
            args.entries.push_back(std::move(e));
        }

        AppendEntriesReply reply;
        m_raft->appendEntries(args, reply);

        resp->set_term(reply.term);
        resp->set_success(reply.success);
        resp->set_conflict_index(reply.conflictIndex);
        resp->set_conflict_term(reply.conflictTerm);
        return grpc::Status::OK;
    }

    grpc::Status InstallSnapshot(grpc::ServerContext*,
                                 const raftpb::InstallSnapshotArgs* req,
                                 raftpb::InstallSnapshotReply* resp) override
    {
        InstallSnapshotArgs args;
        args.term              = req->term();
        args.leaderId          = req->leader_id();
        args.lastIncludedIndex = req->last_included_index();
        args.lastIncludedTerm  = req->last_included_term();
        args.offset            = req->offset();
        const auto& d = req->data();
        args.data.assign(d.begin(), d.end());
        args.done = req->done();

        InstallSnapshotReply reply;
        m_raft->installSnapshot(args, reply);

        resp->set_term(reply.term);
        return grpc::Status::OK;
    }

private:
    Raft* m_raft; // non-owning; lifetime managed by caller
};
