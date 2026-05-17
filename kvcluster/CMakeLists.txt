add_executable(kvnode
    main.cpp
    raft/raft.cpp
    raft/persister.cpp
    kvserver/kvserver.cpp
)

target_include_directories(kvnode PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}          # kvcluster/ — resolves raft/, transport/, kvserver/, backends/
    ${CMAKE_SOURCE_DIR}/include          # json.hpp
    ${CMAKE_BINARY_DIR}/proto_gen        # generated raft.pb.h, kvstore.pb.h
)

target_link_libraries(kvnode PRIVATE
    kvstore_proto
    raft_proto
    gRPC::grpc++
    Threads::Threads
)

target_compile_options(kvnode PRIVATE -fsanitize=address -g)
target_link_options(kvnode PRIVATE -fsanitize=address)
