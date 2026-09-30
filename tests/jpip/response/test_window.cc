#include <climits>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <set>
#include <vector>

#include "jpip/response/woi_composer.h"

using namespace std;

static void Check(bool condition, const char *message) {
    if (!condition) {
        cerr << message << endl;
        exit(EXIT_FAILURE);
    }
}

static void CheckWOIPackets() {
    jpip::CodingParameters coding_parameters;
    coding_parameters.size = jpip::Size(1, 1);
    coding_parameters.num_levels = 0;
    coding_parameters.num_layers = 2;
    coding_parameters.num_components = 2;
    coding_parameters.resolutions.emplace_back(1, 1);
    Check(coding_parameters.FillPrecinctCounts(),
          "Rejected valid WOI coding parameters");

    jpip::WOIComposer composer;
    composer.Reset(&coding_parameters, jpip::WOI(jpip::Point(0, 0), jpip::Size(1, 1), 0));

    int packets = 1;
    while (composer.GetNextPacket(&coding_parameters))
        packets++;
    Check(packets == 4, "WOI navigation repeated the final packet");

    jpip::WOI layered(jpip::Point(0, 0), jpip::Size(1, 1), 0);
    for (int limit : {0, 1, 2, INT_MAX}) {
        layered.layers = limit;
        composer.Reset(&coding_parameters, layered);
        packets = 0;
        while (composer.HasPacket()) {
            Check(composer.GetCurrentPacket().layer < min(limit, 2),
                  "WOI exceeded its layer boundary");
            ++packets;
            composer.GetNextPacket(&coding_parameters);
        }
        Check(packets == min(limit, 2) * 2, "Wrong layer-limited packet count");
    }
    jpip::WOI different = layered;
    different.layers = 1;
    Check(different != layered, "Layer limit did not distinguish windows");

    jpip::CodingParameters boundary_parameters;
    boundary_parameters.size = jpip::Size(512, 1);
    boundary_parameters.num_levels = 0;
    boundary_parameters.num_layers = 1;
    boundary_parameters.num_components = 1;
    boundary_parameters.resolutions.emplace_back(256, 1);
    Check(boundary_parameters.FillPrecinctCounts(),
          "Rejected valid precinct-boundary parameters");

    composer.Reset(&boundary_parameters,
                   jpip::WOI(jpip::Point(256, 0),
                             jpip::Size(1, 1), 0));
    Check(composer.GetCurrentPacket().precinct_xy == jpip::Point(1, 0) &&
                  !composer.GetNextPacket(&boundary_parameters),
          "Selected the preceding precinct for an aligned window");

    composer.Reset(&boundary_parameters,
                   jpip::WOI(jpip::Point(0, 0),
                             jpip::Size(257, 1), 0));
    Check(composer.GetCurrentPacket().precinct_xy == jpip::Point(0, 0) &&
                  composer.GetNextPacket(&boundary_parameters) &&
                  composer.GetCurrentPacket().precinct_xy == jpip::Point(1, 0) &&
                  !composer.GetNextPacket(&boundary_parameters),
          "Omitted the final precinct intersecting a window");

    jpip::CodingParameters offset_parameters;
    offset_parameters.size = jpip::Size(129, 1);
    offset_parameters.origin = jpip::Point(1, 0);
    offset_parameters.num_levels = 1;
    jpip::Size offset_level_size;
    Check(offset_parameters.GetRoundDownResolution(
              jpip::Size(64, 1), &offset_level_size) == 0 &&
              offset_level_size == jpip::Size(64, 1),
          "Resolution size did not follow T.808 Equation C-1");

    jpip::CodingParameters scaled_parameters;
    scaled_parameters.num_levels = 2;
    scaled_parameters.num_layers = 1;
    scaled_parameters.num_components = 1;
    for (int i = 0; i <= scaled_parameters.num_levels; ++i)
        scaled_parameters.resolutions.emplace_back(1, 1);

    composer.Reset(&scaled_parameters,
                   jpip::WOI(jpip::Point(1, 0),
                             jpip::Size(1, 1), 1));
    Check(composer.GetCurrentPacket().precinct_xy == jpip::Point(0, 0) &&
                  composer.GetNextPacket(&scaled_parameters) &&
                  composer.GetCurrentPacket().resolution == 1 &&
                  composer.GetCurrentPacket().precinct_xy == jpip::Point(1, 0),
          "Mapped a reduced-resolution window to the wrong precinct");

    jpip::CodingParameters limit_parameters;
    limit_parameters.num_levels = 32;
    limit_parameters.num_layers = 1;
    limit_parameters.num_components = 1;
    limit_parameters.resolutions.emplace_back(1, 1);
    composer.Reset(&limit_parameters,
                   jpip::WOI(jpip::Point(0, 0),
                             jpip::Size(1, 1), 0));
    Check(composer.GetCurrentPacket().precinct_xy == jpip::Point(0, 0),
          "Mapped the decomposition-limit window to the wrong precinct");
}

static void CheckWindowSequence() {
    jpip::CodingParameters coding;
    coding.size = jpip::Size(17, 11);
    coding.num_levels = 2;
    coding.num_components = 3;
    coding.num_layers = 2;
    coding.resolutions.emplace_back(2, 1);
    coding.resolutions.emplace_back(2, 2);
    coding.resolutions.emplace_back(4, 2);
    Check(coding.FillPrecinctCounts(), "Could not build 2D window geometry");
    jpip::WOIComposer composer;
    Check(!composer.HasPacket() && !composer.GetNextPacket(&coding),
          "A new composer exposed a packet");
    struct Case { int x, y, width, height, resolution; };
    const Case cases[] = {{0,0,17,11,2}, {4,2,1,1,2}, {3,1,2,2,2},
                          {16,10,1,1,2}, {1,1,7,4,1}, {0,0,5,3,0}};
    for (const Case &test : cases) {
        vector<jpip::Packet> expected;
        // Enumerate the window's pixels and deduplicate intersecting precincts.
        // The reference uses no production coordinate or progression helpers.
        for (int layer = 0; layer < coding.num_layers; ++layer)
            for (int resolution = 0; resolution <= test.resolution; ++resolution) {
                set<pair<int,int>> precincts;
                int scale = 1 << (test.resolution - resolution);
                const jpip::Size &size = coding.resolutions[resolution].precinct_size;
                for (int y = test.y; y < test.y + test.height; ++y)
                    for (int x = test.x; x < test.x + test.width; ++x)
                        precincts.emplace(y / scale / size.y, x / scale / size.x);
                for (int component = 0; component < coding.num_components; ++component)
                    for (const pair<int,int> &xy : precincts)
                        expected.emplace_back(layer, resolution, component,
                                              jpip::Point(xy.second, xy.first));
            }
        composer.Reset(&coding, jpip::WOI(jpip::Point(test.x,test.y),
                                        jpip::Size(test.width,test.height), test.resolution));
        for (size_t i = 0; i < expected.size(); ++i) {
            Check(composer.HasPacket(), "Window traversal stopped before its final packet");
            const jpip::Packet &actual = composer.GetCurrentPacket();
            const jpip::Packet &want = expected[i];
            Check(actual.layer == want.layer && actual.resolution == want.resolution &&
                          actual.component == want.component && actual.precinct_xy == want.precinct_xy,
                  "Window traversal differs from the independent LRCP sequence");
            Check(composer.GetNextPacket(&coding) == (i + 1 < expected.size()),
                  "Window traversal has the wrong completion boundary");
        }
        Check(!composer.HasPacket() && !composer.GetNextPacket(&coding),
              "Exhausted window exposed another packet");
        // Reset again before finishing the first layer, as a new request does.
        composer.Reset(&coding, jpip::WOI(jpip::Point(0,0),jpip::Size(1,1),0));
    }
}

int main() {
    CheckWindowSequence();
    CheckWOIPackets();
    return EXIT_SUCCESS;
}
