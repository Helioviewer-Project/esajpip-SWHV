#include <climits>
#include <cstdlib>
#include <cstdint>
#include <iostream>

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

int main() {
    CheckWOIPackets();
    return EXIT_SUCCESS;
}
