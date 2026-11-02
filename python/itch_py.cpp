//
// Created by Rachid EL MAAZOUZ on 01/11/2026.
//
#include <type_traits>
#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <vector>
#include "itch/features.h"

namespace py = pybind11;

template <class T>
static py::array_t<T> to_numpy(std::vector<T>&& v){
    auto* held = new std::vector<T>(std::move(v));
    py::capsule owner(held, [](void* p){delete reinterpret_cast<std::vector<T>*>(p); });
    return py::array_t<T>(static_cast<py::ssize_t>(held->size()),held->data(), owner);
}

static py::dict extract_features_py(const std::string& path, const std::string& symbol) {
    itch::FeatureSeries s = itch::extract_features(path, symbol);
    py::dict d;
    d["ts"]     = to_numpy(std::move(s.ts));
    d["bid_px"] = to_numpy(std::move(s.bid_px));
    d["bid_sz"] = to_numpy(std::move(s.bid_sz));
    d["ask_px"] = to_numpy(std::move(s.ask_px));
    d["ask_sz"] = to_numpy(std::move(s.ask_sz));
    return d;
}

PYBIND11_MODULE(itch_py, m) {
    m.doc() = "ITCH feed handler — Python bindings";
    m.def("extract_features", &extract_features_py,
          py::arg("path"), py::arg("symbol"),
          "Replay an ITCH file and return top-of-book feature columns as NumPy arrays");
}