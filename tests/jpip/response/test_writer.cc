#include <climits>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <vector>

#include "jpip/response/databin_writer.h"

using namespace std;

static void Check(bool condition, const char *message) {
    if (!condition) {
        cerr << message << endl;
        exit(EXIT_FAILURE);
    }
}

static void CheckJPIPMessages() {
    char buf[128];
    jpip::Source file;
    jpip::DataBinWriter writer;
    writer.SetBuffer(buf, sizeof buf);
    writer.Write(jpip::DataBinClass::MAIN_HEADER, 0, 0, 0, file,
                 jpip::FileSegment::Null, true);
    writer.WriteEOR(jpip::EOR::WINDOW_DONE);

    const unsigned char expected[] = {0x70, 0x06, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00};
    Check(writer.Finalize() == sizeof expected, "Wrong JPIP message length");
    for (size_t i = 0; i < sizeof expected; ++i)
        Check(static_cast<unsigned char>(buf[i]) == expected[i], "The JPIP message format changed");

    char large_id_buf[6];
    jpip::DataBinWriter large_id_writer;
    large_id_writer.SetBuffer(large_id_buf, sizeof large_id_buf);
    Check(large_id_writer.Write(jpip::DataBinClass::PRECINCT, 0, 200, 0,
                                file, jpip::FileSegment::Null, true) ==
                  jpip::DataBinWriter::Result::WRITTEN,
          "Rejected a canonical two-byte Bin-ID message");
    const unsigned char large_id_expected[] = {0xf1, 0x48, 0x00,
                                                0x00, 0x00, 0x00};
    Check(large_id_writer.Finalize() == sizeof large_id_expected,
          "Wrong large Bin-ID message length");
    for (size_t i = 0; i < sizeof large_id_expected; ++i)
        Check(static_cast<unsigned char>(large_id_buf[i]) ==
                      large_id_expected[i],
              "Large Bin-ID was not encoded canonically");
}

static void CheckDataBinCapacity() {
    char tiny_buf[2];
    jpip::DataBinWriter tiny_writer;
    tiny_writer.SetBuffer(tiny_buf, sizeof tiny_buf);
    Check(!tiny_writer.WriteEOR(jpip::EOR::WINDOW_DONE),
          "Accepted an EOR message larger than the buffer");
    char eor_buf[3];
    tiny_writer.SetBuffer(eor_buf, sizeof eor_buf);
    Check(tiny_writer.WriteEOR(jpip::EOR::WINDOW_DONE),
          "Did not retry the EOR message in a new buffer");

    char buf[32];
    jpip::Source file;
    jpip::DataBinWriter writer;
    writer.SetBuffer(buf, sizeof buf);
    Check(writer.Write(jpip::DataBinClass::MAIN_HEADER, 0, 0, 0, file,
                       jpip::FileSegment(0, UINT64_MAX), true) ==
              jpip::DataBinWriter::Result::FULL,
          "Reported an oversized data-bin segment as written");
    Check(writer.Finalize() == 0, "Left a partial data-bin message in the buffer");

    const char payload[] = {'x'};
    jpip::Source short_source(payload, sizeof payload);
    writer.SetBuffer(buf, sizeof buf);
    Check(writer.Write(jpip::DataBinClass::MAIN_HEADER, 0, 0, 0, short_source,
                       jpip::FileSegment(0, 2), true) ==
              jpip::DataBinWriter::Result::FAILED,
          "Reported a short source-file read as written");
}

static void CheckCoalescedJPIPMessages() {
    char payload[200];
    for (size_t i = 0; i < sizeof payload; ++i)
        payload[i] = static_cast<char>(i + 1);
    jpip::Source file(payload, sizeof payload);

    char buf[128];
    jpip::DataBinWriter writer;
    writer.SetBuffer(buf, sizeof buf);
    writer.Write(jpip::DataBinClass::MAIN_HEADER, 0, 0, 0, file,
                 jpip::FileSegment(0, 2), false);
    writer.Write(jpip::DataBinClass::MAIN_HEADER, 0, 0, 2, file,
                 jpip::FileSegment(2, 3), true);
    writer.WriteEOR(jpip::EOR::WINDOW_DONE);

    const unsigned char expected[] = {0x70, 0x06, 0x00, 0x00, 0x05, 1, 2, 3, 4, 5, 0x00, 0x02, 0x00};
    Check(writer.Finalize() == sizeof expected, "Wrong coalesced JPIP message length");
    for (size_t i = 0; i < sizeof expected; ++i)
        Check(static_cast<unsigned char>(buf[i]) == expected[i], "Wrong coalesced JPIP message");

    char growing_buf[256];
    jpip::DataBinWriter growing_writer;
    growing_writer.SetBuffer(growing_buf, sizeof growing_buf);
    growing_writer.Write(jpip::DataBinClass::MAIN_HEADER, 0, 0, 0, file,
                         jpip::FileSegment(0, 100), false);
    growing_writer.Write(jpip::DataBinClass::MAIN_HEADER, 0, 0, 100, file,
                         jpip::FileSegment(100, 50), true);
    growing_writer.WriteEOR(jpip::EOR::WINDOW_DONE);

    Check(growing_writer.Finalize() == 159, "Wrong growing JPIP message length");
    const unsigned char growing_header[] = {0x70, 0x06, 0x00, 0x00, 0x81, 0x16};
    for (size_t i = 0; i < sizeof growing_header; ++i)
        Check(static_cast<unsigned char>(growing_buf[i]) == growing_header[i], "Wrong growing JPIP header");
    for (size_t i = 0; i < 150; ++i)
        Check(growing_buf[sizeof growing_header + i] == payload[i], "Wrong shifted JPIP payload");

    char tight_buf[155];
    jpip::DataBinWriter tight_writer;
    tight_writer.SetBuffer(tight_buf, sizeof tight_buf);
    Check(tight_writer.Write(jpip::DataBinClass::MAIN_HEADER, 0, 0, 0, file,
                             jpip::FileSegment(0, 100), false) ==
              jpip::DataBinWriter::Result::WRITTEN,
          "Rejected the complete JPIP message");
    Check(tight_writer.Write(jpip::DataBinClass::MAIN_HEADER, 0, 0, 100, file,
                             jpip::FileSegment(100, 50), true) ==
              jpip::DataBinWriter::Result::FULL,
          "Coalesced a message without space for its header");

    Check(tight_writer.Finalize() == 105, "Did not preserve the complete JPIP message");
    const unsigned char tight_header[] = {0x60, 0x06, 0x00, 0x00, 0x64};
    for (size_t i = 0; i < sizeof tight_header; ++i)
        Check(static_cast<unsigned char>(tight_buf[i]) == tight_header[i],
              "Corrupted the complete JPIP message");
    for (size_t i = 0; i < 100; ++i)
        Check(tight_buf[sizeof tight_header + i] == payload[i],
              "Corrupted the complete JPIP payload");

    char retry_buf[64];
    tight_writer.SetBuffer(retry_buf, sizeof retry_buf);
    Check(tight_writer.Write(jpip::DataBinClass::MAIN_HEADER, 0, 0, 100, file,
                             jpip::FileSegment(100, 50), true) ==
              jpip::DataBinWriter::Result::WRITTEN,
          "Did not retry the deferred JPIP message");
    Check(tight_writer.Finalize() == 53, "Wrong deferred JPIP message length");
    const unsigned char retry_header[] = {0x30, 0x64, 0x32};
    for (size_t i = 0; i < sizeof retry_header; ++i)
        Check(static_cast<unsigned char>(retry_buf[i]) == retry_header[i],
              "Corrupted the deferred JPIP message");
    for (size_t i = 0; i < 50; ++i)
        Check(retry_buf[sizeof retry_header + i] == payload[i + 100],
              "Corrupted the deferred JPIP payload");

    char class_buf[128];
    jpip::DataBinWriter class_writer;
    class_writer.SetBuffer(class_buf, sizeof class_buf);
    class_writer.Write(jpip::DataBinClass::MAIN_HEADER, 0, 0, 0, file,
                       jpip::FileSegment(0, 2), true);
    class_writer.Write(jpip::DataBinClass::META_DATA, 0, 0, 0, file,
                       jpip::FileSegment(2, 3), true);
    class_writer.WriteEOR(jpip::EOR::WINDOW_DONE);

    const unsigned char class_expected[] = {0x70, 0x06, 0x00, 0x00, 0x02, 1, 2,
                                            0x50, 0x08, 0x00, 0x03, 3, 4, 5,
                                            0x00, 0x02, 0x00};
    Check(class_writer.Finalize() == sizeof class_expected, "Wrong mixed-class JPIP message length");
    for (size_t i = 0; i < sizeof class_expected; ++i)
        Check(static_cast<unsigned char>(class_buf[i]) == class_expected[i], "Wrong mixed-class JPIP message");

    char stream_buf[128];
    jpip::DataBinWriter stream_writer;
    stream_writer.SetBuffer(stream_buf, sizeof stream_buf);
    stream_writer.Write(jpip::DataBinClass::MAIN_HEADER, 0, 0, 0, file,
                        jpip::FileSegment(0, 1), true);
    stream_writer.Write(jpip::DataBinClass::MAIN_HEADER, 1, 0, 0, file,
                        jpip::FileSegment(1, 1), true);

    const unsigned char stream_expected[] = {0x70, 0x06, 0x00, 0x00, 0x01, 1,
                                             0x70, 0x06, 0x01, 0x00, 0x01, 2};
    Check(stream_writer.Finalize() == sizeof stream_expected,
          "Wrong mixed-codestream JPIP message length");
    for (size_t i = 0; i < sizeof stream_expected; ++i)
        Check(static_cast<unsigned char>(stream_buf[i]) == stream_expected[i],
              "Wrong mixed-codestream JPIP message");

    char exact_buf[7];
    jpip::DataBinWriter exact_writer;
    exact_writer.SetBuffer(exact_buf, sizeof exact_buf);
    exact_writer.Write(jpip::DataBinClass::MAIN_HEADER, 0, 0, 0, file,
                       jpip::FileSegment(0, 2), true);

    const unsigned char exact_expected[] = {0x70, 0x06, 0x00, 0x00, 0x02, 1, 2};
    Check(exact_writer.Finalize() == sizeof exact_expected, "Did not fill the JPIP buffer exactly");
    for (size_t i = 0; i < sizeof exact_expected; ++i)
        Check(static_cast<unsigned char>(exact_buf[i]) == exact_expected[i], "Wrong exact-size JPIP message");
}

static void CheckMetadataPlaceHolder() {
    const unsigned char header[] = {0x00, 0x00, 0x00, 0x10, 'a', 's', 'o', 'c'};
    jpip::Source file(header, sizeof header);

    char buf[64];
    jpip::DataBinWriter writer;
    jpip::PlaceHolder place_holder(7, false,
                                       jpip::FileSegment(0, sizeof header));
    jpip::PlaceHolder large_place_holder(
            7, true, jpip::FileSegment(0, INT_MAX));
    Check(large_place_holder.length() == static_cast<uint64_t>(INT_MAX) + 44,
          "Truncated large place-holder length");
    writer.SetBuffer(buf, sizeof buf);
    writer.WritePlaceHolder(jpip::DataBinClass::META_DATA, 0, 0, 0, file,
                            place_holder, 0, true);

    const unsigned char expected[] = {
        0x70, 0x08, 0x00, 0x00, 0x1c,
        0x00, 0x00, 0x00, 0x1c, 'p', 'h', 'l', 'd',
        0x00, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07,
        0x00, 0x00, 0x00, 0x10, 'a', 's', 'o', 'c'
    };
    Check(writer.Finalize() == sizeof expected, "Wrong metadata place-holder length");
    for (size_t i = 0; i < sizeof expected; ++i)
        Check(static_cast<unsigned char>(buf[i]) == expected[i], "Wrong metadata place-holder");

    char exact_buf[sizeof expected];
    jpip::DataBinWriter exact_writer;
    exact_writer.SetBuffer(exact_buf, sizeof exact_buf);
    exact_writer.WritePlaceHolder(jpip::DataBinClass::META_DATA, 0, 0, 0, file,
                                  place_holder, 0, true);

    Check(exact_writer.Finalize() == sizeof expected, "Did not fill the place-holder buffer exactly");
    for (size_t i = 0; i < sizeof expected; ++i)
        Check(static_cast<unsigned char>(exact_buf[i]) == expected[i], "Wrong exact-size metadata place-holder");

    char partial_buf[64];
    jpip::DataBinWriter partial_writer;
    partial_writer.SetBuffer(partial_buf, sizeof partial_buf);
    partial_writer.WritePlaceHolder(jpip::DataBinClass::META_DATA, 0, 0, 0,
                                    file, place_holder, 3, true);
    const unsigned char partial_header[] = {0x70, 0x08, 0x00, 0x03, 0x19};
    Check(partial_writer.Finalize() ==
                  static_cast<ptrdiff_t>(sizeof partial_header +
                                         sizeof expected - 5 - 3),
          "Wrong partial metadata place-holder length");
    for (size_t i = 0; i < sizeof partial_header; ++i)
        Check(static_cast<unsigned char>(partial_buf[i]) == partial_header[i],
              "Wrong partial metadata place-holder header");
    for (size_t i = 0; i < sizeof expected - 5 - 3; ++i)
        Check(static_cast<unsigned char>(partial_buf[sizeof partial_header + i]) ==
                      expected[5 + 3 + i],
              "Wrong partial metadata place-holder payload");
}

static uint64_t DecodeInteger(const vector<unsigned char> &bytes, size_t &at) {
    uint64_t value = 0;
    size_t start = at;
    for (;;) {
        Check(at < bytes.size(), "Truncated reference VBAS");
        unsigned char byte = bytes[at++];
        Check(value <= (UINT64_MAX >> 7), "Overflow in reference VBAS");
        value = (value << 7) | (byte & 127);
        if (!(byte & 128)) break;
    }
    Check(at == start + 1 || (bytes[start] & 127) != 0,
          "Noncanonical leading zero in VBAS");
    return value;
}

static void CheckIntegerBoundaries() {
    struct Case { uint64_t value; size_t bin_bytes, integer_bytes; };
    const Case cases[] = {{0,1,1},{15,1,1},{16,2,1},{127,2,1},{128,2,2},
                          {2047,2,2},{2048,3,2},{16383,3,2},{16384,3,3},
                          {UINT32_MAX,5,5},{UINT64_MAX,10,10}};
    const unsigned char payload = 0xa5;
    jpip::Source source(&payload,1);
    for (const Case &test : cases) {
        size_t expected_length = test.bin_bytes + test.integer_bytes + 4;
        for (int spare : {-1,0,3}) {
            vector<char> buffer(expected_length + 5, static_cast<char>(0x55));
            jpip::DataBinWriter writer;
            writer.SetBuffer(buffer.data(), static_cast<int>(expected_length) + spare);
            jpip::DataBinWriter::Result result = writer.Write(jpip::DataBinClass::PRECINCT,
                    0, test.value, test.value, source, jpip::FileSegment(0,1), true);
            if (spare < 0) {
                Check(result == jpip::DataBinWriter::Result::FULL && writer.Finalize() == 0,
                      "An undersized header left a partial message");
                for (char byte : buffer) Check(byte == 0x55, "Rejected header wrote bytes");
                continue;
            }
            Check(result == jpip::DataBinWriter::Result::WRITTEN,
                  "A boundary header did not fit its exact buffer");
            Check(writer.Finalize() == static_cast<ptrdiff_t>(expected_length),
                  "A boundary header was not encoded at canonical length");
            vector<unsigned char> encoded(buffer.begin(), buffer.begin() + expected_length);
            size_t at = 1;
            unsigned char first = encoded[0];
            Check((first & 0x70) == 0x70, "First header omitted explicit class, stream or final flag");
            uint64_t bin = first & 15;
            if (first & 128) {
                for (;;) {
                    Check(at < encoded.size(), "Truncated reference Bin-ID");
                    unsigned char byte = encoded[at++];
                    Check(bin <= (UINT64_MAX >> 7), "Overflow in reference Bin-ID");
                    bin = (bin << 7) | (byte & 127);
                    if (!(byte & 128)) break;
                }
            }
            Check(bin == test.value && at == test.bin_bytes,
                  "Wrong boundary Bin-ID value or canonical length");
            Check(DecodeInteger(encoded, at) == jpip::PRECINCT && DecodeInteger(encoded, at) == 0 &&
                          DecodeInteger(encoded, at) == test.value && DecodeInteger(encoded, at) == 1 &&
                          at + 1 == encoded.size() && encoded[at] == payload,
                  "Boundary header or payload differs from its independent decoding");
            if (spare == 3) {
                Check(writer.WriteEOR(jpip::EOR::WINDOW_DONE) &&
                              writer.Finalize() == static_cast<ptrdiff_t>(expected_length + 3) &&
                              buffer[expected_length] == 0 &&
                              buffer[expected_length + 1] == jpip::EOR::WINDOW_DONE &&
                              buffer[expected_length + 2] == 0,
                      "Exact header-plus-EOR capacity changed framing");
            }
            for (size_t i = expected_length + (spare == 3 ? 3 : 0); i < buffer.size(); ++i)
                Check(buffer[i] == 0x55, "Header or EOR wrote beyond its capacity");
        }
    }
}

int main() {
    CheckIntegerBoundaries();
    CheckJPIPMessages();
    CheckDataBinCapacity();
    CheckCoalescedJPIPMessages();
    CheckMetadataPlaceHolder();
    return EXIT_SUCCESS;
}
