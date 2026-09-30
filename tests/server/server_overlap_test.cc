#include "server_fixture.h"

using namespace server_test;

static void CheckOverlap(uint16_t port, const string &directory,
                         const jpp_test::Expected &expected, bool disconnect_waiter) {
    test_case = disconnect_waiter ? "overlap/waiting-disconnect" : "overlap/queued-response";
    int active = Connect(port);
    Check(active >= 0, "Could not connect active client");
    int receive_size = 64 * 1024;
    Check(setsockopt(active, SOL_SOCKET, SO_RCVBUF, &receive_size, sizeof receive_size) == 0,
          "Could not constrain the active client's receive buffer");
    SendRequest(active, "/overlap.jp2?cnew=http&len=3");
    Response opened = ReadResponse(active);
    Check(opened.headers.find("200 OK") != string::npos &&
                  opened.body == string("\0\4\0", 3), "Could not create overlap channel");
    string cid = ChannelId(opened.headers);
    SendRequest(active, "/jpip?cid=" + cid + "&stream=0&fsiz=260,1&metareq=[*]!!");
    string active_input;
    while (active_input.find("\r\n\r\n") == string::npos)
        Check(ReceiveMore(active, &active_input), "Active response never started");
    Check(active_input.find("HTTP/1.1 200 OK") == 0, "Active response did not send 200");

    int waiting = Connect(port);
    Check(waiting >= 0, "Could not connect waiting client");
    string waiting_target = "/jpip?cid=" + cid + "&stream=0&fsiz=260,1&len=61&handled";
    SendRequest(waiting, waiting_target);
    // Logging happens in Route. Once its record is visible, a subsequent
    // request cannot overtake this one's placement in the waiting slot.
    WaitForLog(directory, "Request: " + waiting_target + "\n");
    int busy = Connect(port);
    Check(busy >= 0, "Could not connect third client");
    SendRequest(busy, "/jpip?cid=" + cid + "&len=62");
    Response refused = ReadResponse(busy);
    Check(refused.headers.find("503 Service Unavailable") != string::npos &&
                  refused.body == "JPIP channel is busy", "Third concurrent request was not refused as busy");
    CheckClosed(busy, 1000, "Busy response retained its connection");
    close(busy);
    if (disconnect_waiter) {
        shutdown(waiting, SHUT_RDWR);
        close(waiting);
    }
    receive_size = 1024 * 1024;
    Check(setsockopt(active, SOL_SOCKET, SO_RCVBUF, &receive_size, sizeof receive_size) == 0,
          "Could not enlarge the receive buffer for draining");
    Response generated = ReadResponse(active, &active_input);
    jpp_test::Cache cache(expected);
    Check(cache.Read(generated.body) == 2 && cache.Complete(),
          "Overlapping requests changed active response bytes or completion");
    if (!disconnect_waiter) {
        Response queued = ReadResponse(waiting);
        Check(queued.headers.find("200 OK") != string::npos &&
                      queued.headers.find("JPIP-handled:") != string::npos &&
                      queued.body == string("\0\2\0", 3),
              "Waiting request did not resume with the completed channel cache");
        close(waiting);
    }
    SendRequest(active, "/jpip?cid=" + cid + "&stream=0&fsiz=260,1");
    Response repeated = ReadResponse(active, &active_input);
    Check(repeated.body == string("\0\2\0", 3),
          "Disconnecting or completing the waiter changed the channel cache");
    SendRequest(active, "/jpip?cclose=" + cid);
    Check(ReadResponse(active).headers.find("200 OK") != string::npos,
          "Could not close overlap channel");
    CheckClosed(active, 1000, "Closed overlap channel retained its connection");
    close(active);
}

static void CheckActiveDisconnect(uint16_t port, const string &directory) {
    test_case = "overlap/active-disconnect";
    int active = Connect(port);
    Check(active >= 0, "Could not connect disconnecting client");
    int receive_size = 64 * 1024;
    Check(setsockopt(active, SOL_SOCKET, SO_RCVBUF, &receive_size, sizeof receive_size) == 0,
          "Could not constrain disconnecting client buffer");
    SendRequest(active, "/overlap.jp2?cnew=http&len=3");
    string cid = ChannelId(ReadResponse(active).headers);
    SendRequest(active, "/jpip?cid=" + cid + "&fsiz=260,1&metareq=[*]!!");
    string partial;
    while (partial.find("\r\n\r\n") == string::npos)
        Check(ReceiveMore(active, &partial), "Disconnect response never started");
    int waiting = Connect(port);
    Check(waiting >= 0, "Could not connect queued disconnect client");
    string target = "/jpip?cid=" + cid + "&len=63&handled";
    SendRequest(waiting, target);
    WaitForLog(directory, "Request: " + target + "\n");
    shutdown(active, SHUT_RDWR);
    close(active);
    Response rejected = ReadResponse(waiting);
    Check(rejected.headers.find("503 Service Unavailable") != string::npos &&
                  rejected.body == "JPIP channel has ended",
          "Active disconnect did not reject the waiting exchange");
    CheckClosed(waiting, 1000, "Rejected waiting exchange retained its connection");
    close(waiting);
    int probe = Connect(port);
    Check(probe >= 0, "Could not connect after active disconnect");
    SendRequest(probe, "/jpip?cid=" + cid);
    Response ended = ReadResponse(probe);
    Check(ended.headers.find("503 Service Unavailable") != string::npos &&
                  (ended.body == "JPIP channel does not exist" || ended.body == "JPIP channel has ended"),
          "Disconnected channel accepted another request");
    close(probe);
}

static void CheckGenerationFailures(uint16_t port) {
    test_case = "generation/before-headers";
    int early = Connect(port);
    Check(early >= 0, "Could not connect for early generation failure");
    SendRequest(early, "/failure.jp2?cnew=http&fsiz=4096,4096&model=M0,Hm,H0");
    Response rejected = ReadResponse(early);
    Check(rejected.headers.find("HTTP/1.1 500 Internal Server Error") == 0 &&
                  rejected.body.find("plt.coverage") != string::npos,
          "Generation failure before headers did not return its diagnostic in 500");
    CheckClosed(early, 1000, "Early generation failure retained its connection");
    close(early);

    test_case = "generation/after-headers";
    int late = Connect(port);
    Check(late >= 0, "Could not connect for late generation failure");
    SendRequest(late, "/failure.jp2?cnew=http&fsiz=4096,4096&metareq=[*]!!");
    string raw;
    char buffer[64000];
    for (;;) {
        ssize_t length = recv(late, buffer, sizeof buffer, 0);
        if (length < 0 && errno == EINTR) continue;
        if (length < 0 && errno == ECONNRESET) break;
        Check(length >= 0, "Failed response did not close before the read deadline");
        if (length == 0) break;
        raw.append(buffer, static_cast<size_t>(length));
        Check(raw.size() < 16 * 1024 * 1024, "Failed response exceeded fixture bound");
    }
    close(late);
    size_t head_end = raw.find("\r\n\r\n");
    Check(head_end != string::npos && raw.find("HTTP/1.1 200 OK") == 0 &&
                  raw.find("HTTP/1.1", 1) == string::npos &&
                  raw.substr(0,head_end).find("Transfer-Encoding: chunked") != string::npos,
          "Late generation failure rewrote HTTP status or lost chunked headers");
    string cid = ChannelId(raw.substr(0,head_end));
    size_t at = head_end + 4;
    size_t complete_chunks = 0;
    while (at < raw.size()) {
        size_t end = raw.find("\r\n", at);
        if (end == string::npos) break;
        size_t length = ResponseLength(raw.substr(at,end-at),16);
        Check(length != 0, "Failed response incorrectly sent a terminal HTTP chunk");
        at = end + 2;
        if (length + 2 > raw.size() - at) break;
        Check(raw.compare(at+length,2,"\r\n") == 0, "Failed response damaged a complete HTTP chunk");
        at += length + 2;
        ++complete_chunks;
    }
    Check(complete_chunks > 0, "Late-failure fixture did not deliver any data before failing");
    int probe = Connect(port);
    Check(probe >= 0, "Could not connect after late generation failure");
    SendRequest(probe, "/jpip?cid=" + cid);
    Response ended = ReadResponse(probe);
    Check(ended.headers.find("503 Service Unavailable") != string::npos,
          "Failed response left its channel serving requests");
    close(probe);
    int healthy = Connect(port);
    Check(healthy >= 0, "Could not reconnect after generation failure");
    SendRequest(healthy, "/wire-frame0.jp2?cnew=http&len=3");
    Response opened = ReadResponse(healthy);
    Check(opened.headers.find("200 OK") != string::npos,
          "Generation failure prevented an independent channel from opening");
    SendRequest(healthy, "/jpip?cclose=" + ChannelId(opened.headers));
    Check(ReadResponse(healthy).headers.find("200 OK") != string::npos,
          "Could not close healthy channel after failure");
    close(healthy);
}

int main() {
    InitializeHarness();
    char name[] = "/tmp/esajpip-overlap-XXXXXX";
    Check(mkdtemp(name) != NULL, "Could not create overlap fixture directory");
    string directory = name;
    test_directory = directory;
    jpp_test::Fixture fixture = jpp_test::MakeFixture(directory);
    ifstream input(directory + "/wire-frame0.jp2", ios::binary);
    Check(input.good(), "Could not read overlap codestream");
    string frame((istreambuf_iterator<char>(input)), istreambuf_iterator<char>());
    string xml = jpp_test::Box("xml ", string(8 * 1024 * 1024, 'x'));
    string preamble = jpp_test::Preamble("jp2 ");
    string codestream_box = frame.substr(preamble.size());
    string movie = preamble + xml + codestream_box;
    WriteFile(directory + "/overlap.jp2", movie.data(), movie.size());
    jpp_test::Expected expected;
    for (const auto &bin : fixture.bins)
        if (get<0>(bin.first) != 8 && get<1>(bin.first) == 0) expected.insert(bin);
    expected[jpp_test::Key(8,0,0)] = preamble + xml + jpp_test::Placeholder(codestream_box,0);

    ifstream bad_input(string(VECTORS) + "/jp2-rule-plt.sum-exceeds-data-23.jp2", ios::binary);
    Check(bad_input.good(), "Could not read deferred failure fixture");
    string bad((istreambuf_iterator<char>(bad_input)), istreambuf_iterator<char>());
    string failure = preamble + xml + bad.substr(preamble.size());
    WriteFile(directory + "/failure.jp2", failure.data(), failure.size());

    uint16_t port = ReservePort();
    string text = "[listen]\nport = " + to_string(port) + "\naddress = 127.0.0.1\n"
            "[jpip]\nimage_directory = " + directory + "\nchunk_size = 64000\n"
            "[connections]\ninitial_timeout = 5\ntimeout = 10\nlimit = 4\n"
            "[channels]\nlimit = 4\n"
            "[logging]\ndirectory =\nfile_enabled = false\nrequests = true\n";
    WriteFile(directory + "/server.ini", text.data(), text.size());
    server::Config config;
    string error;
    Check(config.Load((directory + "/server.ini").c_str(), error), "Could not load overlap config");
    pid_t pid = StartServer(config, directory + "/server-overlap");
    try {
        CheckOverlap(port, directory, expected, false);
        CheckOverlap(port, directory, expected, true);
        CheckActiveDisconnect(port, directory);
        CheckGenerationFailures(port);
    } catch (const exception &failure) { Fail(failure.what()); }
    Check(kill(pid, SIGTERM) == 0, "Could not stop overlap server");
    int status = WaitForServer(pid);
    Check(WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "Overlap server did not stop cleanly");
    RemoveDirectory(directory);
    return EXIT_SUCCESS;
}
