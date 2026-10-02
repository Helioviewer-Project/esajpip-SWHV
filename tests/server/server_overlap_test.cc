#include "server_fixture.h"

using namespace server_test;

enum class WaitingRequest { RESPONSE, DISCONNECT, CLOSE };

static void CheckOverlap(uint16_t port, const string &directory,
                         const jpp_test::Expected &expected, WaitingRequest mode) {
    test_case = mode == WaitingRequest::CLOSE ? "overlap/queued-close" :
            mode == WaitingRequest::DISCONNECT ? "overlap/waiting-disconnect" :
                                                "overlap/queued-response";
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
    string waiting_target = mode == WaitingRequest::CLOSE
            ? "/jpip?cclose=" + cid + "&handled"
            : "/jpip?cid=" + cid + "&stream=0&fsiz=260,1&len=61&handled";
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
    if (mode == WaitingRequest::DISCONNECT) {
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
    if (mode == WaitingRequest::CLOSE) {
        Response closed = ReadResponse(waiting);
        Check(closed.headers.find("200 OK") != string::npos &&
                      closed.headers.find("JPIP-handled:") != string::npos &&
                      closed.headers.find("Connection: close") != string::npos &&
                      closed.body.empty(),
              "Queued channel close did not deliver its complete control reply");
        CheckClosed(waiting, 1000, "Queued channel close retained its connection");
        close(waiting);
        SendRequest(active, "/jpip?cid=" + cid);
        Response ended = ReadResponse(active, &active_input);
        Check(ended.headers.find("503 Service Unavailable") != string::npos &&
                      (ended.body == "JPIP channel has ended" ||
                       ended.body == "JPIP channel does not exist"),
              "Queued close left its channel serving requests");
        CheckClosed(active, 1000, "Ended channel response retained its connection");
        close(active);
        return;
    }
    if (mode == WaitingRequest::RESPONSE) {
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

// Create a channel and start a response too large for the undrained socket.
// Returns its connection, with the response headers already in `input`.
static int StartStalledResponse(uint16_t port, string *cid, string *input) {
    int active = Connect(port);
    Check(active >= 0, "Could not connect stalled client");
    int receive_size = 64 * 1024;
    Check(setsockopt(active, SOL_SOCKET, SO_RCVBUF, &receive_size, sizeof receive_size) == 0,
          "Could not constrain the stalled client's receive buffer");
    SendRequest(active, "/overlap.jp2?cnew=http&len=3");
    *cid = ChannelId(ReadResponse(active).headers);
    SendRequest(active, "/jpip?cid=" + *cid + "&stream=0&fsiz=260,1&metareq=[*]!!");
    while (input->find("\r\n\r\n") == string::npos)
        Check(ReceiveMore(active, input), "Stalled response never started");
    Check(input->find("HTTP/1.1 200 OK") == 0, "Stalled response did not send 200");
    return active;
}

// Read what remains of a response that the server abandons, and check that
// it was cut short rather than terminated.
static void CheckAbandoned(int fd, string *input, const char *message) {
    char buffer[64000];
    for (;;) {
        ssize_t length = recv(fd, buffer, sizeof buffer, 0);
        if (length < 0 && errno == EINTR) continue;
        Check(length >= 0 || errno == ECONNRESET, message);
        if (length <= 0) break;
        input->append(buffer, static_cast<size_t>(length));
        Check(input->size() < 4 * 1024 * 1024, message);
    }
    Check(input->size() < 5 || input->compare(input->size() - 5, 5, "0\r\n\r\n") != 0,
          message);
}

static void CheckBadRequestEndsActive(uint16_t port) {
    test_case = "overlap/bad-request-ends-active";
    string cid, input;
    int active = StartStalledResponse(port, &cid, &input);
    int other = Connect(port);
    Check(other >= 0, "Could not connect for the invalid request");
    SendRequest(other, "/jpip?cid=" + cid + "&fsiz=abc");
    Response rejected = ReadResponse(other);
    Check(rejected.headers.find("400 Bad Request") != string::npos &&
                  rejected.body == "Invalid JPIP fsiz parameter",
          "Invalid request for a busy channel did not return 400");
    CheckClosed(other, 1000, "Invalid request retained its connection");
    close(other);
    CheckAbandoned(active, &input,
                   "Invalid request did not abandon the channel's active response");
    close(active);
    int probe = Connect(port);
    Check(probe >= 0, "Could not connect after the invalid request");
    SendRequest(probe, "/jpip?cid=" + cid);
    Check(ReadResponse(probe).headers.find("503 Service Unavailable") != string::npos,
          "Invalid request left its channel serving requests");
    close(probe);
}

// Stop a server that has a response in progress, a waiting request and an
// image open that is still blocked.
static void CheckBusyShutdown(pid_t pid, uint16_t port, const string &directory) {
    test_case = "overlap/busy-shutdown";
    string cid, input;
    int active = StartStalledResponse(port, &cid, &input);
    int waiting = Connect(port);
    Check(waiting >= 0, "Could not connect waiting client before shutdown");
    string waiting_target = "/jpip?cid=" + cid + "&len=65&handled";
    SendRequest(waiting, waiting_target);
    WaitForLog(directory, "Request: " + waiting_target + "\n");
    int opening = Connect(port);
    Check(opening >= 0, "Could not connect opening client before shutdown");
    string opening_target = "/blocked.jp2?cnew=http&len=3";
    SendRequest(opening, opening_target);
    WaitForLog(directory, "Request: " + opening_target + "\n");

    Check(kill(pid, SIGTERM) == 0, "Could not stop the busy server");
    WaitForLog(directory, "Server stopping");
    CheckClosed(waiting, 1000, "Shutdown answered or retained a waiting request");
    CheckClosed(opening, 1000, "Shutdown answered or retained an opening request");
    CheckAbandoned(active, &input, "Shutdown did not abandon the active response");
    close(waiting);
    close(opening);
    close(active);

    // The image open is either executing, and the server cannot exit until
    // it is released, or was still queued, and has been cancelled.
    string fifo = directory + "/blocked.jp2";
    Clock::time_point deadline = Clock::now() + chrono::seconds(5);
    int status = 0;
    pid_t exited;
    while ((exited = waitpid(pid, &status, WNOHANG)) != pid) {
        Check((exited == 0 || errno == EINTR) && Clock::now() < deadline,
              "Busy server did not stop before the deadline");
        int writer = open(fifo.c_str(), O_WRONLY | O_NONBLOCK);
        if (writer >= 0)
            close(writer);
        this_thread::sleep_for(chrono::milliseconds(5));
    }
    for (volatile sig_atomic_t &child : child_pids)
        if (child == pid) child = 0;
    Check(WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "Busy server did not stop cleanly");
}

// A waiting request expires while its channel's response is still in progress.
// Both waits last connections.timeout, and a response's wait restarts whenever
// it writes. One burst of reading after the waiting request has arrived
// therefore makes the waiting request the first to expire, whatever the rate
// at which the sockets move data.
static void CheckWaitingTimeout(uint16_t port, const string &directory,
                                const jpp_test::Expected &expected) {
    test_case = "overlap/waiting-timeout";
    string cid, active_input;
    int active = StartStalledResponse(port, &cid, &active_input);
    int waiting = Connect(port);
    Check(waiting >= 0, "Could not connect expiring client");
    string waiting_target = "/jpip?cid=" + cid + "&len=64&handled";
    SendRequest(waiting, waiting_target);
    WaitForLog(directory, "Request: " + waiting_target + "\n");

    // Halfway to the waiting request's deadline, read more than the socket
    // buffers hold: the server must then have written again. What is left
    // of the response still exceeds those buffers, so it stays in progress.
    this_thread::sleep_for(chrono::seconds(1));
    size_t burst_end = active_input.size() + 4 * 1024 * 1024;
    while (active_input.size() < burst_end)
        Check(ReceiveMore(active, &active_input),
              "Active response ended while a request waited");
    Response expired = ReadResponse(waiting);
    Check(expired.headers.find("503 Service Unavailable") != string::npos &&
                  expired.headers.find("JPIP-handled:") != string::npos &&
                  expired.body == "JPIP channel request timed out",
          "Expired waiting request did not return its timeout response");
    CheckClosed(waiting, 1000, "Expired waiting request retained its connection");
    close(waiting);

    int receive_size = 1024 * 1024;
    Check(setsockopt(active, SOL_SOCKET, SO_RCVBUF, &receive_size, sizeof receive_size) == 0,
          "Could not enlarge the receive buffer for draining");
    Response generated = ReadResponse(active, &active_input);
    jpp_test::Cache cache(expected);
    Check(cache.Read(generated.body) == 2 && cache.Complete(),
          "An expired waiting request changed active response bytes or completion");
    SendRequest(active, "/jpip?cclose=" + cid);
    Check(ReadResponse(active).headers.find("200 OK") != string::npos,
          "Could not close the channel after its waiting request expired");
    CheckClosed(active, 1000, "Closed channel retained its connection");
    close(active);
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
    Check(mkfifo((directory + "/blocked.jp2").c_str(), 0600) == 0,
          "Could not create the blocking image FIFO");

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
        CheckOverlap(port, directory, expected, WaitingRequest::RESPONSE);
        CheckOverlap(port, directory, expected, WaitingRequest::DISCONNECT);
        CheckOverlap(port, directory, expected, WaitingRequest::CLOSE);
        CheckActiveDisconnect(port, directory);
        CheckGenerationFailures(port);
        CheckBadRequestEndsActive(port);
        CheckBusyShutdown(pid, port, directory);
    } catch (const exception &failure) { Fail(failure.what()); }

    // A short timeout, for the waiting request that expires.
    uint16_t timeout_port = ReservePort();
    string timeout_text = "[listen]\nport = " + to_string(timeout_port) + "\naddress = 127.0.0.1\n"
            "[jpip]\nimage_directory = " + directory + "\nchunk_size = 64000\n"
            "[connections]\ninitial_timeout = 5\ntimeout = 2\nlimit = 4\n"
            "[channels]\nlimit = 4\n"
            "[logging]\ndirectory =\nfile_enabled = false\nrequests = true\n";
    WriteFile(directory + "/timeout.ini", timeout_text.data(), timeout_text.size());
    server::Config timeout_config;
    Check(timeout_config.Load((directory + "/timeout.ini").c_str(), error),
          "Could not load overlap timeout config");
    pid = StartServer(timeout_config, directory + "/server-overlap-timeout");
    try {
        CheckWaitingTimeout(timeout_port, directory, expected);
    } catch (const exception &failure) { Fail(failure.what()); }
    Check(kill(pid, SIGTERM) == 0, "Could not stop overlap timeout server");
    int status = WaitForServer(pid);
    Check(WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "Overlap timeout server did not stop cleanly");
    RemoveDirectory(directory);
    return EXIT_SUCCESS;
}
