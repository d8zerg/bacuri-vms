#include "archive/verify.hpp"
#include <sodium.h>
#include <fstream>
#include <iostream>

int main(int argc, char** argv)
try {
    if (argc != 3 && argc != 6)
        throw std::invalid_argument("Usage: bacuri-vms-verify-export TRUSTED_PUBLIC_KEY.json EXTRACTED_DIRECTORY "
                                    "[POINT_ID BEGIN_UTC_US END_UTC_US]");
    if (sodium_init() < 0)
        throw std::runtime_error("Cannot initialize cryptography");
    std::ifstream key{argv[1]};
    const auto manifest = bacuri::vms::archive::verify_evidence(argv[2], nlohmann::json::parse(key));
    if (argc == 6 &&
        (manifest.at("point_id") != std::string{argv[3]} || manifest.at("begin_utc_us").dump() != argv[4] ||
         manifest.at("end_utc_us").dump() != argv[5]))
        throw std::invalid_argument("Evidence does not match the expected point and interval");
    std::cout << manifest.dump(2) << '\n';
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
