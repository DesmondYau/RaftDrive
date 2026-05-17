file(GLOB_RECURSE RAFTDRIVE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/services/*.cpp"
)

add_executable(raftdrive ${RAFTDRIVE_SOURCES})

target_include_directories(raftdrive PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CROW_INCLUDE}
    ${AWSSDK_INCLUDE_DIRS}
)

target_link_libraries(raftdrive PRIVATE
    kvstore_proto
    gRPC::grpc++
    ${AWSSDK_LINK_LIBRARIES}
    Threads::Threads
)
