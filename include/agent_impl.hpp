#pragma once

#include "http_client.hpp"
#include "compact_log.hpp"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <filesystem>
#include <expected>

namespace agent {

enum class AgentState {
    IDLE,
    GENERATING,
    VERIFYING,
    REPAIRING,
    SUCCESS,
    FAILURE
};

// Datalog-style Fact representation
struct Fact {
    std::string predicate;
    std::vector<std::string> arguments;
    
    std::string to_string() const {
        std::string s = predicate + "(";
        for (size_t i = 0; i < arguments.size(); ++i) {
            s += arguments[i] + (i == arguments.size() - 1 ? "" : ", ");
        }
        return s + ").";
    }
};

struct FactBase {
    std::string target_file;
    std::string extension;
    std::string source_context;
    std::string generated_code;
    std::string build_error;
    int attempt_count = 0;
    AgentState current_state = AgentState::IDLE;
    std::vector<Fact> history;
    
    void add_fact(std::string pred, std::vector<std::string> args = {}) {
        history.push_back({pred, args});
    }
};

class OllamaClient {
public:
    OllamaClient(const std::string& model = "qwen2.5-coder:3b");
    std::expected<std::string, std::string> prompt(const std::string& system, const std::string& user);
private:
    std::string model_;
    hfdown::HttpClient http_;
};

class DatalogEngine {
public:
    void add_fact(const Fact& fact);
    std::string query(const std::string& query); // e.g., "path(ModelFile, String)"
private:
    std::vector<Fact> facts_;
};

class AgentController {
public:
    AgentController(const std::string& generator_model, const std::string& repair_model);
    void run_conversion_loop();

private:
    OllamaClient ollama_; // Generator client
    OllamaClient repair_ollama_; // Repair client
    std::vector<std::string> targets_;
    FactBase current_facts_;
    DatalogEngine datalog_;

    // FSM
    void evaluate_rules();
    void do_generate();
    void do_verify();
    void do_repair();
    void do_query_datalog(const std::string& query);
    
    // Tools
    std::string verify_c99(const std::string& code);
    std::string verify_cpp(const std::string& code);
    
    void process_target(const std::string& target);
};

} // namespace agent
