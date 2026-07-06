#pragma once

#include <memory>
#include <random>
#include <atomic>
#include <grpcpp/grpcpp.h>
#include "kvstore.grpc.pb.h"
#include "kvserver/kvserver.hpp"
#include "kvserver/models.hpp"


class KVStoreServiceImpl final : public kvstore::KVStore::Service
{
public:
    explicit KVStoreServiceImpl(std::shared_ptr<KVServer> kvServer)
        : m_kvServer(std::move(kvServer))
    {
        std::mt19937_64 eng(std::random_device{}());
        m_clientId = std::uniform_int_distribution<uint64_t>{}(eng);
    }

    grpc::Status Get(grpc::ServerContext*, const kvstore::GetRequest* req, kvstore::GetResponse* resp) override
    {
        if (!m_kvServer->getRaft()->isLeader())
        {
            resp->set_status(kvstore::GetResponse::WRONG_LEADER);
            return grpc::Status::OK;
        }

        models::GetArgs args;
        args.m_key      = req->key();
        args.m_clientID = m_clientId;
        args.m_seq      = ++m_seq;

        models::GetReply reply;
        m_kvServer->get(args, reply);

        if (reply.m_err == "ErrWrongLeader")
        {
            resp->set_status(kvstore::GetResponse::WRONG_LEADER);
            return grpc::Status::OK;
        }
        if (reply.m_err == "ErrNoKey")
        {
            resp->set_status(kvstore::GetResponse::NOT_FOUND);
            return grpc::Status::OK;
        }

        resp->set_value(reply.m_value);
        resp->set_status(kvstore::GetResponse::OK);
        return grpc::Status::OK;
    }

    grpc::Status Put(grpc::ServerContext*, const kvstore::PutRequest* req, kvstore::PutResponse* resp) override
    {
        if (!m_kvServer->getRaft()->isLeader())
        {
            resp->set_status(kvstore::PutResponse::WRONG_LEADER);
            return grpc::Status::OK;
        }

        models::PutAppendArgs args;
        args.m_key       = req->key();
        args.m_value     = req->value();
        args.m_operation = "Put";
        args.m_clientID  = m_clientId;
        args.m_seq       = ++m_seq;

        models::PutAppendReply reply;
        m_kvServer->putAppend(args, reply);

        if (reply.m_err == "ErrWrongLeader")
        {
            resp->set_status(kvstore::PutResponse::WRONG_LEADER);
            return grpc::Status::OK;
        }

        resp->set_status(kvstore::PutResponse::OK);
        return grpc::Status::OK;
    }

    grpc::Status Append(grpc::ServerContext*, const kvstore::AppendRequest* req, kvstore::AppendResponse* resp) override
    {
        if (!m_kvServer->getRaft()->isLeader())
        {
            resp->set_status(kvstore::AppendResponse::WRONG_LEADER);
            return grpc::Status::OK;
        }

        models::PutAppendArgs args;
        args.m_key       = req->key();
        args.m_value     = req->value();
        args.m_operation = "Append";
        args.m_clientID  = m_clientId;
        args.m_seq       = ++m_seq;

        models::PutAppendReply reply;
        m_kvServer->putAppend(args, reply);

        if (reply.m_err == "ErrWrongLeader")
        {
            resp->set_status(kvstore::AppendResponse::WRONG_LEADER);
            return grpc::Status::OK;
        }

        resp->set_status(kvstore::AppendResponse::OK);
        return grpc::Status::OK;
    }

private:
    std::shared_ptr<KVServer> m_kvServer;
    uint64_t                  m_clientId;
    std::atomic<int>          m_seq{0};
};
