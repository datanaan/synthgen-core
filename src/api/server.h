#pragma once

#include "api/service.h"
#include <string>
#include <memory>

namespace httplib { class Server; }

namespace synthgen::api {

class SynthGenServer {
public:
    explicit SynthGenServer(int port = 8080);
    ~SynthGenServer();

    void start();
    void stop();

    SynthGenService& service();

private:
    void register_routes();

    int port_;
    std::unique_ptr<httplib::Server> server_;
    SynthGenService service_;
};

}  // namespace synthgen::api
