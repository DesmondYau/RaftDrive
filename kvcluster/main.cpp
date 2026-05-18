#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <grpcpp/grpcpp.h>

#include "service_kv_grpc.hpp"
#include "service_raft_grpc.hpp"
#include "backends/backend_kvRaft.hpp"
#include "kvserver/kvserver.hpp"
#include "raft/persister.hpp"
#include "transport/client_raft_grpc.hpp"

// Usage: kvnode <nodeId> <listenAddr> <peer0Addr> <peer1Addr> [...]
// e.g.:  kvnode 0 0.0.0.0:50050 kvnode-1:50050 kvnode-2:50050

int main(int argc, char** argv)
{
    std::cout << std::unitbuf;  // flush after every write (Docker pipes buffer by default)
    if (argc < 4)
    {
        std::cerr << "Usage: kvnode <nodeId> <listenAddr> <peer0> [peer1...]\n";
        return 1;
    }

    int         nodeId     = std::atoi(argv[1]);
    std::string listenAddr = argv[2];

    // Raft::m_peers must be indexed by node ID (size = total nodes, nullptr at own slot).
    // argv[3..] lists all peer addresses in ascending node-ID order, skipping self.
    // We reconstruct the full indexed array by inserting nullptr at position nodeId.
    int totalNodes = (argc - 3) + 1;  // peers given + self
    std::vector<std::shared_ptr<RaftClient>> transports(totalNodes, nullptr);
    int peerArgIdx = 0;
    for (int i = 0; i < totalNodes; ++i)
    {
        if (i == nodeId) continue;          // leave nullptr at own slot
        transports[i] = std::make_shared<GrpcRaftClient>(argv[3 + peerArgIdx]);
        ++peerArgIdx;
    }

    auto persister = std::make_shared<Persister>();
    auto kvServer  = std::make_shared<KVServer>(nodeId, -1, persister, std::move(transports));
    auto backend   = std::make_shared<DistributedRaftBackend>(kvServer);

    KVStoreServiceImpl kvService(backend);
    RaftServiceImpl    raftService(kvServer->getRaft());

    grpc::ServerBuilder builder;
    builder.AddListeningPort(listenAddr, grpc::InsecureServerCredentials());
    builder.RegisterService(&kvService);
    builder.RegisterService(&raftService);

    auto server = builder.BuildAndStart();
    std::cout << "[kvnode-" << nodeId << "] listening on " << listenAddr << "\n";
    server->Wait();

    return 0;
}
