#include "line_count.hpp"

#include <fstream>
#include <iostream>

using namespace std;

int main(int argc, char** argv) {
    if (argc != 2) {
        cerr << "Expected one input file\n";
        return 2;
    }
    ifstream input(argv[1], ios::binary);
    if (!input) {
        cerr << "Could not open input\n";
        return 3;
    }
    const auto lines = count_lines(input);
    if (input.bad() || !input.eof()) {
        cerr << "Could not read input\n";
        return 3;
    }
    cout << lines << endl;
}
