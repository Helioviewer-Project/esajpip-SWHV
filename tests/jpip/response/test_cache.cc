#include <climits>
#include <cstdlib>
#include <cstdint>
#include <iostream>

#include "jpip/response/cache_model.h"

using namespace std;

static void Check(bool condition, const char *message) {
    if (!condition) {
        cerr << message << endl;
        exit(EXIT_FAILURE);
    }
}

static void CheckCacheModel() {
    jpip::CacheModel model;
    const jpip::CacheModel &read_only = model;
    const jpip::DataBinClass classes[] = {
        jpip::DataBinClass::META_DATA,
        jpip::DataBinClass::MAIN_HEADER,
        jpip::DataBinClass::TILE_HEADER,
        jpip::DataBinClass::PRECINCT
    };

    for (jpip::DataBinClass bin_class : classes) {
        Check(read_only.GetDataBin(bin_class, 2, 3) == 0, "Nonempty initial cache model");
        Check(model.AddToDataBin(bin_class, 2, 3, 17) == 17, "Wrong cache-model increment");
        Check(read_only.GetDataBin(bin_class, 2, 3) == 17, "Wrong cached data-bin length");
        Check(model.AddToDataBin(bin_class, 2, 3, INT_MAX - 18) == INT_MAX - 1,
              "Wrong large cache-model increment");
        Check(model.AddToDataBin(bin_class, 2, 3, 2) == INT_MAX,
              "Cache-model increment overflowed");
        Check(model.AddToDataBin(bin_class, 2, 3, 0, true) == INT_MAX,
              "Incomplete terminal cache model");
    }

    jpip::CacheModel augmented;
    Check(augmented.AugmentDataBin(jpip::DataBinClass::META_DATA, 0, 0, 17) == 17,
          "Did not augment an empty cache-model entry");
    Check(augmented.AugmentDataBin(jpip::DataBinClass::META_DATA, 0, 0, 7) == 17,
          "Reduced an augmented cache-model entry");
    Check(augmented.AugmentDataBin(jpip::DataBinClass::META_DATA, 0, 0, 17) == 17,
          "Accumulated a repeated cache-model descriptor");
    Check(augmented.AugmentDataBin(jpip::DataBinClass::META_DATA, 0, 0, INT_MAX) ==
              INT_MAX,
          "Did not complete an augmented cache-model entry");

    jpip::CacheModel packed;
    packed.AddToDataBin(jpip::DataBinClass::PRECINCT, 0, 0, 0, true);
    packed.Pack();
    const jpip::CacheModel &read_only_packed = packed;
    Check(read_only_packed.GetDataBin(jpip::DataBinClass::PRECINCT, 0, 0) == INT_MAX,
          "Forgot a packed complete precinct");
    Check(read_only_packed.GetDataBin(jpip::DataBinClass::PRECINCT, 0, 1) == 0,
          "Absent precinct after packed prefix is complete");
    packed.SetFullMetadata();
    Check(read_only_packed.GetDataBin(jpip::DataBinClass::META_DATA, 0, 7) == INT_MAX,
          "Did not retain the complete metadata state");

    Check(model.GetDataBin(jpip::DataBinClass::EXTENDED_PRECINCT, 0, 0) == -1,
          "Accepted an unsupported cache-model data-bin class");
}

static void CheckSparsePacking() {
    jpip::CacheModel model;
    const jpip::CacheModel &view = model;
    using jpip::DataBinClass;
    model.AddToDataBin(DataBinClass::PRECINCT, 0, 0, 0, true);
    model.AddToDataBin(DataBinClass::PRECINCT, 0, 2, 0, true);
    model.AddToDataBin(DataBinClass::PRECINCT, 0, 3, 17);
    model.AddToDataBin(DataBinClass::PRECINCT, 1, 0, 9);
    model.AddToDataBin(DataBinClass::MAIN_HEADER, 0, 0, 5);
    model.AddToDataBin(DataBinClass::TILE_HEADER, 1, 0, 7);
    for (int repeat = 0; repeat < 3; ++repeat) {
        model.Pack();
        Check(view.GetDataBin(DataBinClass::PRECINCT, 0, 0) == INT_MAX &&
                      view.GetDataBin(DataBinClass::PRECINCT, 0, 1) == 0 &&
                      view.GetDataBin(DataBinClass::PRECINCT, 0, 2) == INT_MAX &&
                      view.GetDataBin(DataBinClass::PRECINCT, 0, 3) == 17 &&
                      view.GetDataBin(DataBinClass::PRECINCT, 0, 4) == 0,
              "Packing crossed a hole or changed its suffix");
        Check(view.GetDataBin(DataBinClass::PRECINCT, 1, 0) == 9 &&
                      view.GetDataBin(DataBinClass::MAIN_HEADER, 0, 0) == 5 &&
                      view.GetDataBin(DataBinClass::TILE_HEADER, 1, 0) == 7,
              "Packing changed another stream or header class");
    }
    model.AugmentDataBin(DataBinClass::PRECINCT, 0, 1, INT_MAX);
    model.Pack();
    Check(model.AddToDataBin(DataBinClass::PRECINCT, 0, 1, 3) == INT_MAX &&
                  model.AugmentDataBin(DataBinClass::PRECINCT, 0, 3, 7) == 17,
          "Updating a packed prefix or partial suffix changed cached bytes");
    model.AddToDataBin(DataBinClass::PRECINCT, 0, 3, 0, true);
    model.Pack();
    Check(view.GetDataBin(DataBinClass::PRECINCT, 0, 3) == INT_MAX &&
                  view.GetDataBin(DataBinClass::PRECINCT, 0, 4) == 0 &&
                  model.AddToDataBin(DataBinClass::PRECINCT, 0, 7, 11) == 11 &&
                  view.GetDataBin(DataBinClass::PRECINCT, 0, 6) == 0,
          "Emptying and regrowing packed storage lost its prefix or holes");
    for (DataBinClass cls : {DataBinClass::EXTENDED_PRECINCT, DataBinClass::TILE_DATA,
                            DataBinClass::EXTENDED_TILE})
        Check(model.GetDataBin(cls, 0, 0) == -1 &&
                      model.AddToDataBin(cls, 0, 0, 1) == -1 &&
                      model.AugmentDataBin(cls, 0, 0, 1) == -1,
              "An unsupported class changed cache state");
}

int main() {
    CheckSparsePacking();
    CheckCacheModel();
    return EXIT_SUCCESS;
}
