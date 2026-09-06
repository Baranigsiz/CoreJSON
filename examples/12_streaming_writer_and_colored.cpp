#include <iostream>
#include <senko/senko.hpp>

using json = senko::json;

int main() {
    std::cout << "=== SenkoJSON - Streaming Writer & ANSI Terminal Colors Demo ===\n\n";

    // 1. Zero-Allocation Streaming Writer
    std::cout << "[1] Streaming JSON directly without DOM allocations:\n";
    senko::json_writer writer(2); // Indent 2 spaces for pretty-print
    writer.start_object()
          .key("service").value("SenkoEngine")
          .key("status").value("healthy")
          .key("uptime_seconds").value(86400)
          .key("metrics").start_object()
              .key("requests_per_sec").value(152300.5)
              .key("error_rate").value(0.0)
              .key("cluster_nodes").start_array()
                  .value("node-us-east-1")
                  .value("node-eu-west-1")
                  .value("node-ap-east-1")
              .end_array()
          .end_object()
          .key("notes").null_value()
          .end_object();

    std::cout << writer.str() << "\n\n";

    // 2. Syntax-Highlighted Terminal JSON
    std::cout << "[2] ANSI Syntax-Highlighted Terminal Output (doc.dump_colored()):\n";
    json doc = json::parse(writer.str());
    std::cout << doc.dump_colored(2) << "\n";

    return 0;
}
