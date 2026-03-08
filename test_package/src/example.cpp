#include "emcpp.h"
#include <vector>
#include <string>

int main() {
    emcpp();

    std::vector<std::string> vec;
    vec.push_back("test_package");

    emcpp_print_vector(vec);
}
