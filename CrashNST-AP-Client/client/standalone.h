#pragma once
#include <windows.h>
#include <filesystem>
#include <string>

namespace standalone
{
    bool save_ready();
    std::filesystem::path state_directory();
    std::filesystem::path connection_file();
    void bind_saves(const std::filesystem::path &appdata);
}

// AP loop started by the DLL bootstrap
int nst_client_main(std::string server, std::string slot);
bool nst_verified_executable(const std::filesystem::path &path);
