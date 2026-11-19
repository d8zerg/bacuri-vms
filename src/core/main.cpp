#include <cstdio>
#include <cstdlib>
#include <exception>
#include <print>
#include <string_view>

int main(int argc, char* argv[])
try
{
    if (argc == 2 && std::string_view{argv[1]} == "--version") {
        std::println("bacuri.vms {}", BACURI_VMS_VERSION);
        return EXIT_SUCCESS;
    }

    std::fputs("Usage: bacuri-vms-core --version\n", stderr);
    return EXIT_FAILURE;
}
catch (const std::exception& error)
{
    std::fputs(error.what(), stderr);
    std::fputc('\n', stderr);
    return EXIT_FAILURE;
}
