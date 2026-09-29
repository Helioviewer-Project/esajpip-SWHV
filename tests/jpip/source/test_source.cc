#include <climits>
#include <cstdlib>
#include <cstdint>
#include <iostream>

#include "jpip/source/source.h"

using namespace std;

static void Check(bool condition, const char *message) {
    if (!condition) {
        cerr << message << endl;
        exit(EXIT_FAILURE);
    }
}

static void verify_source_ranges() {
    const char bytes[] = {'a', 'b', 'c', 'd'};
    const jpip::Source source(bytes, sizeof bytes);
    char result[2] = {};
    Check(source.Read(2, result, 2) && result[0] == 'c' && result[1] == 'd' &&
          source.Read(0, result, 2) && result[0] == 'a' && result[1] == 'b',
          "absolute source reads retained cursor state");
    Check(!source.Read(3, result, 2) && !source.Read(UINT64_MAX, result, 1) &&
          !source.Read(0, result, UINT64_MAX) && result[0] == 'a' && result[1] == 'b',
          "invalid source range changed the destination");
    Check(source.Read(4, result, 0) && !source.Read(5, result, 0),
          "source end boundary was clamped");
}

int main() {
    verify_source_ranges();
    return EXIT_SUCCESS;
}
