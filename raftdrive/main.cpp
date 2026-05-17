#include <iostream>
#include <cstdlib>
#include "services/service_metadata.hpp"
#include "clients/kv_grpc_client.hpp"

static void check(const std::string& label, bool pass) {
    std::cout << (pass ? "[PASS] " : "[FAIL] ") << label << "\n";
}

int main() {
    std::cout << std::unitbuf;  // flush after every write (Docker pipes buffer by default)
    const char* target = std::getenv("RAFTDRIVE_KV_TARGETS");
    if (!target) target = "localhost:50050";

    std::cout << "[raftdrive-test] connecting to kvnodes at " << target << "\n";

    KVStoreClient kv(target);
    MetadataService meta(kv);

    // 1. ensureRoot — idempotent, must not throw
    meta.ensureRoot();
    auto root = meta.getDir("/");
    check("ensureRoot: root dir exists", root.has_value());
    if (root) check("ensureRoot: root path is '/'", root->path == "/");

    // 2. createDir
    meta.createDir("/TestDir");
    auto dir = meta.getDir("/TestDir");
    check("createDir: dir exists", dir.has_value());
    if (dir) check("createDir: name correct", dir->name == "TestDir");
    if (dir) check("createDir: parent is '/'", dir->parent == "/");

    // 3. root children include TestDir
    auto rootChildren = meta.getChildren("/");
    bool hasTestDir = false;
    for (const auto& c : rootChildren) if (c == "TestDir") hasTestDir = true;
    check("getChildren: TestDir in root children", hasTestDir);

    // 4. createFile inside TestDir
    FileMeta f;
    f.name = "hello.txt";
    f.path = "/TestDir/hello.txt";
    f.parent = "/TestDir";
    f.size = 42;
    f.content_type = "text/plain";
    f.s3_key = "files/test-uuid-hello.txt";
    meta.createFile(f);

    auto file = meta.getFile("/TestDir/hello.txt");
    check("createFile: file exists", file.has_value());
    if (file) check("createFile: name correct", file->name == "hello.txt");
    if (file) check("createFile: s3_key correct", file->s3_key == "files/test-uuid-hello.txt");

    // 5. TestDir children include hello.txt
    auto dirChildren = meta.getChildren("/TestDir");
    bool hasFile = false;
    for (const auto& c : dirChildren) if (c == "hello.txt") hasFile = true;
    check("getChildren: hello.txt in TestDir children", hasFile);

    // 6. deleteFile
    meta.deleteFile("/TestDir/hello.txt");
    check("deleteFile: file gone", !meta.getFile("/TestDir/hello.txt").has_value());

    dirChildren = meta.getChildren("/TestDir");
    bool fileGone = true;
    for (const auto& c : dirChildren) if (c == "hello.txt") { fileGone = false; break; }
    check("deleteFile: removed from parent children", fileGone);

    // 7. deleteDir
    meta.deleteDir("/TestDir");
    check("deleteDir: dir gone", !meta.getDir("/TestDir").has_value());

    rootChildren = meta.getChildren("/");
    bool dirGone = true;
    for (const auto& c : rootChildren) if (c == "TestDir") { dirGone = false; break; }
    check("deleteDir: removed from root children", dirGone);

    std::cout << "[raftdrive-test] done.\n";
    return 0;
}