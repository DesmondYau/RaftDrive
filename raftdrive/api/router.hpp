#pragma once
#include "crow.h"
#include "../handlers/handler_fs.hpp"

inline void registerRoutes(crow::SimpleApp& app, FileSystemService& fs)
{
    CROW_ROUTE(app, "/api/dirs/")
    .methods(crow::HTTPMethod::GET)
    ([&fs](const crow::request& req) {
        return handleListDir(fs, "/");
    });

    CROW_ROUTE(app, "/api/dirs/<path>")
    .methods(crow::HTTPMethod::GET)
    ([&fs](const crow::request& req, std::string path) {
        return handleListDir(fs, "/" + path);
    });

    CROW_ROUTE(app, "/api/dirs/<path>")
    .methods(crow::HTTPMethod::POST)
    ([&fs](const crow::request& req, std::string path) {
        return handleCreateDir(fs, "/" + path);
    });

    CROW_ROUTE(app, "/api/dirs/<path>")
    .methods(crow::HTTPMethod::DELETE)
    ([&fs](const crow::request& req, std::string path) {
        return handleDeleteDir(fs, "/" + path);
    });

    CROW_ROUTE(app, "/api/files/<path>")
    .methods(crow::HTTPMethod::POST)
    ([&fs](const crow::request& req, std::string path) {
        return handleUploadFile(fs, "/" + path, req);
    });

    CROW_ROUTE(app, "/api/files/<path>")
    .methods(crow::HTTPMethod::GET)
    ([&fs](const crow::request& req, std::string path) {
        return handleDownloadFile(fs, "/" + path);
    });

    CROW_ROUTE(app, "/api/files/<path>")
    .methods(crow::HTTPMethod::DELETE)
    ([&fs](const crow::request& req, std::string path) {
        return handleDeleteFile(fs, "/" + path);
    });
}