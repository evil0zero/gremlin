#include "line_count.hpp"

#include <istream>
#include <string>

using namespace std;

uintmax_t count_lines(istream& input) {
    uintmax_t lines = uintmax_t();
    string line;
    while (getline(input, line)) ++lines;
    return lines;
}
