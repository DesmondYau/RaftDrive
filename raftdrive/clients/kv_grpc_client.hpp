#pragma once

#include <memory>
#include <string>
#include <vector>
#include <sstream>
#include <atomic>
#include <thread>
#include <chrono>
#include <grpcpp/grpcpp.h>
#include "kvstore.grpc.pb.h"

class KVStoreClient
{
    static constexpr int    kMaxRounds      = 25;   // 25 × 200ms = 5s timeout
    static constexpr int    kSleepMs        = 200;

public:
    explicit KVStoreClient(const std::string& targets)
    {
        std::stringstream ss(targets);
        std::string addr;
        while (std::getline(ss, addr, ','))
        {
            if (!addr.empty())
                m_stubs.push_back(kvstore::KVStore::NewStub(grpc::CreateChannel(addr, grpc::InsecureChannelCredentials())));
        }
    }

    // Returns value on OK, "" on NOT_FOUND/UNAVAILABLE. Retries until leader found.
    std::string get(const std::string& key)
    {
        const int n = static_cast<int>(m_stubs.size());
        for (int round = 0; round < kMaxRounds; ++round)
        {
            for (int attempt = 0; attempt < n; ++attempt)
            {
                int idx = (m_leaderHint + attempt) % n;

                grpc::ClientContext ctx;
                kvstore::GetRequest req;
                req.set_key(key);
                kvstore::GetResponse resp;

                grpc::Status status = m_stubs[idx]->Get(&ctx, req, &resp);

                if (!status.ok()) continue;
                if (resp.status() == kvstore::GetResponse::WRONG_LEADER) continue;

                m_leaderHint = idx;
                if (resp.status() == kvstore::GetResponse::OK)
                    return resp.value();
                return "";
            }
            // No node gave a definitive answer this round — sleep and retry.
            std::this_thread::sleep_for(std::chrono::milliseconds(kSleepMs));
        }
        return "";
    }

    bool put(const std::string& key, const std::string& value)
    {
        const int n = static_cast<int>(m_stubs.size());
        for (int round = 0; round < kMaxRounds; ++round)
        {
            for (int attempt = 0; attempt < n; ++attempt)
            {
                int idx = (m_leaderHint + attempt) % n;

                grpc::ClientContext ctx;
                kvstore::PutRequest req;
                req.set_key(key);
                req.set_value(value);
                kvstore::PutResponse resp;

                grpc::Status status = m_stubs[idx]->Put(&ctx, req, &resp);

                if (!status.ok()) continue;
                if (resp.status() == kvstore::PutResponse::WRONG_LEADER) continue;

                m_leaderHint = idx;
                return resp.status() == kvstore::PutResponse::OK;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(kSleepMs));
        }
        return false;
    }

    bool append(const std::string& key, const std::string& value)
    {
        const int n = static_cast<int>(m_stubs.size());
        for (int round = 0; round < kMaxRounds; ++round)
        {
            for (int attempt = 0; attempt < n; ++attempt)
            {
                int idx = (m_leaderHint + attempt) % n;

                grpc::ClientContext ctx;
                kvstore::AppendRequest req;
                req.set_key(key);
                req.set_value(value);
                kvstore::AppendResponse resp;

                grpc::Status status = m_stubs[idx]->Append(&ctx, req, &resp);

                if (!status.ok()) continue;
                if (resp.status() == kvstore::AppendResponse::WRONG_LEADER) continue;

                m_leaderHint = idx;
                return resp.status() == kvstore::AppendResponse::OK;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(kSleepMs));
        }
        return false;
    }

private:
    std::vector<std::unique_ptr<kvstore::KVStore::Stub>> m_stubs;
    int m_leaderHint{0};
};
