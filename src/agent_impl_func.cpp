void AgentController::execute_conversion_step(const std::filesystem::path& src) {
    bool is_asm = (src.extension() == ".asm");
    compact::Writer::print(is_asm ? ">>> Generating: " : ">>> Converting: "); compact::Writer::print(src.string()); compact::Writer::nl();
    
    std::string input = read_source(src.string());
    std::string system_prompt;
    if (is_asm) {
        system_prompt = "You are a low-level engineer on Darwin ARM64. Write optimized assembly. "
                        "Use Mach-O syntax. Entry is _start. Exit is mov x16, #1; svc #0x80. "
                        "Output ONLY the code inside triple backticks.";
    } else {
        system_prompt = "You are a formal compiler. Output ONLY formal IR code inside triple backticks.";
    }
    
    std::string current_user_prompt = "C++ Code:\n" + input + "\n\nIR Code:";
    int retries = 3;
    bool success = false;

    while (retries-- > 0) {
        compact::Writer::print("[AGENT] Requesting model (Attempt "); 
        compact::Writer::print_num(3 - retries); 
        compact::Writer::print(")..."); compact::Writer::nl();
        
        auto start_wait = std::chrono::steady_clock::now();
        auto response = ollama_.prompt(system_prompt, current_user_prompt);
        auto end_wait = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(end_wait - start_wait).count();
        
        if (!response) {
            compact::Writer::error("[AGENT] Ollama call failed. Error: ");
            compact::Writer::error(response.error()); compact::Writer::nl();
            return;
        }

        compact::Writer::print("[AGENT] Model responded in "); 
        compact::Writer::print_num(elapsed); 
        compact::Writer::print(" seconds."); compact::Writer::nl();

        std::string output = *response;
        size_t code_start = output.find("```");
        if (code_start != std::string::npos) {
            size_t first_nl = output.find('\n', code_start);
            size_t code_end = output.find("```", first_nl);
            if (code_end != std::string::npos) {
                output = output.substr(first_nl + 1, code_end - first_nl - 1);
            }
        }

        std::string verification = verify_build(src.string(), output);
        compact::Writer::print(verification); compact::Writer::nl();
        
        if (verification.find("Success") != std::string::npos) {
            write_ir(src.string() + (is_asm ? ".s" : ".ir.txt"), output);
            success = true;
            break;
        } else {
            compact::Writer::error("[RETRY] Feeding errors back to model..."); compact::Writer::nl();
            current_user_prompt = "The previous attempt failed with these errors:\n" + verification + 
                                 "\nPlease fix the code and output the full corrected version inside triple backticks.";
        }
    }

    if (!success) {
        compact::Writer::error("Failed to generate valid code after multiple attempts."); compact::Writer::nl();
    }
}
