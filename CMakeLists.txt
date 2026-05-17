cmake_minimum_required(VERSION 3.20)
project(RaftDrive CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CROW_INCLUDE "/usr/local/include")

find_package(Threads REQUIRED)
find_package(Protobuf REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(absl REQUIRED)
find_package(utf8_range REQUIRED)
find_package(AWSSDK REQUIRED COMPONENTS s3)

set(PROTO_GEN_DIR "${CMAKE_BINARY_DIR}/proto_gen")
file(MAKE_DIRECTORY ${PROTO_GEN_DIR})

get_target_property(GRPC_CPP_PLUGIN gRPC::grpc_cpp_plugin LOCATION)

# ── kvstore.proto ─────────────────────────────────────────────────────────────
set(KVSTORE_PROTO_FILE "${CMAKE_SOURCE_DIR}/proto/kvstore.proto")

add_custom_command(
    OUTPUT
        ${PROTO_GEN_DIR}/kvstore.pb.cc
        ${PROTO_GEN_DIR}/kvstore.pb.h
        ${PROTO_GEN_DIR}/kvstore.grpc.pb.cc
        ${PROTO_GEN_DIR}/kvstore.grpc.pb.h
    COMMAND protobuf::protoc
        --grpc_out=${PROTO_GEN_DIR}
        --cpp_out=${PROTO_GEN_DIR}
        --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
        -I${CMAKE_SOURCE_DIR}/proto
        ${KVSTORE_PROTO_FILE}
    DEPENDS ${KVSTORE_PROTO_FILE}
)

add_library(kvstore_proto STATIC
    ${PROTO_GEN_DIR}/kvstore.pb.cc
    ${PROTO_GEN_DIR}/kvstore.grpc.pb.cc
)
target_include_directories(kvstore_proto PUBLIC ${PROTO_GEN_DIR})
target_link_libraries(kvstore_proto PUBLIC
    gRPC::grpc++
    protobuf::libprotobuf
    absl::log
    absl::log_internal_message
    absl::log_internal_check_op
    absl::status
    absl::statusor
    utf8_range::utf8_validity
    utf8_range::utf8_range
)

# ── raft.proto ────────────────────────────────────────────────────────────────
set(RAFT_PROTO_FILE "${CMAKE_SOURCE_DIR}/proto/raft.proto")

add_custom_command(
    OUTPUT
        ${PROTO_GEN_DIR}/raft.pb.cc
        ${PROTO_GEN_DIR}/raft.pb.h
        ${PROTO_GEN_DIR}/raft.grpc.pb.cc
        ${PROTO_GEN_DIR}/raft.grpc.pb.h
    COMMAND protobuf::protoc
        --grpc_out=${PROTO_GEN_DIR}
        --cpp_out=${PROTO_GEN_DIR}
        --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
        -I${CMAKE_SOURCE_DIR}/proto
        ${RAFT_PROTO_FILE}
    DEPENDS ${RAFT_PROTO_FILE}
)

add_library(raft_proto STATIC
    ${PROTO_GEN_DIR}/raft.pb.cc
    ${PROTO_GEN_DIR}/raft.grpc.pb.cc
)
target_include_directories(raft_proto PUBLIC ${PROTO_GEN_DIR})
target_link_libraries(raft_proto PUBLIC
    gRPC::grpc++
    protobuf::libprotobuf
    absl::log
    absl::log_internal_message
    absl::log_internal_check_op
    absl::status
    absl::statusor
    utf8_range::utf8_validity
    utf8_range::utf8_range
)

add_subdirectory(kvcluster)
add_subdirectory(raftdrive)
