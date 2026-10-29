#include "../tools/native/provider.hpp"
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
    for (int i=1; i<argc; ++i) if (std::string_view(argv[i])=="--version") {
        std::cout << "arken-conversion-provider-fixtures-r6249\n";
        return 0;
    }
    try {
        namespace C=arken::conversion::cloudflare;
        C::Require(argc==3 && std::string_view(argv[1])=="--write-fixtures", "fixture_arguments_invalid");
        C::provider::Tests(C::fs::absolute(argv[2]));
        return 0;
    } catch (const std::exception&) {
        std::cerr << "provider_fixture_failed\n";
        return 1;
    }
}
