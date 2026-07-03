#pragma once
#include "crow.h"
#include "../services/service_fs.hpp"

inline crow::response handleListDir(FileSystemService& fs, const std::string& path)
{
    try
    {
        // Call service — throws std::runtime_error if directory not found
        ListingResult result = fs.listDir(path);

        // Build JSON response
        crow::json::wvalue json;
        json["path"] = result.dir.path;
        json["name"] = result.dir.name;

        // Serialize children array — each item tagged with "type"
        crow::json::wvalue::list childrenJson;
        for (auto& item : result.children)
        {
            crow::json::wvalue child;
            if (std::holds_alternative<FileMeta>(item))
            {
                auto& f = std::get<FileMeta>(item);
                child["type"]         = "file";
                child["name"]         = f.name;
                child["size"]         = f.size;
                child["content_type"] = f.content_type;
            }
            else
            {
                auto& d = std::get<DirMeta>(item);
                child["type"] = "dir";
                child["name"] = d.name;
            }
            childrenJson.push_back(std::move(child));
        }
        json["children"] = std::move(childrenJson);

        return crow::response(200, json);
    }
    catch (const std::runtime_error& e)
    {
        // Service throws runtime_error for not found — map to 404
        return crow::response(404, e.what());
    }
}

inline crow::response handleCreateDir(FileSystemService& fs, const std::string& path)
{
    try
    {
        fs.createDir(path);
        return crow::response(201);
    }
    catch (const std::runtime_error& e)
    {
        return crow::response(409, e.what());
    }
}

inline crow::response handleDownloadFile(FileSystemService& fs, const std::string& path)
{
    try
    {
        DownloadResult result = fs.downloadFile(path);

        crow::response res(200);
        res.body = result.data;
        res.set_header("Content-Type", result.contentType);
        return res;
    }
    catch (const std::runtime_error& e)
    {
        return crow::response(404, e.what());
    }
}

inline crow::response handleDeleteDir(FileSystemService& fs, const std::string& path)
{
    try
    {
        fs.deleteDir(path);
        return crow::response(200);
    }
    catch (const std::runtime_error& e)
    {
        return crow::response(404, e.what());
    }
}

inline crow::response handleUploadFile(FileSystemService& fs, const std::string& path, const crow::request& req)
{
    try
    {
        std::string contentType = req.get_header_value("Content-Type");
        FileMeta fileMeta = fs.uploadFile(path, req.body, contentType);

        // Return created file metadata as JSON
        crow::json::wvalue json;
        json["name"]         = fileMeta.name;
        json["path"]         = fileMeta.path;
        json["size"]         = fileMeta.size;
        json["content_type"] = fileMeta.content_type;
        json["created_at"]   = fileMeta.created_at;

        return crow::response(201, json);
    }
    catch (const std::runtime_error& e)
    {
        std::cerr << "[uploadFile] ERROR: " << e.what() << "\n";
        return crow::response(409, e.what());
    }
}

inline crow::response handleDeleteFile(FileSystemService& fs, const std::string& path)
{
    try
    {
        fs.deleteFile(path);
        return crow::response(200);
    }
    catch (const std::runtime_error& e)
    {
        return crow::response(404, e.what());
    }
}