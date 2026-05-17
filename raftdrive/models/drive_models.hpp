#pragma once
#include <string>
#include <variant>
#include <vector>
#include <cstdint>

struct FileMeta 
{
    std::string name;                   // file name
    std::string path;                   // file path
    std::string parent;                 // Directory that contains it
    std::string content_type;           // MIME type
    std::string s3_key;                 // The object key in S3 
    int64_t     size{0};
    int64_t     created_at{0};
    int64_t     modified_at{0};
};

struct DirMeta 
{
    std::string name;
    std::string path;
    std::string parent;
    int64_t     created_at{0};
    int64_t     modified_at{0};
};

using FsItem = std::variant<FileMeta, DirMeta>;

struct ListingResult 
{
    DirMeta              dir;
    std::vector<FsItem>  children;
};
