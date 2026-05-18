#pragma once

#include <string>
#include "service_metadata.hpp"
#include "service_storage.hpp"
#include "../models/drive_models.hpp"

struct DownloadResult
{
    std::string data;
    std::string contentType;
};

class FileSystemService
{
public:
    FileSystemService(StorageService& storageService, MetadataService& metadataService);

    void ensureRoot();

    // Directory ops
    ListingResult listDir(const std::string& path);
    void createDir(const std::string& path);
    void deleteDir(const std::string& path);

    // File ops
    DownloadResult downloadFile(const std::string& path);
    FileMeta uploadFile(const std::string& path, const std::string& data, const std::string& contentType);
    void deleteFile(const std::string& path);


private:
    static std::string generateS3Key(const std::string& filename);

    StorageService& m_storageService;
    MetadataService& m_metadataService;
};