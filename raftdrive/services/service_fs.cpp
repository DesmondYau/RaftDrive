#include <filesystem>
#include <chrono>
#include <sstream>
#include <random>
#include <iomanip>
#include <ctime>

#include "service_fs.hpp"

FileSystemService::FileSystemService(StorageService& storageService, MetadataService& metadataService)
    : m_storageService(storageService), m_metadataService(metadataService)
{}

void FileSystemService::ensureRoot()
{
    m_metadataService.ensureRoot();
}

ListingResult FileSystemService::listDir(const std::string& path)
{
    // Check if directory exist.Handler should catch and handle exception
    auto dirOpt = m_metadataService.getDir(path);
    if (!dirOpt.has_value())
        throw std::runtime_error("Directory not found: " + path);
    
    // If exist, get dirMeta and children names for the directory
    DirMeta dirMeta = *dirOpt;
    auto childrenNames = m_metadataService.getChildren(path);

    // Get dirMeta or fileMeta from the vector of children
    std::vector<FsItem> children;
    for (auto& childName : childrenNames)
    {
        std::string childPath = (path == "/") ? "/" + childName : path + "/" + childName;

        // Check if child is a file
        auto file = m_metadataService.getFile(childPath);
        if (file.has_value())
        {
            children.push_back(*file);
            continue;
        }

        // Otherwise, check if child is a directory
        auto dir = m_metadataService.getDir(childPath);
        if (dir.has_value())
        {
            children.push_back(*dir);
            continue;
        }

    }

    return {dirMeta, children};
}

void FileSystemService::createDir(const std::string& path)
{
    // Check if directory exist.Handler should catch and handle exception
    auto dirOpt = m_metadataService.getDir(path);
    if (dirOpt.has_value())
        throw std::runtime_error("Directory exists already: " + path);

    m_metadataService.createDir(path);
}

void FileSystemService::deleteDir(const std::string& path)
{
    // Check if directory exist.Handler should catch and handle exception
    auto dirOpt = m_metadataService.getDir(path);
    if (!dirOpt.has_value())
        throw std::runtime_error("Directory not found: " + path);

    // If exist, get children names for the directory
    auto childrenNames = m_metadataService.getChildren(path);

    // Recursively delete children first
    for (auto& childName : childrenNames)
    {
        std::string childPath = (path == "/") ? "/" + childName : path + "/" + childName;

        // If child is file
        auto file = m_metadataService.getFile(childPath);
        if (file.has_value())
        {
            deleteFile(childPath);
            continue;
        }

        // Otherwise, check if child is a directory
        auto dir = m_metadataService.getDir(childPath);
        if (dir.has_value())
        {
            deleteDir(childPath);
        }
    }

    m_metadataService.deleteDir(path);

}

DownloadResult FileSystemService::downloadFile(const std::string& path)
{
    auto outcome = m_metadataService.getFile(path);

    if (!outcome.has_value())
        throw std::runtime_error("File not found: " + path);

    return { m_storageService.getObject(outcome->s3_key), outcome->content_type };
}

FileMeta FileSystemService::uploadFile(const std::string& path, const std::string& data, const std::string& contentType)
{
    auto outcome = m_metadataService.getFile(path);

    // Check if file exist
    if (outcome.has_value())
    {
        throw std::runtime_error("File already exists: " + path);
    }

    // Parse argument
    std::string name   = path.substr(path.rfind('/') + 1);
    std::string parent = path.substr(0, path.rfind('/'));
    if (parent.empty()) 
        parent = "/";
    int64_t now = static_cast<int64_t>(std::time(nullptr));

    // Generate S3 key from filename
    std::string s3key = generateS3Key(name);

    // Store object on S3
    m_storageService.putObject(s3key, data, contentType);

    // Build FileMeta and save metadata
    FileMeta fileMeta { name, path, parent, contentType, s3key, std::ssize(data), now, now };
    m_metadataService.createFile(fileMeta);

    return fileMeta; 

}

void FileSystemService::deleteFile(const std::string& path)
{
    auto outcome = m_metadataService.getFile(path);

    // Check if file exist
    if (!outcome.has_value())
    {
        throw std::runtime_error("File not found: " + path);
    }

    // Delete the file object on S3
    m_storageService.deleteObject(outcome->s3_key);

    // Delete the file metadata
    m_metadataService.deleteFile(path);
}


std::string FileSystemService::generateS3Key(const std::string& filename)
{
    std::mt19937_64 eng(std::random_device{}());
    std::uniform_int_distribution<uint64_t> dist;
    std::ostringstream oss;
    oss << "files/" << std::hex << dist(eng) << "-" << filename;
    return oss.str();
}