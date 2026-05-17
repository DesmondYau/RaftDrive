#pragma once

#include <memory>
#include <grpcpp/grpcpp.h>
#include "kvstore.grpc.pb.h"
#include "backends/backend_abstract.hpp"


class KVStoreServiceImpl final : public kvstore::KVStore::Service
{
public:
    explicit KVStoreServiceImpl(std::shared_ptr<IKVBackend> backend)
        : m_backend(std::move(backend))
    {}

    grpc::Status Get(grpc::ServerContext*, const kvstore::GetRequest* req, kvstore::GetResponse* resp) override
    {
        if (!m_backend->isHealthy())
        {
            resp->set_status(kvstore::GetResponse::UNAVAILABLE);
            return grpc::Status::OK;
        }

        if (!m_backend->isLeader())
        {
            resp->set_status(kvstore::GetResponse::WRONG_LEADER);
            return grpc::Status::OK;
        }

        try
        {
            std::string value = m_backend->get(req->key());
            if (value.empty())
                resp->set_status(kvstore::GetResponse::NOT_FOUND);
            else
            {
                resp->set_value(value);
                resp->set_status(kvstore::GetResponse::OK);
            }
        }
        catch (const WrongLeaderError&)
        {
            resp->set_status(kvstore::GetResponse::WRONG_LEADER);
        }

        return grpc::Status::OK;
    }

    grpc::Status Put(grpc::ServerContext*, const kvstore::PutRequest* req, kvstore::PutResponse* resp) override
    {
        if (!m_backend->isHealthy())
        {
            resp->set_status(kvstore::PutResponse::UNAVAILABLE);
            return grpc::Status::OK;
        }

        if (!m_backend->isLeader())
        {
            resp->set_status(kvstore::PutResponse::WRONG_LEADER);
            return grpc::Status::OK;
        }

        try
        {
            m_backend->put(req->key(), req->value());
            resp->set_status(kvstore::PutResponse::OK);
        }
        catch (const WrongLeaderError&)
        {
            resp->set_status(kvstore::PutResponse::WRONG_LEADER);
        }

        return grpc::Status::OK;
    }

    grpc::Status Append(grpc::ServerContext*, const kvstore::AppendRequest* req, kvstore::AppendResponse* resp) override
    {
        if (!m_backend->isHealthy())
        {
            resp->set_status(kvstore::AppendResponse::UNAVAILABLE);
            return grpc::Status::OK;
        }

        if (!m_backend->isLeader())
        {
            resp->set_status(kvstore::AppendResponse::WRONG_LEADER);
            return grpc::Status::OK;
        }

        try
        {
            m_backend->append(req->key(), req->value());
            resp->set_status(kvstore::AppendResponse::OK);
        }
        catch (const WrongLeaderError&)
        {
            resp->set_status(kvstore::AppendResponse::WRONG_LEADER);
        }

        return grpc::Status::OK;
    }

private:
    std::shared_ptr<IKVBackend> m_backend;
};
