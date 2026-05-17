#include "service_metadata.hpp"
#include "../../include/json.hpp"
#include <ctime>

MetadataService::MetadataService(KVStoreClient& kvStoreClient)
    : m_kvStoreClient {kvStoreClient}
{}
    
void MetadataService::ensureRoot()
{
    if (!getDir("/").has_value())
        createDir("/");
}

std::optional<DirMeta> MetadataService::getDir(const std::string& path)
{
    
    std::string result = m_kvStoreClient.get(dirKey(path));
    if (result.empty())
        return std::nullopt;


    DirMeta dirMeta;
    nlohmann::json j = nlohmann::json::parse(result);
    dirMeta.name           = j["name"].get<std::string>();
    dirMeta.path           = j["path"].get<std::string>();
    dirMeta.parent         = j["parent"].get<std::string>();
    dirMeta.created_at     = j["created_at"].get<int64_t>();
    dirMeta.modified_at    = j["modified_at"].get<int64_t>();

    return dirMeta;
}

void MetadataService::createDir(const std::string& path)
{
    // Extract name and parent path from input
    std::string name = path.substr(path.rfind('/') + 1);
    std::string parent = path.substr(0, path.rfind('/'));
    if (parent.empty())
        parent = "/";

    int64_t now = std::time(nullptr);

    // 1. Write dir entry
    nlohmann::json j;
    j["name"] = name;
    j["path"] = path;
    j["parent"] = parent;
    j["created_at"] = now;
    j["modified_at"] = now;
    m_kvStoreClient.put(dirKey(path), j.dump());

    // 2. Create childrenKey for this new path
    m_kvStoreClient.put(childrenKey(path), "[]");

    // 3. Register this dir as child of parent;
    createChild(parent, name);    
}

void MetadataService::deleteDir(const std::string& path)
{
    if (!getDir(path).has_value()) 
        return;

    // extract parent and name to remove from parent's children list
    std::string name   = path.substr(path.rfind('/') + 1);
    std::string parent = path.substr(0, path.rfind('/'));
    if (parent.empty()) 
        parent = "/";

    m_kvStoreClient.put(dirKey(path), "");       // clear dir entry
    m_kvStoreClient.put(childrenKey(path), "");  // clear children list
    deleteChild(parent, name);                   // remove from parent
}

std::optional<FileMeta> MetadataService::getFile(const std::string& path)
{
    std::string result = m_kvStoreClient.get(fileKey(path));
    if (result.empty())
        return std::nullopt;

    FileMeta fileMeta;
    nlohmann::json j = nlohmann::json::parse(result);
    fileMeta.name         = j["name"].get<std::string>();
    fileMeta.path         = j["path"].get<std::string>();
    fileMeta.parent       = j["parent"].get<std::string>();
    fileMeta.size         = j["size"].get<int64_t>();
    fileMeta.content_type = j["content_type"].get<std::string>();
    fileMeta.s3_key       = j["s3_key"].get<std::string>();
    fileMeta.created_at   = j["created_at"].get<int64_t>();
    fileMeta.modified_at  = j["modified_at"].get<int64_t>();
    return fileMeta;
}

void MetadataService::createFile(const FileMeta& fileMeta)
{
    int64_t now = std::time(nullptr);

    nlohmann::json j;
    j["name"]         = fileMeta.name;
    j["path"]         = fileMeta.path;
    j["parent"]       = fileMeta.parent;
    j["size"]         = fileMeta.size;
    j["content_type"] = fileMeta.content_type;
    j["s3_key"]       = fileMeta.s3_key;
    j["created_at"]   = fileMeta.created_at  ? fileMeta.created_at  : now;
    j["modified_at"]  = fileMeta.modified_at ? fileMeta.modified_at : now;
    m_kvStoreClient.put(fileKey(fileMeta.path), j.dump());

    // Create child for the parent of file
    createChild(fileMeta.parent, fileMeta.name);
}

void MetadataService::deleteFile(const std::string& path)
{
    if (!getFile(path).has_value())
        return;

    std::string name   = path.substr(path.rfind('/') + 1);
    std::string parent = path.substr(0, path.rfind('/'));
    if (parent.empty()) parent = "/";

    m_kvStoreClient.put(fileKey(path), "");
    deleteChild(parent, name);
}

std::vector<std::string> MetadataService::getChildren(const std::string& parentPath)
{
    std::string result = m_kvStoreClient.get(childrenKey(parentPath));
    if (result.empty())
        return {};

    nlohmann::json j = nlohmann::json::parse(result);
    return j.get<std::vector<std::string>>();
    
}

void MetadataService::createChild(const std::string& parentPath, const std::string& childName)
{
    auto children = getChildren(parentPath);
    children.push_back(childName);

    nlohmann::json j = children;
    m_kvStoreClient.put(childrenKey(parentPath), j.dump());
}

void MetadataService::deleteChild(const std::string& parentPath, const std::string& childName)
{
    auto children = getChildren(parentPath);
    std::erase(children, childName);

    nlohmann::json j = children;
    m_kvStoreClient.put(childrenKey(parentPath), j.dump());
}


std::string MetadataService::dirKey(const std::string& path)
{
    return "dir:" + path;
}

std::string MetadataService::fileKey(const std::string& path)
{
    return "file:" + path;
}

std::string MetadataService::childrenKey(const std::string& path)
{
    return "children:" + path;
}