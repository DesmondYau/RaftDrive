#pragma once

#include <string>
#include <vector>
#include <optional>
#include "../models/drive_models.hpp"
#include "../clients/kv_grpc_client.hpp"

class MetadataService
{
public:
    explicit MetadataService(KVStoreClient& kvStoreClient);
    
    void ensureRoot();

    std::optional<DirMeta> getDir(const std::string& path);
    void createDir(const std::string& path);
    void deleteDir(const std::string& path);

    std::optional<FileMeta> getFile(const std::string& path);
    void createFile(const FileMeta& fileMeta);
    void deleteFile(const std::string& path);

    std::vector<std::string> getChildren(const std::string& parentPath);
    void createChild(const std::string& parentPath, const std::string& childName);
    void deleteChild(const std::string& parentPath, const std::string& childName);

private:
    static std::string dirKey(const std::string& path);
    static std::string fileKey(const std::string& path);
    static std::string childrenKey(const std::string& path);

    KVStoreClient& m_kvStoreClient;                                     // backend service does not own kvStoreClient but borrow
};