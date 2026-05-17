#pragma once
#include <grpcpp/grpcpp.h>
#include "raft.grpc.pb.h"
#include "client_abstract.hpp"


/**
 * @brief Implementation of Abstract Class IRaftTranspot
 */
class GrpcRaftTransport : public IRaftTransport {
public:
    explicit GrpcRaftTransport(const std::string& peerAddr)
        : m_stub(raftpb::RaftService::NewStub(grpc::CreateChannel(peerAddr, grpc::InsecureChannelCredentials())))
    {}

    bool sendRequestVote(const RequestVoteArgs& args, RequestVoteReply& reply) override
    {
        raftpb::RequestVoteArgs req;
        req.set_term(args.term);
        req.set_candidate_id(args.candidateId);
        req.set_last_log_index(args.lastLogIndex);
        req.set_last_log_term(args.lastLogTerm);

        raftpb::RequestVoteReply resp;
        grpc::ClientContext ctx;
        ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(200));

        grpc::Status status = m_stub->RequestVote(&ctx, req, &resp);
        if (!status.ok()) 
        {
            std::cerr << "[GrpcTransport] RequestVote FAILED: code=" << status.error_code()
                      << " msg=" << status.error_message() << "\n";
            return false;
        }

        reply.term        = resp.term();
        reply.voteGranted = resp.vote_granted();
        return true;
    }

    bool sendAppendEntries(const AppendEntriesArgs& args, AppendEntriesReply& reply) override
    {
        raftpb::AppendEntriesArgs req;
        req.set_term(args.term);
        req.set_leader_id(args.leaderId);
        req.set_pre_log_index(args.preLogIndex);
        req.set_pre_log_term(args.preLogTerm);
        req.set_leader_commit(args.leaderCommit);

        for (const auto& e : args.entries) 
        {
            auto* pe = req.add_entries();
            pe->set_command(e.command);
            pe->set_term(e.term);
        }

        raftpb::AppendEntriesReply resp;
        grpc::ClientContext ctx;
        ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(200));

        grpc::Status status = m_stub->AppendEntries(&ctx, req, &resp);
        if (!status.ok()) 
            return false;

        reply.term          = resp.term();
        reply.success       = resp.success();
        reply.conflictIndex = resp.conflict_index();
        reply.conflictTerm  = resp.conflict_term();
        return true;
    }

    bool sendInstallSnapshot(const InstallSnapshotArgs& args, InstallSnapshotReply& reply) override
    {
        raftpb::InstallSnapshotArgs req;
        req.set_term(args.term);
        req.set_leader_id(args.leaderId);
        req.set_last_included_index(args.lastIncludedIndex);
        req.set_last_included_term(args.lastIncludedTerm);
        req.set_offset(args.offset);
        req.set_data(args.data.data(), args.data.size());
        req.set_done(args.done);

        raftpb::InstallSnapshotReply resp;
        grpc::ClientContext ctx;
        ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(200));

        grpc::Status status = m_stub->InstallSnapshot(&ctx, req, &resp);
        if (!status.ok()) 
            return false;

        reply.term = resp.term();
        return true;
    }

private:
    std::unique_ptr<raftpb::RaftService::Stub> m_stub;
};
