#pragma once

#include "json.hpp"
#include "models.hpp"



// =======================================================================
// GET RPC Serialization (KVServer)
// =======================================================================

inline void decodeArgs(const std::string& args, models::GetArgs& a)
{
    nlohmann::json j = nlohmann::json::parse(args);
    a.m_key      = j["Key"].get<std::string>();
    a.m_clientID = j["ClientID"].get<int64_t>();
    a.m_seq      = j["Seq"].get<int>();
}

inline std::string encodeArgs(const models::GetArgs& a)
{
    nlohmann::json j;
    j["Key"]      = a.m_key;
    j["ClientID"] = a.m_clientID;
    j["Seq"]      = a.m_seq;
    return j.dump();
}

inline void decodeReply(const std::string& replyStr, models::GetReply& r)
{
    nlohmann::json j = nlohmann::json::parse(replyStr);
    r.m_err   = j["Err"].get<std::string>();
    r.m_value = j["Value"].get<std::string>();
}

inline std::string encodeReply(const models::GetReply& r)
{
    nlohmann::json j;
    j["Err"]   = r.m_err;
    j["Value"] = r.m_value;
    return j.dump();
}

// =======================================================================
// PUT/APPEND RPC Serialization (KVServer)
// =======================================================================

inline void decodeArgs(const std::string& args, models::PutAppendArgs& a)
{
    nlohmann::json j = nlohmann::json::parse(args);
    a.m_key       = j["Key"].get<std::string>();
    a.m_value     = j["Value"].get<std::string>();
    a.m_operation = j["Operation"].get<std::string>();
    a.m_clientID  = j["ClientID"].get<int64_t>();
    a.m_seq       = j["Seq"].get<int>();
}

inline std::string encodeArgs(const models::PutAppendArgs& a)
{
    nlohmann::json j;
    j["Key"]       = a.m_key;
    j["Value"]     = a.m_value;
    j["Operation"] = a.m_operation;
    j["ClientID"]  = a.m_clientID;
    j["Seq"]       = a.m_seq;
    return j.dump();
}

inline void decodeReply(const std::string& replyStr, models::PutAppendReply& r)
{
    nlohmann::json j = nlohmann::json::parse(replyStr);
    r.m_err = j["Err"].get<std::string>();
}

inline std::string encodeReply(const models::PutAppendReply& r)
{
    nlohmann::json j;
    j["Err"] = r.m_err;
    return j.dump();
}


// =======================================================================
// JSON Serialization & Deserialization Helpers
// =======================================================================
inline std::string serializeOp(const models::Op& op)
{
    nlohmann::json j;
    j["m_operation"] = op.m_operation;
    j["m_key"]       = op.m_key;
    j["m_value"]     = op.m_value;
    j["m_clientID"]  = op.m_clientID;
    j["m_seq"]       = op.m_seq;

    return j.dump();
}

inline models::Op deserializeOp(const std::string& data)
{
    models::Op op;
    try
    {
        nlohmann::json j = nlohmann::json::parse(data);

        op.m_operation = j.value("m_operation", "");
        op.m_key       = j.value("m_key", "");
        op.m_value     = j.value("m_value", "");
        op.m_clientID  = j.value("m_clientID", 0ULL);
        op.m_seq       = j.value("m_seq", 0);
    }
    catch (const nlohmann::json::exception& e)
    {
        std::cerr << "[KVServer] CRITICAL: JSON Decode error in deserializeOp: "
                << e.what() << "\nData was: " << data << std::endl;
    }

    return op;
}
