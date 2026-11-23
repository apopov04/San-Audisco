#include "bridge/agent_platform.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#ifndef _WIN32
#include <signal.h>
#endif
using Clock = std::chrono::steady_clock;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--hang") {
        // Intentionally never consume stdin: reproduce a stalled engine.
        std::this_thread::sleep_for(std::chrono::seconds(30));
        return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--echo") {
        std::string line;
        while (std::getline(std::cin, line) && line != "quit")
            std::cout << line << '\n' << std::flush;
        return 0;
    }
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN); // Same process policy as the agent executable.
#endif
    const char* phase = "construct";
    int cycle = 0;
    try {
        bridge::agent::Child child;
        auto executable = bridge::agent::executable_path();
        for (; cycle < 3; ++cycle) {
            phase = "start stalled child";
            child.start(executable, {"--hang"});
            auto began = Clock::now();
            phase = "fill stalled child command queue";
            bool full = false;
            for (int i = 0; i < 256; ++i) {
                try { child.send(std::string(4095, 'x') + "\n"); }
                catch (const std::exception&) { full = true; break; }
            }
            require(full, "Stalled child did not apply bounded backpressure");
            require(Clock::now() - began < std::chrono::milliseconds(500),
                    "Sending to stalled child blocked the agent");
            phase = "stop stalled child";
            began = Clock::now();
            child.stop();
            require(Clock::now() - began < std::chrono::milliseconds(2500),
                    "Stopping stalled child exceeded deadline");
            require(!child.running(), "Stopped child remains running");
            phase = "restart echo child";
            child.start(executable, {"--echo"});
            phase = "send echo command";
            child.send("restarted\n");
            phase = "read echo response";
            std::string output;
            auto deadline = Clock::now() + std::chrono::seconds(3);
            do {
                output += child.read_output();
                if (output.find("restarted") != std::string::npos) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            } while (Clock::now() < deadline);
            require(output.find("restarted") != std::string::npos,
                    "Restarted child did not receive command");
            phase = "stop echo child";
            child.stop();
        }
        std::cout << "PASS bounded child backpressure, forced stop, and restart\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL cycle " << cycle << " (" << phase << "): " << error.what() << '\n';
        return 1;
    }
}
