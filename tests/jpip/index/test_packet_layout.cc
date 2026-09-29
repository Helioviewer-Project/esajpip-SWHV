#include <climits>
#include <cstdlib>
#include <cstdint>
#include <iostream>

#include "jpip/index/coding_parameters.h"
#include "jpip/index/packet_index.h"

using namespace std;

static void Check(bool condition, const char *message) {
    if (!condition) {
        cerr << message << endl;
        exit(EXIT_FAILURE);
    }
}

static void CheckProgressionIndexes() {
    jpip::CodingParameters params;
    params.size = jpip::Size(8, 8);
    params.num_levels = 2;
    params.num_layers = 2;
    params.num_components = 3;
    params.resolutions.emplace_back(1, 1);
    params.resolutions.emplace_back(2, 2);
    params.resolutions.emplace_back(4, 4);
    Check(params.FillPrecinctCounts(),
          "Rejected valid progression coding parameters");
    for (const jpip::CodingParameters::Resolution &resolution : params.resolutions)
        Check(resolution.num_precincts == jpip::Size(2, 2),
              "Wrong precomputed precinct count");

    jpip::Packet packet(1, 1, 2, jpip::Size(1, 1));
    params.progression = jpip::CodingParameters::LRCP_PROGRESSION;
    Check(params.GetProgressionIndex(packet) == 59, "Wrong LRCP packet index");
    params.progression = jpip::CodingParameters::RLCP_PROGRESSION;
    Check(params.GetProgressionIndex(packet) == 47, "Wrong RLCP packet index");
    params.progression = jpip::CodingParameters::RPCL_PROGRESSION;
    Check(params.GetProgressionIndex(packet) == 47, "Wrong RPCL packet index");
    params.progression = jpip::CodingParameters::PCRL_PROGRESSION;
    Check(params.GetProgressionIndex(packet) == 69, "Wrong PCRL packet index");
    params.progression = jpip::CodingParameters::CPRL_PROGRESSION;
    Check(params.GetProgressionIndex(packet) == 69, "Wrong CPRL packet index");
    Check(params.GetPrecinctDataBinId(packet) == 23, "Wrong precinct data-bin ID");

    params.progression = 5;
    Check(params.GetProgressionIndex(packet) == -1,
          "An invalid progression order selected a packet");

    jpip::CodingParameters spatial;
    spatial.size = jpip::Size(11, 9);
    spatial.num_levels = 2;
    spatial.num_layers = 2;
    spatial.num_components = 3;
    spatial.resolutions.emplace_back(1, 2);
    spatial.resolutions.emplace_back(2, 1);
    spatial.resolutions.emplace_back(1, 4);
    Check(spatial.FillPrecinctCounts(),
          "Rejected valid spatial progression parameters");

    int num_packets = 0;
    for (const jpip::CodingParameters::Resolution &resolution : spatial.resolutions)
        num_packets += resolution.num_precincts.x * resolution.num_precincts.y;
    num_packets *= spatial.num_components * spatial.num_layers;

    int expected = 0;
    spatial.progression = jpip::CodingParameters::PCRL_PROGRESSION;
    for (int y = 0; y < spatial.size.y; ++y) {
        for (int x = 0; x < spatial.size.x; ++x) {
            for (int c = 0; c < spatial.num_components; ++c) {
                for (int r = 0; r <= spatial.num_levels; ++r) {
                    int step_x = spatial.resolutions[r].precinct_size.x <<
                                 (spatial.num_levels - r);
                    int step_y = spatial.resolutions[r].precinct_size.y <<
                                 (spatial.num_levels - r);
                    if (x % step_x != 0 || y % step_y != 0)
                        continue;
                    for (int l = 0; l < spatial.num_layers; ++l) {
                        packet = jpip::Packet(l, r, c,
                                jpip::Point(x / step_x, y / step_y));
                        Check(spatial.GetProgressionIndex(packet) == expected++,
                              "PCRL packet sequence does not follow the reference grid");
                    }
                }
            }
        }
    }
    Check(expected == num_packets, "PCRL packet sequence is incomplete");

    expected = 0;
    spatial.progression = jpip::CodingParameters::CPRL_PROGRESSION;
    for (int c = 0; c < spatial.num_components; ++c) {
        for (int y = 0; y < spatial.size.y; ++y) {
            for (int x = 0; x < spatial.size.x; ++x) {
                for (int r = 0; r <= spatial.num_levels; ++r) {
                    int step_x = spatial.resolutions[r].precinct_size.x <<
                                 (spatial.num_levels - r);
                    int step_y = spatial.resolutions[r].precinct_size.y <<
                                 (spatial.num_levels - r);
                    if (x % step_x != 0 || y % step_y != 0)
                        continue;
                    for (int l = 0; l < spatial.num_layers; ++l) {
                        packet = jpip::Packet(l, r, c,
                                jpip::Point(x / step_x, y / step_y));
                        Check(spatial.GetProgressionIndex(packet) == expected++,
                              "CPRL packet sequence does not follow the reference grid");
                    }
                }
            }
        }
    }
    Check(expected == num_packets, "CPRL packet sequence is incomplete");
}

static void CheckPacketIndexBounds() {
    jpip::PacketIndex index;
    Check(!index.Add(jpip::FileSegment(63, 1)),
          "Packet index accepted an offset reserved for segment indexes");
    Check(!index.Add(jpip::FileSegment(
                  static_cast<uint64_t>(UINT32_MAX) + 1, 1)),
          "Packet index truncated an offset wider than its storage");
    Check(index.Add(jpip::FileSegment(64, 1)) &&
                  index.Add(jpip::FileSegment(65, 1)),
          "Packet index rejected representable offsets");
    jpip::FileSegment segment;
    Check(index.Get(0, &segment) && segment == jpip::FileSegment(64, 1) &&
                  index.Get(1, &segment) && segment == jpip::FileSegment(65, 1),
          "Packet index returned the wrong bounded segment");

    jpip::PacketIndex fragmented;
    for (uint64_t i = 0; i < jpip::PacketIndex::MAX_SEGMENTS; ++i)
        Check(fragmented.Add(jpip::FileSegment(64 + i * 2, 1)),
              "Packet index rejected a supported segment count");
    Check(!fragmented.Add(jpip::FileSegment(
                  64 + jpip::PacketIndex::MAX_SEGMENTS * 2, 1)),
          "Packet index accepted more segment indexes than it can represent");
}

static void CheckResolutionSelection() {
    jpip::CodingParameters params;
    params.size = jpip::Size(4097, 4095);
    params.num_levels = 5;
    jpip::Size selected;

    Check(params.GetClosestResolution(jpip::Size(2050, 2048), &selected) == 4 &&
              selected == jpip::Size(2049, 2048),
          "Wrong closest resolution size");
    Check(params.GetRoundUpResolution(jpip::Size(2048, 2048), &selected) == 4 &&
              selected == jpip::Size(2049, 2048),
          "Wrong round-up resolution size");
    Check(params.GetRoundDownResolution(jpip::Size(2048, 2048), &selected) == 3 &&
              selected == jpip::Size(1025, 1024),
          "Wrong round-down resolution size");

    params.size = jpip::Size(100, 100);
    params.num_levels = 1;
    Check(params.GetClosestResolution(jpip::Size(77, 77), &selected) == 0 &&
              selected == jpip::Size(50, 50),
          "Closest resolution was not selected by area");
    Check(params.GetClosestResolution(jpip::Size(125, 50), &selected) == 1 &&
              selected == jpip::Size(100, 100),
          "Closest resolution tie did not select the larger image");

    params.size = jpip::Size(INT_MAX, INT_MAX);
    params.num_levels = 32;
    Check(params.GetRoundDownResolution(jpip::Size(1, 1), &selected) == 1 &&
              selected == jpip::Size(1, 1),
          "Wrong resolution size at the decomposition limit");

}

int main() {
    CheckProgressionIndexes();
    CheckPacketIndexBounds();
    CheckResolutionSelection();
    return EXIT_SUCCESS;
}
