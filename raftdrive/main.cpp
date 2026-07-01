#include <iostream>
#include <cstdlib>
#include <aws/core/Aws.h>
#include "crow.h"
#include "clients/kv_grpc_client.hpp"
#include "services/service_metadata.hpp"
#include "services/service_storage.hpp"
#include "services/service_fs.hpp"
#include "api/router.hpp"

int main()
{
    std::cout << std::unitbuf;

    // 1. Read environment variables
    const char* t = std::getenv("RAFTDRIVE_KV_TARGETS");
    const char* b = std::getenv("RAFTDRIVE_S3_BUCKET");
    const char* e = std::getenv("RAFTDRIVE_S3_ENDPOINT");
    const char* r = std::getenv("RAFTDRIVE_S3_REGION");
    const char* p = std::getenv("RAFTDRIVE_PORT");

    const std::string kvTargets = t ? t : "localhost:50050";
    const std::string bucket    = b ? b : "raftdrive-objects";
    const std::string endpoint  = e ? e : "";
    const std::string region    = r ? r : "us-east-1";
    const int         port      = p ? std::atoi(p) : 8080;

    // 2. Init AWS SDK
    Aws::SDKOptions options;
    Aws::InitAPI(options);

    // 3. Construct services in order
    KVStoreClient kv {kvTargets};
    MetadataService meta {kv};
    StorageService storage {bucket, endpoint, region};
    FileSystemService fs {storage, meta};

    // 4. Ensure root dir exists
    fs.ensureRoot();

    // 5. Set up Crow and register routes
    crow::SimpleApp app;
    registerRoutes(app, fs);

    // 6. Start server
    app.port(port).multithreaded().run();

    // 7. Shutdown AWS SDK after server exits
    Aws::ShutdownAPI(options);

    return 0;
}
