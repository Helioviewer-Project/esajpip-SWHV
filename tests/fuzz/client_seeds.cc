#include "client_support.h"

static void write(const std::string &path, const client_fuzz::Bytes &bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    client_fuzz::require(static_cast<bool>(file), "cannot write client fuzz seed");
}

int main(int argc, char **argv) {
    using namespace client_fuzz;
    require(argc == 3, "client_seeds needs response and source corpus directories");
    write(std::string(argv[1]) + "/window-done", {0, HV_EOR_WINDOW_DONE, 0});
    write(std::string(argv[1]) + "/truncated-header", {0xE0});
    for (unsigned selector = 0; selector < 3; selector++) {
        Context &fixture = context(selector);
        for (size_t index = 0; index < fixture.image.GetNumCodestreams(); index++) {
            const jpip::CodingParameters &coding = *fixture.image.GetCodingParameters(static_cast<int>(index));
            for (int reduce : {0, coding.num_levels})
                for (int layers : {1, coding.num_layers}) {
                    jpip::ResponseRequest request;
                    request.AddStream(index, index);
                    request.layers = layers;
                    request.has.fsiz = true;
                    request.resolution_size = jpip::Size(((coding.size.x - 1) >> reduce) + 1,
                                                        ((coding.size.y - 1) >> reduce) + 1);
                    std::string name = std::to_string(selector) + "-" + std::to_string(index) + "-" +
                                       std::to_string(reduce) + "-" + std::to_string(layers);
                    jpip::DataBinServer fresh;
                    Bytes raw = response(fresh, fixture.image, fixture.sources, request);
                    write(std::string(argv[1]) + "/" + name, raw);
                    jpip::DataBinServer channel;
                    jpip::ResponseRequest opening;
                    opening.AddStream(0, fixture.image.GetNumCodestreams() - 1);
                    opening.layers = 0;
                    response(channel, fixture.image, fixture.sources, opening);
                    Bytes body = response(channel, fixture.image, fixture.sources, request);
                    Bytes input = {static_cast<uint8_t>(selector), static_cast<uint8_t>(index),
                                   static_cast<uint8_t>(reduce), static_cast<uint8_t>(layers)};
                    input.insert(input.end(), body.begin(), body.end());
                    write(std::string(argv[2]) + "/" + name, input);
                    input.resize(input.size() - 1);
                    write(std::string(argv[2]) + "/" + name + "-truncated", input);
                }
        }
    }
    return 0;
}
