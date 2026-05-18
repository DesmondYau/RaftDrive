#pragma once

#include <string>
#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/DeleteObjectRequest.h>

class StorageService
{
public:
    StorageService(const std::string& bucketName, const std::string& addr, const std::string& region);

    void putObject(const std::string& key, const std::string& data, const std::string& contentType);
    std::string getObject(const std::string& key);
    void deleteObject(const std::string& key);

private:
    Aws::S3::S3Client m_client;
    std::string m_bucket;
};