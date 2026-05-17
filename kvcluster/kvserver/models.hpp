#pragma once

#include <string>
#include <vector>
#include <map>
#include <sstream>
#include "json.hpp"

namespace models
{

struct GetArgs
{
    std::string m_key;
    uint64_t m_clientID;
    int m_seq;
};


struct GetReply
{
    std::string m_value;
    std::string m_err;
};


struct PutAppendArgs
{
    std::string m_key;
    std::string m_value;
    std::string m_operation;
    uint64_t m_clientID;
    int m_seq;
};

struct PutAppendReply
{
    std::string m_value;
    std::string m_err;
};


struct Op
{
    std::string m_operation;
    std::string m_key;
    std::string m_value;
    uint64_t m_clientID;
    int m_seq;
};


struct KvInput
{
    uint8_t m_operation; // 0 => Get, 1 => Put, 2 => Append
    std::string m_key;
    std::string m_value;
};

struct KvOutput
{
    std::string m_value;
};

struct KvModel
{
    static std::string Init()
    {
        return "";
    }

    static std::pair<bool, std::string> Step(const std::string& state, const KvInput& input, const KvOutput& output)
    {
        if (input.m_operation == 0)
        {
            return {output.m_value == state, state};
        }
        else if (input.m_operation == 1)
        {
            return {true, input.m_value};
        }
        else if (input.m_operation == 2)
        {
            return {true, state + input.m_value};
        }
        else
        {
            return {output.m_value == state, state + input.m_value};
        }
    }

    static std::string DescribeOperation(const KvInput& input, const KvOutput& output)
    {
        std::ostringstream oss;
        switch (input.m_operation)
        {
            case 0:
                oss << "get('" << input.m_key << "') -> '" << output.m_value << "'";
                break;
            case 1:
                oss << "put('" << input.m_key << "', '" << input.m_value << "')";
                break;
            case 2:
                oss << "append('" << input.m_key << "', '" << input.m_value << "')";
                break;
            default:
                oss << "<invalid>";
                break;
        }
        return oss.str();
    }

    template <typename OpType>
    static std::vector<std::vector<OpType>> Partition(const std::vector<OpType>& history)
    {
        std::map<std::string, std::vector<OpType>> grouped_history;

        for (const auto& op : history) {
            grouped_history[op.m_input.m_key].push_back(op);
        }

        std::vector<std::vector<OpType>> ret;
        ret.reserve(grouped_history.size());

        for (auto const& [key, ops] : grouped_history) {
            ret.push_back(ops);
        }

        return ret;
    }
};

} // namespace models
