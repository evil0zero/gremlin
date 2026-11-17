#include <array>
#include <concepts>
#include <span>
#include <string_view>

using namespace std;

template<integral T>
constexpr T sum(span<const T> values) {
    T total = T();
    for (T value : values) {
        total += value;
    }
    return total;
}

int main(int argc, char** argv) {
    constexpr array values{1, 2, 3};
    static_assert(sum<int>(values) == 6);
    return argc == 1 && !string_view(argv[0]).empty() ? 0 : 1;
}
