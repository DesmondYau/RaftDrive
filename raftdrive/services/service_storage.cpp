#include "service_storage.hpp"
#include <sstream>



StorageService::StorageService(const std::string& bucketName, const std::string& addr, const std::string& region)
    : m_bucket { bucketName }
{
    // Set up AWS client config
    Aws::Client::ClientConfiguration cfg;
    cfg.region = region;

    bool useLocalStack = !addr.empty();
    if (useLocalStack)
        cfg.endpointOverride = addr;
    std::cerr << "[S3 init] bucket=" << bucketName << " region=" << region << " endpoint=" << addr << " useLocalStack=" << useLocalStack << "\n";

    // Virtual hosted-style URLs required for real S3; path-style needed for LocalStack
    m_client = Aws::S3::S3Client(cfg, Aws::Client::AWSAuthV4Signer::PayloadSigningPolicy::Never, !useLocalStack);

}

void StorageService::putObject(const std::string& key, const std::string& data, const std::string& contentType)
{
    // Wrap data in stringstream
    auto stream = std::make_shared<std::stringstream>(data);

    // Build request
    Aws::S3::Model::PutObjectRequest req;
    req.SetBucket(m_bucket);
    req.SetKey(key);
    req.SetBody(stream);
    req.SetContentType(contentType);
    std::cerr << "[S3 putObject] bucket=" << m_bucket << " key=" << key << "\n";

    auto outcome = m_client.PutObject(req);
    if (!outcome.IsSuccess())
    {
        auto& err = outcome.GetError();
        std::cerr << "[S3 putObject] code=" << static_cast<int>(err.GetErrorType())
                  << " msg=" << err.GetMessage()
                  << " remoteHost=" << err.GetRemoteHostIpAddress() << "\n";
        throw std::runtime_error(err.GetMessage());
    }

}

std::string StorageService::getObject(const std::string& key)
{
    // Build request
    Aws::S3::Model::GetObjectRequest req;
    req.SetBucket(m_bucket);
    req.SetKey(key);

    // Call GetObject
    auto outcome = m_client.GetObject(req);
    if (outcome.IsSuccess())
    {
        std::ostringstream oss;
        oss << outcome.GetResult().GetBody().rdbuf();
        return oss.str();
    }
    else
    {
        throw std::runtime_error(outcome.GetError().GetMessage());
    }
}

void StorageService::deleteObject(const std::string& key)
{
    // Build request
    Aws::S3::Model::DeleteObjectRequest req;
    req.SetBucket(m_bucket);
    req.SetKey(key);

    // Call DeleteObject
    auto outcome = m_client.DeleteObject(req);
    if (!outcome.IsSuccess())
        throw std::runtime_error(outcome.GetError().GetMessage());

}
