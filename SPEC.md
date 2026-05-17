# RaftDrive — Complete Build Spec

## What We're Building

A cloud file drive (like Google Drive) with:
- **KVServer cluster** (your existing Lab4_KvRaft code) as a fault-tolerant metadata store
- **Amazon S3 / LocalStack** for actual file storage
- **gRPC** as the real network API between services
- **React + TypeScript** frontend for upload/download/browsing

---

## Architecture

```
Browser (React/TS on :5173)
        | /api/* (Vite proxy)
        ▼
raftdrive   (C++, Crow REST API on :8080)
    ├── REST handlers   →  fs_service
    ├── KvGrpcClient    →  kvcluster :50051
    └── StorageService  →  S3 / LocalStack :4566
        | gRPC over real TCP
        ▼
kvcluster   (C++, gRPC server on :50051)
    └── IKVBackend ← InProcessRaftBackend
            └── Config + Clerk  (Lab4_KvRaft, UNMODIFIED)
                    └── 3x Raft nodes (in-process, labrpc)
```

**Phase 2 swap** (when your new Raft is ready): replace `InProcessRaftBackend`
with `DistributedRaftBackend` in `kvcluster/main.cpp` — nothing else changes.

---

## Project File Layout

```
RaftDrive/
├── CMakeLists.txt
├── SPEC.md                          ← this file
├── docker-compose.yml
├── .env.example
├── scripts/
│   ├── start.sh
│   └── init-localstack.sh
├── proto/
│   └── kvstore.proto                ← gRPC service definition
├── third_party/
│   └── crow/                        ← git submodule: CrowCpp/Crow
├── kvcluster/
│   ├── CMakeLists.txt
│   ├── main.cpp
│   ├── kv_grpc_service.hpp
│   └── backends/
│       ├── ikv_backend.hpp          ← THE SWAP POINT (Phase 1 → Phase 2)
│       ├── inprocess_raft_backend.hpp
│       └── iraft_transport.hpp      ← forward doc for Phase 2
└── raftdrive/
    ├── CMakeLists.txt
    ├── main.cpp
    ├── models/
    │   └── drive_models.hpp
    ├── clients/
    │   └── kv_grpc_client.hpp
    ├── services/
    │   ├── metadata_service.hpp
    │   ├── metadata_service.cpp
    │   ├── storage_service.hpp
    │   ├── storage_service.cpp
    │   ├── fs_service.hpp
    │   └── fs_service.cpp
    ├── handlers/
    │   ├── dirs_handler.hpp
    │   ├── files_handler.hpp
    │   └── upload_handler.hpp
    └── api/
        └── router.hpp
frontend/
├── package.json
├── vite.config.ts
├── tailwind.config.js
└── src/
    ├── main.tsx
    ├── App.tsx
    ├── types/file.ts
    ├── api/driveApi.ts
    ├── store/driveStore.ts
    ├── hooks/
    │   ├── useDirListing.ts
    │   └── useUpload.ts
    └── components/
        ├── FileExplorer.tsx
        ├── BreadcrumbNav.tsx
        ├── FileRow.tsx
        ├── Toolbar.tsx
        ├── UploadButton.tsx
        └── ContextMenu.tsx
```

---

## Build Order (critical path)

1. `proto/kvstore.proto`
2. `CMakeLists.txt` (root + subfolders)
3. `kvcluster/` — the backend cluster
4. `raftdrive/` — the application service
5. `frontend/` — the React UI
6. `docker-compose.yml` + scripts

---

## Step 1 — Proto File (`proto/kvstore.proto`)

The proto defines the contract. Everything downstream depends on this.

```proto
syntax = "proto3";
package kvstore;

service KVStore {
    rpc Get    (GetRequest)    returns (GetResponse);
    rpc Put    (PutRequest)    returns (PutResponse);
    rpc Append (AppendRequest) returns (AppendResponse);
}

message GetRequest  { string key = 1; }
message GetResponse {
    string value  = 1;
    enum Status { OK = 0; NOT_FOUND = 1; UNAVAILABLE = 2; }
    Status status = 2;
}
message PutRequest    { string key = 1; string value = 2; }
message PutResponse   { bool ok = 1; }
message AppendRequest { string key = 1; string value = 2; }
message AppendResponse{ bool ok = 1; }
```

The Clerk inside kvcluster handles all idempotency — the gRPC API is clean/stateless.

---

## Step 2 — CMake (`CMakeLists.txt`)

Three CMake targets:
1. **`raft_kv_lib`** — static lib compiling your Lab3+Lab4 .cpp files
2. **`kvcluster`** — gRPC server binary; links `raft_kv_lib`
3. **`raftdrive`** — REST API binary; links `kvstore_proto` + AWS SDK + Crow

Lab3 `.cpp` files to compile:
- `raft.cpp`, `persister.cpp`, `helper.cpp`, `rpc/labrpc.cpp`

Lab4 `.cpp` files to compile:
- `kvserver.cpp`, `config.cpp`

Key CMake trick — generate gRPC stubs:
```cmake
add_custom_command(
    OUTPUT kvstore.pb.cc kvstore.pb.h kvstore.grpc.pb.cc kvstore.grpc.pb.h
    COMMAND protobuf::protoc
        --grpc_out=... --cpp_out=... --plugin=protoc-gen-grpc=...
        kvstore.proto
)
add_library(kvstore_proto STATIC kvstore.pb.cc kvstore.grpc.pb.cc)
target_link_libraries(kvstore_proto PUBLIC gRPC::grpc++ protobuf::libprotobuf)
```

---

## Step 3 — kvcluster Binary

### 3a. `backends/ikv_backend.hpp` — The Swap Point

```cpp
class IKVBackend {
public:
    virtual ~IKVBackend() = default;
    virtual std::string get(const std::string& key) = 0;   // "" if not found
    virtual void put(const std::string& key, const std::string& value) = 0;
    virtual void append(const std::string& key, const std::string& value) = 0;
    virtual bool isHealthy() const = 0;
    virtual void shutdown() = 0;
};
```

### 3b. `backends/inprocess_raft_backend.hpp` — Phase 1 Implementation

Wraps the existing `Config` + `Clerk` from Lab4:

```cpp
class InProcessRaftBackend : public IKVBackend {
public:
    InProcessRaftBackend(int numNodes = 3) {
        m_config = make_config(numNodes, false, -1);
        for (int i = 0; i < numNodes; i++) m_config->startServer(i);
        m_config->connectAll();
        m_clerk = m_config->makeClient({0, 1, 2});
    }

    std::string get(const std::string& key) override {
        std::lock_guard lk(m_mu);
        return m_clerk->Get(key);    // Clerk handles leader retry internally
    }
    void put(...)    override { std::lock_guard lk(m_mu); m_clerk->Put(key, value); }
    void append(...) override { std::lock_guard lk(m_mu); m_clerk->Append(key, value); }
    ...
private:
    std::unique_ptr<Config> m_config;
    Clerk* m_clerk;
    std::mutex m_mu;   // Clerk not thread-safe on its own
};
```

### 3c. `kv_grpc_service.hpp` — gRPC Service Implementation

```cpp
class KVStoreServiceImpl : public kvstore::KVStore::Service {
public:
    KVStoreServiceImpl(std::shared_ptr<IKVBackend> backend) : m_backend(backend) {}

    grpc::Status Get(ctx, const GetRequest* req, GetResponse* resp) override {
        auto val = m_backend->get(req->key());
        val.empty() ? resp->set_status(NOT_FOUND)
                    : (resp->set_value(val), resp->set_status(OK));
        return grpc::Status::OK;
    }
    // Same for Put and Append...
};
```

### 3d. `main.cpp`

```cpp
int main() {
    auto backend = std::make_shared<InProcessRaftBackend>(3);
    // PHASE 2: swap the line above with DistributedRaftBackend

    KVStoreServiceImpl service(backend);
    grpc::ServerBuilder builder;
    builder.AddListeningPort("0.0.0.0:50051", grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    auto server = builder.BuildAndStart();
    server->Wait();
}
```

---

## Step 4 — raftdrive Backend

### 4a. `models/drive_models.hpp`

```cpp
struct FileMeta {
    std::string name, path, parent, content_type, s3_key;
    int64_t size, created_at, modified_at;
};
struct DirMeta {
    std::string name, path, parent;
    int64_t created_at, modified_at;
};
using FsItem = std::variant<FileMeta, DirMeta>;
struct ListingResult { DirMeta dir; std::vector<FsItem> children; };
```

### 4b. `clients/kv_grpc_client.hpp`

Thin wrapper around the generated `kvstore::KVStore::Stub`:

```cpp
class KvGrpcClient {
public:
    KvGrpcClient(const std::string& target) {
        m_stub = kvstore::KVStore::NewStub(
            grpc::CreateChannel(target, grpc::InsecureChannelCredentials()));
    }
    std::string get(const std::string& key);   // "" if NOT_FOUND
    bool put(const std::string& key, const std::string& value);
    bool append(const std::string& key, const std::string& value);
private:
    std::unique_ptr<kvstore::KVStore::Stub> m_stub;
};
```

### 4c. Metadata Schema (KV Keys)

All values are JSON strings. Key naming:

| Key format       | Value |
|------------------|-------|
| `dir:/path`      | `{"name":"X","path":"/X","parent":"/","created_at":...,"modified_at":...}` |
| `file:/path/f`   | `{"name":"f","path":"/path/f","parent":"/path","size":1024,"content_type":"...","s3_key":"files/uuid-f","created_at":...,"modified_at":...}` |
| `children:/path` | `["subdir","file.txt"]` — bare names of immediate children |

### 4d. `services/metadata_service.hpp`

```cpp
class MetadataService {
public:
    MetadataService(std::shared_ptr<KvGrpcClient> kv);

    std::optional<FileMeta>   getFileMeta(const std::string& path);
    std::optional<DirMeta>    getDirMeta(const std::string& path);
    std::vector<std::string>  getChildren(const std::string& path);

    void putFileMeta(const FileMeta& f);
    void putDirMeta(const DirMeta& d);

    // Read-modify-write children list (safe: single writer, linearizable Put)
    void addChild(const std::string& parent, const std::string& name);
    void removeChild(const std::string& parent, const std::string& name);

    void deleteFileMeta(const std::string& path);
    void deleteDirMeta(const std::string& path);
    void deleteChildrenList(const std::string& path);

    void ensureRoot();   // called at startup; idempotent

private:
    std::shared_ptr<KvGrpcClient> m_kv;

    static std::string fileKey (const std::string& p) { return "file:"     + p; }
    static std::string dirKey  (const std::string& p) { return "dir:"      + p; }
    static std::string childKey(const std::string& p) { return "children:" + p; }
};
```

Use `nlohmann/json` (already in Lab3 includes as `json.hpp`) to serialize/deserialize.

### 4e. `services/storage_service.hpp`

```cpp
class StorageService {
public:
    // endpoint_override: "http://localhost:4566" for LocalStack, "" for real S3
    StorageService(const std::string& bucket, const std::string& endpoint_override = "");

    // Returns the s3_key used (same as passed in)
    std::string uploadObject(const std::string& s3_key,
                             const std::string& content_type,
                             const std::vector<uint8_t>& data);
    bool downloadObject(const std::string& s3_key, std::vector<uint8_t>& out);
    bool deleteObject(const std::string& s3_key);

    // Generates a UUID-based key: "files/<uuid>-<original_name>"
    // UUID key means renames never require an S3 object copy
    static std::string generateS3Key(const std::string& original_name);

private:
    std::string m_bucket;
    Aws::S3::S3Client m_client;
};
```

In `main.cpp` call `Aws::InitAPI(opts)` before constructing StorageService and `Aws::ShutdownAPI(opts)` at exit.

### 4f. `services/fs_service.hpp`

Orchestrates metadata + storage. This is what the REST handlers call.

```cpp
class FsService {
public:
    FsService(std::shared_ptr<MetadataService> meta,
              std::shared_ptr<StorageService>  storage);

    ListingResult        listDir   (const std::string& path);
    DirMeta              createDir (const std::string& path);
    FileMeta             uploadFile(const std::string& dir_path,
                                    const std::string& filename,
                                    const std::string& content_type,
                                    const std::vector<uint8_t>& data);
    std::vector<uint8_t> downloadFile(const std::string& path);
    void                 deleteFile(const std::string& path);
    void                 deleteDir (const std::string& path);   // recursive BFS
    FileMeta             renameFile(const std::string& old_path,
                                    const std::string& new_path);
    DirMeta              renameDir (const std::string& old_path,
                                    const std::string& new_path);
};
```

### 4g. REST API (Crow)

Register all routes in `api/router.hpp`:

```
GET    /api/dirs?path=         List dir → {dir, children[]}
POST   /api/dirs               body: {"path":"/NewDir"} → 201 DirMeta
DELETE /api/dirs?path=         Recursive delete → 204
PATCH  /api/dirs/rename        body: {"old_path","new_path"} → 200 DirMeta

GET    /api/files/meta?path=   File metadata → 200 FileMeta
GET    /api/files/download?path=  Streams S3 bytes → 200 (Content-Disposition)
DELETE /api/files?path=        Delete file (KV + S3) → 204
PATCH  /api/files/rename       body: {"old_path","new_path"} → 200 FileMeta

POST   /api/upload?path=       Multipart "file" field → 201 FileMeta
```

Use `crow::multipart::message` to parse uploads.
Return JSON using `nlohmann/json` or just build strings.
All error responses: `{"error": "message"}`.

### 4h. `main.cpp` wiring

```cpp
int main() {
    Aws::SDKOptions opts;
    Aws::InitAPI(opts);

    auto kv       = std::make_shared<KvGrpcClient>(getenv("RAFTDRIVE_KV_TARGET"));
    auto meta     = std::make_shared<MetadataService>(kv);
    auto storage  = std::make_shared<StorageService>(
                        getenv("RAFTDRIVE_S3_BUCKET"),
                        getenv("RAFTDRIVE_S3_ENDPOINT") ?: "");
    auto fs       = std::make_shared<FsService>(meta, storage);

    meta->ensureRoot();

    crow::SimpleApp app;
    registerRoutes(app, fs);
    app.port(8080).multithreaded().run();

    Aws::ShutdownAPI(opts);
}
```

---

## Step 5 — Frontend (React + TypeScript + Vite + Tailwind)

### Setup
```bash
cd RaftDrive
npm create vite@latest frontend -- --template react-ts
cd frontend && npm install
npm install -D tailwindcss postcss autoprefixer
npx tailwindcss init -p
npm install zustand lucide-react
```

### `vite.config.ts` — Proxy API calls
```ts
export default defineConfig({
    plugins: [react()],
    server: {
        proxy: { '/api': 'http://localhost:8080' }
    }
})
```

### Types (`src/types/file.ts`)
```ts
export type FileType = 'file' | 'dir';

export interface FileMeta {
    type: 'file';
    name: string;
    path: string;
    size: number;
    content_type: string;
    modified_at: number;
}

export interface DirMeta {
    type: 'dir';
    name: string;
    path: string;
    modified_at: number;
}

export type FsItem = FileMeta | DirMeta;

export interface DirListing {
    dir: DirMeta;
    children: FsItem[];
}
```

### State (`src/store/driveStore.ts`)
```ts
import { create } from 'zustand';

interface DriveStore {
    cwd: string;
    selection: Set<string>;
    setCwd: (path: string) => void;
    toggleSelect: (path: string) => void;
    clearSelection: () => void;
}

export const useDriveStore = create<DriveStore>((set) => ({
    cwd: '/',
    selection: new Set(),
    setCwd: (path) => set({ cwd: path, selection: new Set() }),
    toggleSelect: (path) => set((s) => {
        const sel = new Set(s.selection);
        sel.has(path) ? sel.delete(path) : sel.add(path);
        return { selection: sel };
    }),
    clearSelection: () => set({ selection: new Set() }),
}));
```

### API (`src/api/driveApi.ts`)
```ts
export async function listDir(path: string): Promise<DirListing>
export async function createDir(path: string): Promise<DirMeta>
export async function uploadFile(dirPath: string, file: File,
    onProgress?: (pct: number) => void): Promise<FileMeta>
export async function downloadFile(path: string): Promise<void>  // triggers browser save
export async function deleteFile(path: string): Promise<void>
export async function deleteDir(path: string): Promise<void>
export async function renameFile(oldPath: string, newPath: string): Promise<FileMeta>
export async function renameDir(oldPath: string, newPath: string): Promise<DirMeta>
```

For `downloadFile`: create a hidden `<a href="/api/files/download?path=..." download>`, click it, remove it.
For `uploadFile`: use `XMLHttpRequest` so you get `upload.onprogress` events.

### Components

**`FileExplorer.tsx`** — root component:
- calls `useDirListing(cwd)` (custom hook wrapping `listDir`)
- renders `<BreadcrumbNav>`, `<Toolbar>`, `<FileList>`

**`BreadcrumbNav.tsx`** — splits cwd on `/`, renders each segment as a clickable `<button>` calling `setCwd`

**`FileRow.tsx`** — one row per item:
- folder icon (lucide `Folder`) or file icon (lucide `File`) based on type
- double-click dir → navigate; double-click file → `downloadFile`
- single click → `toggleSelect`
- shows size, formatted date

**`Toolbar.tsx`** — "New Folder" button, "Upload" button, "Delete" button (disabled when nothing selected)

**`UploadButton.tsx`** — hidden `<input type="file" multiple>`, triggers on button click, uses `useUpload` hook with XHR progress

**`ContextMenu.tsx`** — right-click menu with Rename / Delete / Download actions

---

## Step 6 — Docker Compose (`docker-compose.yml`)

```yaml
version: "3.9"
services:
  localstack:
    image: localstack/localstack:3.0
    ports:
      - "4566:4566"
    environment:
      SERVICES: s3
      AWS_DEFAULT_REGION: us-east-1
      AWS_ACCESS_KEY_ID: test
      AWS_SECRET_ACCESS_KEY: test
    volumes:
      - localstack_data:/var/lib/localstack
      - ./scripts/init-localstack.sh:/etc/localstack/init/ready.d/init.sh

volumes:
  localstack_data:
```

`scripts/init-localstack.sh`:
```bash
#!/bin/bash
awslocal s3 mb s3://raftdrive-objects --region us-east-1
```

`.env.example`:
```
RAFTDRIVE_KV_TARGET=localhost:50051
RAFTDRIVE_S3_BUCKET=raftdrive-objects
RAFTDRIVE_S3_ENDPOINT=http://localhost:4566
AWS_ACCESS_KEY_ID=test
AWS_SECRET_ACCESS_KEY=test
RAFTDRIVE_PORT=8080
```

---

## Step 7 — Installing Dependencies

```bash
# gRPC + Protobuf
sudo apt install -y libgrpc++-dev libprotobuf-dev protobuf-compiler-grpc

# AWS C++ SDK (S3 only)
sudo apt install -y libaws-sdk-cpp-s3-dev
# If not available: build from source (see aws/aws-sdk-cpp on GitHub)

# Crow (header-only — add as git submodule)
cd RaftDrive
git init
git submodule add https://github.com/CrowCpp/Crow third_party/crow

# Asio (required by Crow)
sudo apt install -y libasio-dev libssl-dev

# Node.js
sudo apt install -y nodejs npm
```

---

## Step 8 — Build & Run

```bash
# 1. Build C++ binaries
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
cd ..

# 2. Start LocalStack (S3)
docker compose up -d

# 3. Start KV cluster
./build/kvcluster/kvcluster

# 4. Start RaftDrive service (new terminal)
RAFTDRIVE_KV_TARGET=localhost:50051 \
RAFTDRIVE_S3_BUCKET=raftdrive-objects \
RAFTDRIVE_S3_ENDPOINT=http://localhost:4566 \
./build/raftdrive/raftdrive

# 5. Start frontend (new terminal)
cd frontend && npm run dev
# Open http://localhost:5173
```

---

## Verification Checklist

- [ ] `grpcurl -plaintext localhost:50051 kvstore.KVStore/Get '{"key":"dir:/"}'`  
      → `{"status":"NOT_FOUND"}` (before first use) or `{"value":"{...}","status":"OK"}`
- [ ] Create folder `/Documents` → KV has `children:/` = `["Documents"]`
- [ ] Upload a file → it appears in listing; S3 has the object
- [ ] Download the file → browser saves it with correct name
- [ ] Delete the file → gone from listing and S3
- [ ] Kill one of the 3 Raft nodes (simulate via `Config::shutdownServer(1)`) → operations still succeed (tolerates 1 failure)

---

## Phase 2 Swap Checklist (when your new Raft is ready)

- [ ] Implement `GrpcRaftTransport : IRaftTransport` using gRPC stubs
- [ ] Build new `kvnode` binary: one process per Raft node, uses `GrpcRaftTransport`
- [ ] Implement `DistributedRaftBackend : IKVBackend`
- [ ] In `kvcluster/main.cpp`: swap `InProcessRaftBackend` → `DistributedRaftBackend`
- [ ] Update Docker Compose: 3× `kvnode` containers instead of 1× `kvcluster`
- [ ] `raftdrive/`, `proto/kvstore.proto`, and `frontend/` — **zero changes needed**
