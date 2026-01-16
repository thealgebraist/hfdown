#include "agent_impl.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>

namespace agent {
// DatalogEngine and OllamaClient implementations remain the same...
void DatalogEngine::add_fact(const Fact& fact) { facts_.push_back(fact); }
std::string DatalogEngine::query(const std::string& query_str) {
    if (query_str.find("path(") == 0) {
        std::string start_type = query_str.substr(query_str.find("(") + 1, query_str.find(",") - query_str.find("(") - 1);
        std::string end_type = query_str.substr(query_str.find(",") + 2, query_str.find(")") - query_str.find(",") - 2);
        for (const auto& fact : facts_) {
            if (fact.predicate == "type" && fact.arguments[0] == start_type && fact.arguments[1] == "struct") {
                for (size_t i = 2; i < fact.arguments.size(); i += 2) {
                    if (fact.arguments[i+1] == end_type) return "proj(\"" + fact.arguments[i] + "\")";
                }
            }
        }
    }
    return "unknown_path";
}

OllamaClient::OllamaClient(const std::string& model) : model_(model) {}
std::expected<std::string, std::string> OllamaClient::prompt(const std::string& s, const std::string& u) {
    auto escape = [](const std::string& str) {
        std::string escaped;
        for (char c : str) {
            if (c == '"' || c == '\\') escaped += '\\';
            escaped += c;
        }
        return escaped;
    };
    std::string body = "{\"model\":\"" + model_ + "\",\"system\":\"" + escape(s) + "\",\"prompt\":\"" + escape(u) + "\",\"stream\":false}";
    auto r = http_.post("http://localhost:11434/api/generate", body);
    if (!r) return std::unexpected("Ollama failed: " + r.error().message);
    std::string text = *r;
    size_t start = text.find("\"response\":\"");
    if (start == std::string::npos) return text;
    start += 12;
    size_t end = text.find("\"", start);
    return text.substr(start, end - start);
}

// Updated AgentController for two-model pipeline
AgentController::AgentController(const std::string& generator_model, const std::string& repair_model) 
    : ollama_(generator_model), repair_ollama_(repair_model) {
    targets_ = {"lisp_compiler.c"}; // Focus on one component-based strategy
    datalog_.add_fact({"type", {"File", "struct", "name", "String"}});
}

void AgentController::run_conversion_loop() { for (const auto& t : targets_) process_target(t); }

void AgentController::evaluate_rules() {
    compact::Writer::print("[FSM] State: " + std::to_string((int)current_facts_.current_state) + "\n");
    if (current_facts_.current_state == AgentState::IDLE) do_generate();
    else if (current_facts_.current_state == AgentState::GENERATING) do_verify();
    else if (current_facts_.current_state == AgentState::REPAIRING) do_repair();
}

void AgentController::process_target(const std::string& target) {
    current_facts_ = {};
    current_facts_.target_file = target;
    current_facts_.add_fact("target", {target});
    evaluate_rules();
    if (current_facts_.current_state == AgentState::SUCCESS) {
        std::ofstream f(target); f << current_facts_.generated_code; f.close();
        compact::Writer::print(">>> SUCCESS: " + target + "\n");
    } else {
        compact::Writer::error(">>> FAILURE: " + target + ". Final error: " + current_facts_.build_error + "\n");
    }
}

void AgentController::do_generate() {
    current_facts_.current_state = AgentState::GENERATING;
    std::string system = "You are a C99 compiler expert for ARM64 macOS. Output ONLY clean, complete C99 code in backticks. Include headers.";
    std::string user = "Write a Lisp-to-ARM64 compiler in C99. Structure it with: 1. A lexer for Lisp atoms/lists. 2. A recursive descent parser building an AST. 3. An AST traversal function that emits ARM64 assembly for basic arithmetic.";
    
    auto resp = ollama_.prompt(system, user);
    if (resp) {
        current_facts_.generated_code = *resp;
        // Simplified extraction
        size_t start = current_facts_.generated_code.find("```c");
        if (start != std::string::npos) {
            start += 3;
            size_t end = current_facts_.generated_code.find("```", start);
            if (end != std::string::npos) current_facts_.generated_code = current_facts_.generated_code.substr(start, end - start);
        }
        current_facts_.add_fact("generated");
        evaluate_rules();
    } else {
        current_facts_.current_state = AgentState::FAILURE;
    }
}

void AgentController::do_verify() {
    current_facts_.current_state = AgentState::VERIFYING;
    std::string res = verify_c99(current_facts_.generated_code);
    if (res.find("Success") != std::string::npos) {
        current_facts_.current_state = AgentState::SUCCESS;
    } else {
        current_facts_.build_error = res;
        current_facts_.add_fact("error", {res});
        if (current_facts_.attempt_count < 25) {
            current_facts_.current_state = AgentState::REPAIRING;
            evaluate_rules();
        } else {
            current_facts_.current_state = AgentState::FAILURE;
        }
    }
}

void AgentController::do_repair() {
    current_facts_.attempt_count++;
    compact::Writer::print("[FSM] Repairing with specialist model (Attempt " + std::to_string(current_facts_.attempt_count) + ")\n");
    std::string system = "You are a C99 syntax expert. Fix the errors in the following code. Output ONLY the full, corrected C99 code in backticks.";
    std::string user = "Code:\n" + current_facts_.generated_code + "\n\nErrors:\n" + current_facts_.build_error;
    
    auto resp = repair_ollama_.prompt(system, user);
    if (resp) {
        current_facts_.generated_code = *resp;
        size_t start = current_facts_.generated_code.find("```c");
        if (start != std::string::npos) {
            start += 3;
            size_t end = current_facts_.generated_code.find("```", start);
            if (end != std::string::npos) current_facts_.generated_code = current_facts_.generated_code.substr(start, end - start);
        }
        current_facts_.add_fact("repaired");
        evaluate_rules();
    } else {
        current_facts_.current_state = AgentState::FAILURE;
    }
}

std::string AgentController::verify_c99(const std::string& code) {
    std::ofstream f("temp.c"); f << code; f.close();
    std::string cmd = "clang -std=c99 -c temp.c -o /dev/null 2>&1";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return "popen failed";
    char buf[128]; std::string log;
    while (fgets(buf, 128, p)) log += buf;
    return (pclose(p) == 0) ? "Success" : log;
}
std::string AgentController::verify_cpp(const std::string& code) { return "Not implemented"; }
void AgentController::do_query_datalog(const std::string& q) {}

} // namespace agent

int main(int argc, char** argv) {
    agent::AgentController controller("mathstral:7b", "qwen2.5-coder:3b");
    controller.run_conversion_loop();
    return 0;
}
