#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
namespace bridge::agent {
std::filesystem::path executable_path();
std::filesystem::path default_directory();
void private_directory(const std::filesystem::path&);
void atomic_write(const std::filesystem::path&, const std::string&);
void detach(const std::filesystem::path&,const std::vector<std::string>&);
bool capture_authorized();
class InstanceLock {
public: explicit InstanceLock(const std::filesystem::path&);~InstanceLock();
private: std::intptr_t handle_=-1;
};
class Child {
public:
 Child();~Child();
 void start(const std::filesystem::path&,const std::vector<std::string>&);
 bool running(); void send(const std::string&); void stop();
 std::string read_output(); std::string read_errors();
private: struct Impl;std::unique_ptr<Impl> p_;
};
}
