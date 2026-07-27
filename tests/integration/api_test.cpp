#include <gtest/gtest.h>
#include "api/server.h"

#include <httplib.h>
#include <thread>
#include <chrono>
#include <string>

using namespace synthgen::api;

class ApiTest : public ::testing::Test {
protected:
    void SetUp() override {
        static int port_offset = 0;
        int port = 28000 + (::getpid() % 1000) + (port_offset++ * 13);
        server_ = std::make_unique<SynthGenServer>(port);
        port_ = port;

        // Start server in background thread
        server_thread_ = std::thread([this]() {
            server_->start();
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        client_ = std::make_unique<httplib::Client>("http://127.0.0.1:" + std::to_string(port_));
        client_->set_connection_timeout(5);
        client_->set_read_timeout(5);
    }

    void TearDown() override {
        server_->stop();
        if (server_thread_.joinable()) server_thread_.join();
    }

    std::unique_ptr<SynthGenServer> server_;
    std::unique_ptr<httplib::Client> client_;
    std::thread server_thread_;
    int port_;
};

TEST_F(ApiTest, HealthEndpoint) {
    auto res = client_->Get("/v1/health");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);
    EXPECT_NE(res->body.find("\"healthy\""), std::string::npos);
    EXPECT_NE(res->body.find("\"version\""), std::string::npos);
}

TEST_F(ApiTest, MetricsEndpoint) {
    auto res = client_->Get("/v1/metrics");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);
}

TEST_F(ApiTest, DefineTypeEndpoint) {
    auto res = client_->Post("/v1/types",
        R"({
            "type_name": "sensor",
            "columns": [
                {"name": "temperature", "type": "FLOAT", "range_min": -50.0, "range_max": 80.0},
                {"name": "pressure", "type": "FLOAT", "range_min": 900.0, "range_max": 1100.0}
            ]
        })", "application/json");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200) << "Body: " << res->body;
    EXPECT_NE(res->body.find("\"sensor\""), std::string::npos) << "Body: " << res->body;
    EXPECT_NE(res->body.find("2"), std::string::npos) << "Body: " << res->body;
}

TEST_F(ApiTest, DefineTypeMissingName) {
    auto res = client_->Post("/v1/types",
        R"({"columns": [{"name": "x", "type": "FLOAT"}]})", "application/json");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 400);
}

TEST_F(ApiTest, DefineTypeInvalidJson) {
    auto res = client_->Post("/v1/types", "not json", "application/json");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 400);
}

TEST_F(ApiTest, LoadDataEndpoint) {
    // First define type
    client_->Post("/v1/types",
        R"({"type_name": "sensor_ld", "columns": [{"name": "temp", "type": "FLOAT"}]})",
        "application/json");

    auto res = client_->Post("/v1/types/sensor_ld/data",
        R"({"path": "/data/test.parquet", "mode": "strict"})", "application/json");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);
    EXPECT_NE(res->body.find("\"success\""), std::string::npos);
}

TEST_F(ApiTest, LoadDataTypeNotFound) {
    auto res = client_->Post("/v1/types/nonexistent/data",
        R"({"path": "/data/test.parquet"})", "application/json");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 404);
}

TEST_F(ApiTest, DefineConstraintEndpoint) {
    client_->Post("/v1/types",
        R"({"type_name": "sensor_dc", "columns": [{"name": "temp", "type": "FLOAT", "range_min": -50.0, "range_max": 80.0}]})",
        "application/json");

    auto res = client_->Post("/v1/constraints",
        R"({
            "constraint_name": "safe",
            "type_name": "sensor_dc",
            "checks": [{"column": "temp", "min": -10.0, "max": 45.0}]
        })", "application/json");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);
    EXPECT_NE(res->body.find("\"safe\""), std::string::npos);
}

TEST_F(ApiTest, ExplainEndpoint) {
    client_->Post("/v1/types",
        R"({"type_name": "sensor_ex", "columns": [{"name": "temp", "type": "FLOAT"}]})",
        "application/json");

    auto res = client_->Post("/v1/explain",
        R"({"type_name": "sensor_ex", "constraints": ["test"]})", "application/json");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);
    EXPECT_NE(res->body.find("\"row_by_row\""), std::string::npos);
}

TEST_F(ApiTest, GenerateEndpoint) {
    // Full pipeline via API
    client_->Post("/v1/types",
        R"({"type_name": "sensor_gen", "columns": [
            {"name": "temperature", "type": "FLOAT", "range_min": -50.0, "range_max": 80.0}
        ]})", "application/json");

    client_->Post("/v1/constraints",
        R"({
            "constraint_name": "safe_gen",
            "type_name": "sensor_gen",
            "checks": [{"column": "temperature", "min": -10.0, "max": 45.0}]
        })", "application/json");

    auto res = client_->Post("/v1/generate",
        R"({
            "type_name": "sensor_gen",
            "constraints": ["safe_gen"],
            "limit": 100,
            "seed": 42
        })", "application/json");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200) << "Body: " << res->body;
    EXPECT_NE(res->body.find("\"physics_guaranteed\""), std::string::npos);
    EXPECT_NE(res->body.find("\"not_applicable\""), std::string::npos);
    EXPECT_NE(res->body.find("100"), std::string::npos);  // rows_generated value
}

TEST_F(ApiTest, GenerateTypeNotFound) {
    auto res = client_->Post("/v1/generate",
        R"({"type_name": "nonexistent", "limit": 10})", "application/json");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 404);
}

TEST_F(ApiTest, GenerateInvalidLimit) {
    client_->Post("/v1/types",
        R"({"type_name": "sensor_gi", "columns": [{"name": "temp", "type": "FLOAT"}]})",
        "application/json");

    auto res = client_->Post("/v1/generate",
        R"({"type_name": "sensor_gi", "limit": 0})", "application/json");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 400);
}
