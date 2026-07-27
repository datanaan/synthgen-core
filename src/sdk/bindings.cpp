#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "api/service.h"
#include "api/request.h"
#include "api/response.h"

namespace py = pybind11;

PYBIND11_MODULE(synthgen_native, m) {
    m.doc() = "SynthGen Core native module";

    // === Request types ===
    py::class_<synthgen::api::DefineTypeRequest>(m, "_DefineTypeRequest")
        .def(py::init<>())
        .def_readwrite("type_name", &synthgen::api::DefineTypeRequest::type_name);

    py::class_<synthgen::api::LoadDataRequest>(m, "_LoadDataRequest")
        .def(py::init<>())
        .def_readwrite("type_name", &synthgen::api::LoadDataRequest::type_name)
        .def_readwrite("path", &synthgen::api::LoadDataRequest::path)
        .def_readwrite("mode", &synthgen::api::LoadDataRequest::mode);

    py::class_<synthgen::api::DefineConstraintRequest>(m, "_DefineConstraintRequest")
        .def(py::init<>())
        .def_readwrite("constraint_name", &synthgen::api::DefineConstraintRequest::constraint_name)
        .def_readwrite("type_name", &synthgen::api::DefineConstraintRequest::type_name);

    py::class_<synthgen::api::GenerateRequest>(m, "_GenerateRequest")
        .def(py::init<>())
        .def_readwrite("type_name", &synthgen::api::GenerateRequest::type_name)
        .def_readwrite("limit", &synthgen::api::GenerateRequest::limit)
        .def_readwrite("distribution", &synthgen::api::GenerateRequest::distribution)
        .def_readwrite("seed", &synthgen::api::GenerateRequest::seed);

    // === Response types ===
    py::class_<synthgen::api::SchemaRef>(m, "SchemaRef")
        .def_readonly("type_name", &synthgen::api::SchemaRef::type_name)
        .def_readonly("column_count", &synthgen::api::SchemaRef::column_count);

    py::class_<synthgen::api::ImportResult>(m, "ImportResult")
        .def_readonly("type_name", &synthgen::api::ImportResult::type_name)
        .def_readonly("rows_imported", &synthgen::api::ImportResult::rows_imported)
        .def_readonly("status", &synthgen::api::ImportResult::status);

    py::class_<synthgen::api::ConstraintRef>(m, "ConstraintRef")
        .def_readonly("constraint_name", &synthgen::api::ConstraintRef::constraint_name)
        .def_readonly("type_name", &synthgen::api::ConstraintRef::type_name)
        .def_readonly("check_count", &synthgen::api::ConstraintRef::check_count);

    py::class_<synthgen::api::ExplainResult>(m, "ExplainResult")
        .def_readonly("execution_mode", &synthgen::api::ExplainResult::execution_mode)
        .def_readonly("path", &synthgen::api::ExplainResult::path)
        .def_readonly("constraint_classification", &synthgen::api::ExplainResult::constraint_classification);

    py::class_<synthgen::api::GenerateResult>(m, "GenerateResult")
        .def_readonly("data_format", &synthgen::api::GenerateResult::data_format)
        .def_readonly("evidence_json", &synthgen::api::GenerateResult::evidence_json)
        .def_readonly("stats", &synthgen::api::GenerateResult::stats);

    py::class_<synthgen::api::GenerationStatsResponse>(m, "GenerationStatsResponse")
        .def_readonly("rows_generated", &synthgen::api::GenerationStatsResponse::rows_generated)
        .def_readonly("elapsed_ms", &synthgen::api::GenerationStatsResponse::elapsed_ms)
        .def_readonly("distribution_used", &synthgen::api::GenerationStatsResponse::distribution_used);

    py::class_<synthgen::api::HealthResponse>(m, "HealthResponse")
        .def_readonly("status", &synthgen::api::HealthResponse::status)
        .def_readonly("version", &synthgen::api::HealthResponse::version)
        .def_readonly("components", &synthgen::api::HealthResponse::components);

    // === Service ===
    py::class_<synthgen::api::SynthGenService>(m, "SynthService")
        .def(py::init<>())
        .def("define_type", [](synthgen::api::SynthGenService& svc,
                               const std::string& type_name,
                               const std::vector<std::tuple<std::string, std::string,
                                   bool, bool,
                                   std::optional<double>,
                                   std::optional<double>>>& columns) {
            synthgen::api::DefineTypeRequest req;
            req.type_name = type_name;
            for (const auto& [name, type, not_null, is_order, rmin, rmax] : columns) {
                synthgen::api::DefineTypeRequest::ColumnDef cd;
                cd.name = name;
                cd.type = type;
                cd.not_null = not_null;
                cd.is_order = is_order;
                cd.range_min = rmin;
                cd.range_max = rmax;
                req.columns.push_back(cd);
            }
            auto result = svc.define_type(req);
            if (!result.ok()) {
                throw std::runtime_error(result.error().message);
            }
            return result.value();
        })
        .def("load_data", [](synthgen::api::SynthGenService& svc,
                             const std::string& type_name,
                             const std::string& path,
                             const std::string& mode) {
            synthgen::api::LoadDataRequest req;
            req.type_name = type_name;
            req.path = path;
            req.mode = mode;
            auto result = svc.load_data(req);
            if (!result.ok()) {
                throw std::runtime_error(result.error().message);
            }
            return result.value();
        }, py::arg("type_name"), py::arg("path"), py::arg("mode") = "strict")
        .def("define_constraint", [](synthgen::api::SynthGenService& svc,
                                     const std::string& constraint_name,
                                     const std::string& type_name,
                                     const std::vector<std::tuple<std::string,
                                         std::optional<double>,
                                         std::optional<double>>>& checks) {
            synthgen::api::DefineConstraintRequest req;
            req.constraint_name = constraint_name;
            req.type_name = type_name;
            for (const auto& [col, min_v, max_v] : checks) {
                synthgen::api::DefineConstraintRequest::RangeCheck rc;
                rc.column = col;
                rc.min_val = min_v;
                rc.max_val = max_v;
                req.checks.push_back(rc);
            }
            auto result = svc.define_constraint(req);
            if (!result.ok()) {
                throw std::runtime_error(result.error().message);
            }
            return result.value();
        })
        .def("explain", [](synthgen::api::SynthGenService& svc,
                           const std::string& type_name,
                           const std::vector<std::string>& constraints) {
            synthgen::api::ExplainRequest req;
            req.type_name = type_name;
            req.constraints = constraints;
            auto result = svc.explain(req);
            if (!result.ok()) {
                throw std::runtime_error(result.error().message);
            }
            return result.value();
        })
        .def("generate", [](synthgen::api::SynthGenService& svc,
                            const std::string& type_name,
                            const std::vector<std::string>& constraints,
                            int64_t limit,
                            const std::optional<uint64_t>& seed,
                            const std::string& distribution) {
            synthgen::api::GenerateRequest req;
            req.type_name = type_name;
            req.constraints = constraints;
            req.limit = limit;
            req.seed = seed;
            req.distribution = distribution;
            auto result = svc.generate(req);
            if (!result.ok()) {
                throw std::runtime_error(result.error().message);
            }
            return result.value();
        }, py::arg("type_name"), py::arg("constraints"),
           py::arg("limit") = 1000,
           py::arg("seed") = py::none(),
           py::arg("distribution") = "uniform")
        .def("health", &synthgen::api::SynthGenService::health);
}
