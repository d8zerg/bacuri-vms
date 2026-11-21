#include "maintenance.hpp"
#include <iostream>
#include <unistd.h>
using namespace bacuri::vms::maintenance;
int main(int argc, char** argv)
try {
    ensure(argc == 3 && geteuid() == 0, "Usage as root: bacuri-install TRUSTED_KEYRING BUNDLE_DIRECTORY");
    ensure(!fs::exists("/var/lib/bacuri-maintenance/pending"), "Resume pending maintenance with bacuri-maintain");
    Bundle bundle({.keyring = argv[1], .directory = argv[2]});
    auto version =
        capture({"/usr/bin/dpkg-query", "-W", "-f=${db:Status-Status} ${Version}", "bacuri-vms-basic"}, true);
    ensure(!version.starts_with("installed ") || version == "installed " + bundle.version,
           "Use bacuri-maintain to change the installed release");
    bundle.install();
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
