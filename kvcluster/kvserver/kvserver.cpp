#include <iostream>
#include "kvserver.hpp"
#include "kvhelper.hpp"
#include "models.hpp"
#include "raft/logger.hpp"
#include "raft/persister.hpp"


KVServer::KVServer(int id, int maxRaftState, std::shared_ptr<Persister> persister,
                   std::vector<std::shared_ptr<RaftClient>> transports)
    : m_id {id}
    , m_maxRaftState {maxRaftState}
    , m_dead {false}
{
    auto applyChannel = std::make_shared<ApplyChannel>();
    auto logger = std::make_shared<Logger>();

    m_logger = logger;

    m_raft = std::make_unique<Raft>(std::move(transports), m_id, persister, applyChannel, logger);

    m_applyChannel = applyChannel;

    m_applierThread = std::thread(&KVServer::applierLoop, this);
}

KVServer::~KVServer()
{
    kill();
}

void KVServer::applierLoop()
{
    while (!m_dead)
    {
        auto msgOpt = m_applyChannel->pop();

        if (!msgOpt.has_value())
        {
            break;
        }

        auto applyMsg = msgOpt.value();

        if (applyMsg.CommandValid)
        {
            std::lock_guard<std::mutex> lock{m_mu};

            models::Op op = deserializeOp(applyMsg.Command);

            if (op.m_operation == "Get")
            {
                if (m_kvStore.contains(op.m_key))
                {
                    op.m_value = m_kvStore[op.m_key];
                }
                else
                {
                    op.m_value = "";
                }
            }
            else if (op.m_operation == "Append")
            {
                if (!(m_clientSeqMap.contains(op.m_clientID) && m_clientSeqMap[op.m_clientID] >= op.m_seq))
                {
                    m_kvStore[op.m_key] += op.m_value;
                    m_clientSeqMap[op.m_clientID] = op.m_seq;

                    m_logger->logKVServer(LogLevel::INFO, LogEvent(LogEvent::Type::KV_STATEMACHINE, m_id,
                        "Applied Append. Key: " + op.m_key + " Current Value: " + m_kvStore[op.m_key]));
                }
            }
            else if (op.m_operation == "Put")
            {
                if (!(m_clientSeqMap.contains(op.m_clientID) && m_clientSeqMap[op.m_clientID] >= op.m_seq))
                {
                    m_kvStore[op.m_key] = op.m_value;
                    m_clientSeqMap[op.m_clientID] = op.m_seq;

                    m_logger->logKVServer(LogLevel::INFO, LogEvent(LogEvent::Type::KV_STATEMACHINE, m_id,
                        "Applied Put. Key: " + op.m_key + " New Value: " + op.m_value));
                }
            }

            if (m_waitMap.contains(applyMsg.CommandIndex))
            {
                m_logger->logKVServer(LogLevel::DEBUG, LogEvent(LogEvent::Type::KV_STATEMACHINE, m_id,
                    "Fulfilling promise at log index: " + std::to_string(applyMsg.CommandIndex)));

                m_waitMap[applyMsg.CommandIndex].set_value(op);
                m_waitMap.erase(applyMsg.CommandIndex);
            }
        }
    }
}

void KVServer::get(const models::GetArgs& args, models::GetReply& reply)
{
    std::unique_lock<std::mutex> lock(m_mu);

    m_logger->logKVServer(LogLevel::INFO, LogEvent(LogEvent::Type::KV_REQUEST, m_id,
        "Received GET request for key: " + args.m_key + " from Client: " + std::to_string(args.m_clientID) + " Seq: " + std::to_string(args.m_seq)));

    models::Op op {"Get", args.m_key, "", args.m_clientID, args.m_seq};
    auto [idx, term, isLeader] = m_raft->start(serializeOp(op));

    if (!isLeader)
    {
        reply.m_err = "ErrWrongLeader";
        return;
    }

    std::promise<models::Op> prom;
    auto fut = prom.get_future();
    m_waitMap[idx] = std::move(prom);

    lock.unlock();
    auto status = fut.wait_for(std::chrono::milliseconds(500));
    lock.lock();

    m_waitMap.erase(idx);

    if (m_dead)
    {
        reply.m_err = "ErrWrongLeader";
        return;
    }

    if (status == std::future_status::ready)
    {
        models::Op committedOp;
        try
        {
            committedOp = fut.get();
        }
        catch(const std::exception& e)
        {
            reply.m_err = "ErrWrongLeader";
            return;
        }

        if (committedOp.m_clientID == args.m_clientID && committedOp.m_seq == args.m_seq)
        {
            if (committedOp.m_value != "")
            {
                reply.m_value = committedOp.m_value;
                reply.m_err = "OK";
                m_logger->logKVServer(LogLevel::INFO, LogEvent(LogEvent::Type::KV_REPLY, m_id, "Replied OK to GET key: " + args.m_key));
            }
            else
            {
                reply.m_err = "ErrNoKey";
                m_logger->logKVServer(LogLevel::WARN, LogEvent(LogEvent::Type::KV_REPLY, m_id, "Replied ErrNoKey to GET key: " + args.m_key));
            }
        }
        else
        {
            reply.m_err = "ErrWrongLeader";
            m_logger->logKVServer(LogLevel::WARN, LogEvent(LogEvent::Type::KV_ERROR, m_id, "Wrong Command Trap caught during GET! Expected Seq: " + std::to_string(args.m_seq)));
        }
    }
    else
    {
        reply.m_err = "ErrWrongLeader";
        m_logger->logKVServer(LogLevel::ERROR, LogEvent(LogEvent::Type::KV_ERROR, m_id, "Timeout waiting for Raft consensus on GET key: " + args.m_key));
    }
}

void KVServer::putAppend(const models::PutAppendArgs& args, models::PutAppendReply& reply)
{
    std::unique_lock<std::mutex> lock(m_mu);

    m_logger->logKVServer(LogLevel::INFO, LogEvent(LogEvent::Type::KV_REQUEST, m_id,
        "Received " + args.m_operation + " request for key: " + args.m_key + " from Client: " + std::to_string(args.m_clientID) + " Seq: " + std::to_string(args.m_seq)));

    models::Op op {args.m_operation, args.m_key, args.m_value, args.m_clientID, args.m_seq};
    auto [idx, term, isLeader] = m_raft->start(serializeOp(op));

    if (!isLeader)
    {
        reply.m_err = "ErrWrongLeader";
        return;
    }

    if (m_clientSeqMap.contains(args.m_clientID) && m_clientSeqMap[args.m_clientID] >= args.m_seq)
    {
        m_logger->logKVServer(LogLevel::INFO, LogEvent(LogEvent::Type::KV_REPLY, m_id, "Duplicate request detected. Returning early OK."));
        reply.m_err = "OK";
        return;
    }

    std::promise<models::Op> prom;
    auto fut = prom.get_future();
    m_waitMap[idx] = std::move(prom);

    lock.unlock();
    auto status = fut.wait_for(std::chrono::milliseconds(500));
    lock.lock();

    m_waitMap.erase(idx);

    if (m_dead == true)
    {
        reply.m_err = "ErrWrongLeader";
        return;
    }

    if (status == std::future_status::ready)
    {
        models::Op committedOp;
        try
        {
            committedOp = fut.get();
        }
        catch(const std::exception& e)
        {
            reply.m_err = "ErrWrongLeader";
            return;
        }

        if (committedOp.m_clientID == args.m_clientID && committedOp.m_seq == args.m_seq)
        {
            reply.m_err = "OK";
            m_logger->logKVServer(LogLevel::INFO, LogEvent(LogEvent::Type::KV_REPLY, m_id, "Replied OK to " + args.m_operation + " key: " + args.m_key));
        }
        else
        {
            reply.m_err = "ErrWrongLeader";
            m_logger->logKVServer(LogLevel::WARN, LogEvent(LogEvent::Type::KV_ERROR, m_id, "Wrong Command Trap caught during " + args.m_operation + "!"));
        }
    }
    else
    {
        reply.m_err = "ErrWrongLeader";
        m_logger->logKVServer(LogLevel::ERROR, LogEvent(LogEvent::Type::KV_ERROR, m_id, "Timeout waiting for Raft consensus on " + args.m_operation + " key: " + args.m_key));
    }
}

void KVServer::kill()
{
    {
        std::lock_guard<std::mutex> lock(m_mu);
        m_dead.store(true);
    }

    m_applyChannel->close();

    if (m_applierThread.joinable())
        m_applierThread.join();
}

Raft* KVServer::getRaft()
{
    return m_raft.get();
}
