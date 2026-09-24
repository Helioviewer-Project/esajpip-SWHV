#include "server/server.h"

#include <array>
#include <csignal>
#include <cstdio>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>
#include <unistd.h>

#include <glib.h>
#include <uv.h>

#include "config.h"
#include "jpip/request.h"
#include "net/address.h"
#include "server/channel_work.h"
#include "server/connection.h"
#include "server/request_head.h"
#include "trace.h"

using namespace std;

namespace {

const unsigned int RESPONSE_BUFFERS = 4;
const char JPIP_HANDLED_HEADER[] =
        "JPIP-handled: tid,cid,cnew=http,stream,len,handled\r\n";
const char COMMON_HEADERS[] =
        "Access-Control-Allow-Origin: *\r\n"
        "Cache-Control: no-cache\r\n";

int GenerateChannelId(string *id) {
    uint8_t random[16];
    int result = uv_random(NULL, NULL, random, sizeof random, 0, NULL);
    if (result != 0)
        return result;

    static const char hex[] = "0123456789abcdef";
    id->resize(sizeof random * 2);
    for (size_t i = 0; i < sizeof random; ++i) {
        (*id)[i * 2] = hex[random[i] >> 4];
        (*id)[i * 2 + 1] = hex[random[i] & 15];
    }
    return 0;
}

string EscapeForLog(const string &text) {
    char *escaped = g_strescape(text.c_str(), NULL);
    string result(escaped);
    g_free(escaped);
    return result;
}

class Server;
struct Channel;
struct Exchange;
void WorkDone(server::ChannelWork &, void *owner);

struct Client {
    Client(Server *_server, uint64_t _id, const Config &cfg);

    Server *server;
    uint64_t id;
    unique_ptr<server::Connection> connection;
    server::Connection::Write write;
    Exchange *exchange = NULL;
    bool counted = false;
};

struct Exchange {
    Exchange(Channel *_channel, Client *_client, jpip::Request _request,
             server::RequestHead _head)
        : channel(_channel), client(_client), request(std::move(_request)),
          head(std::move(_head)) {}

    ~Exchange() { Detach(); }

    Client *Detach() {
        Client *attached = client;
        if (attached && attached->exchange == this)
            attached->exchange = NULL;
        client = NULL;
        return attached;
    }

    Channel *channel;
    Client *client;
    jpip::Request request;
    server::RequestHead head;
    int work_buffer = -1;
    unsigned int writes = 0;
    bool headers_sent = false;
    bool generation_complete = false;
    bool response_complete = false;
    bool cleanup_complete = false;
};

struct Buffer {
    Buffer() = default;

    vector<char> data;
    server::Connection::Write write;
    bool busy = false;

    Buffer(const Buffer &) = delete;
    Buffer &operator=(const Buffer &) = delete;
};

struct Channel {
    enum State { OPEN_QUEUED, OPENING, OPEN, ENDING };

    Channel(Server *_server, string _id, uint64_t _number,
            uv_loop_t *loop, const Config &cfg);

    Server *server;
    string id;
    uint64_t number;
    server::ChannelWork work;
    uv_timer_t timer;
    array<Buffer, RESPONSE_BUFFERS> buffers;
    unique_ptr<Exchange> waiting;
    unique_ptr<Exchange> exchange;
    State state = OPEN_QUEUED;
    bool timer_initialized = false;
    bool timer_closed = false;
};

const string &Target(const Channel &channel) {
    return channel.exchange->request.has.target
            ? channel.exchange->request.target : channel.exchange->request.object;
}

bool UseGzip(const Channel &channel) {
    return channel.exchange->request.has.metareq &&
            channel.exchange->head.accepts_gzip;
}

string OptionalHeaders(const jpip::Request &request, bool cnew = false) {
    string result;
    if (request.has.tid && !cnew)
        result += "JPIP-tid: 0\r\n";
    if (request.has.handled)
        result += JPIP_HANDLED_HEADER;
    if (request.has.tid || request.has.handled || cnew) {
        result += "Access-Control-Expose-Headers: ";
        bool comma = false;
        if (cnew) {
            result += "JPIP-cnew,JPIP-tid";
            comma = true;
        } else if (request.has.tid) {
            result += "JPIP-tid";
            comma = true;
        }
        if (request.has.handled) {
            if (comma)
                result += ',';
            result += "JPIP-handled";
        }
        result += "\r\n";
    }
    return result;
}

string ErrorResponse(int code, const char *reason, const string &message,
                     const jpip::Request *request = NULL) {
    string result = "HTTP/1.1 " + to_string(code) + " " + reason + "\r\n";
    if (request)
        result += OptionalHeaders(*request);
    result += COMMON_HEADERS;
    result += "Content-Type: text/plain\r\nContent-Length: ";
    result += to_string(message.size());
    result += "\r\nConnection: close\r\n\r\n";
    result += message;
    return result;
}

string SuccessHeaders(const Channel &channel, bool gzip) {
    string result = "HTTP/1.1 200 OK\r\n";
    if (channel.exchange->request.has.cnew) {
        result += "JPIP-cnew: cid=" + channel.id +
                ",path=jpip,transport=http\r\nJPIP-tid: 0\r\n";
        result += OptionalHeaders(channel.exchange->request, true);
    } else {
        result += OptionalHeaders(channel.exchange->request);
    }
    result += COMMON_HEADERS;
    result += "Transfer-Encoding: chunked\r\nContent-Type: image/jpp-stream\r\n";
    if (gzip)
        result += "Content-Encoding: gzip\r\n";
    if (channel.exchange->head.close)
        result += "Connection: close\r\n";
    result += "\r\n";
    return result;
}

string ChunkHeader(size_t size) {
    char text[2 * sizeof(size_t) + 3];
    int length = snprintf(text, sizeof text, "%zx\r\n", size);
    return string(text, static_cast<size_t>(length));
}

class Server {
private:
    const Config &cfg;
    const net::InetAddress &listen_address;
    uv_loop_t loop;
    uv_tcp_t listener;
    uv_signal_t stop_signals[2];
    uv_async_t reaper;
    map<uint64_t, unique_ptr<Client> > clients;
    map<string, unique_ptr<Channel> > channels;
    deque<string> open_queue;
    vector<uint64_t> dead_clients;
    vector<string> dead_channels;
    uint64_t next_client = 1;
    uint64_t next_channel = 1;
    const unsigned int max_concurrent_opens;
    unsigned int active_opens = 0;
    unsigned int num_connections = 0;
    int open_handles = 0;
    bool stopping = false;

    // The loop cannot safely recover partially modified global state after an
    // allocation failure. No exception may cross libuv's C callback boundary.
    static void Listen(uv_stream_t *handle, int status) noexcept {
        static_cast<Server *>(handle->data)->Accept(status);
    }
    static void Signal(uv_signal_t *handle, int) noexcept {
        static_cast<Server *>(handle->data)->Stop();
    }
    static void HandleClosed(uv_handle_t *handle) noexcept {
        Server *server = static_cast<Server *>(handle->data);
        server->open_handles--;
        server->FinishStop();
    }

    static void CloseHandle(uv_handle_t *handle, void *) noexcept {
        if (!uv_is_closing(handle))
            uv_close(handle, NULL);
    }

    int InitializationFailed(int result) {
        int close_result;
        do {
            uv_walk(&loop, CloseHandle, NULL);
            uv_run(&loop, UV_RUN_NOWAIT);
            close_result = uv_loop_close(&loop);
        } while (close_result == UV_EBUSY);
        return result;
    }
    static void Reap(uv_async_t *handle) noexcept {
        static_cast<Server *>(handle->data)->ReapObjects();
    }
    static void ChannelExpired(uv_timer_t *timer) noexcept {
        Channel *channel = static_cast<Channel *>(timer->data);
        LOG("The channel " << channel->number << " timed out");
        channel->server->End(*channel);
    }
    static void ChannelTimerClosed(uv_handle_t *handle) noexcept {
        Channel *channel = static_cast<Channel *>(handle->data);
        channel->timer_closed = true;
        channel->server->FinishChannel(*channel);
    }

    void Accept(int status) {
        if (status != 0 || stopping)
            return;
        if (num_connections >= static_cast<unsigned int>(cfg.max_connections())) {
            uv_tcp_t *rejected = new (nothrow) uv_tcp_t;
            if (!rejected) {
                ERROR("A rejected connection can not be allocated");
                Stop();
                return;
            }
            int result = uv_tcp_init(&loop, rejected);
            if (result == 0) {
                rejected->data = rejected;
                (void) uv_accept(reinterpret_cast<uv_stream_t *>(&listener),
                                 reinterpret_cast<uv_stream_t *>(rejected));
                uv_close(reinterpret_cast<uv_handle_t *>(rejected),
                         [](uv_handle_t *handle) {
                             delete static_cast<uv_tcp_t *>(handle->data);
                         });
            } else {
                delete rejected;
                ERROR("A rejected connection can not be initialized: "
                      << uv_strerror(result));
                Stop();
            }
            return;
        }

        unique_ptr<Client> client(new Client(this, next_client++, cfg));
        uint64_t id = client->id;
        clients[id] = std::move(client);
        if (!clients[id]->connection->Initialize(&loop) ||
            !clients[id]->connection->Accept(
                    reinterpret_cast<uv_stream_t *>(&listener))) {
            clients[id]->connection->Abort();
            return;
        }
        LOG("Accepted connection [" << id << "]");
        clients[id]->counted = true;
        num_connections++;
        if (!clients[id]->connection->Start())
            clients[id]->connection->Abort();
    }

    void Route(Client &client, server::RequestHead &&head) {
        jpip::Request request;
        string error;
        if (!request.ParseTarget(head.target, &error)) {
            LOG("Bad request: " << EscapeForLog(head.target));
            SendError(client, 400, "Bad Request", error, &request);
            if (!request.has.cnew)
                EndRequestedChannel(request);
            return;
        }
        if (!request.has.cnew && !request.has.cid && !request.has.cclose) {
            client.connection->Abort();
            return;
        }
        if (head.unsupported_body) {
            SendError(client, 400, "Bad Request",
                      "HTTP request bodies are not supported", &request);
            EndRequestedChannel(request);
            return;
        }
        if (cfg.log_requests())
            LOG("Request: " << EscapeForLog(head.target));

        if (request.has.cnew) {
            NewChannel(client, std::move(request), std::move(head));
            return;
        }
        auto found = channels.find(request.channel);
        if (found == channels.end() || found->second->state == Channel::ENDING) {
            SendError(client, 503, "Service Unavailable",
                      found == channels.end() ? "JPIP channel does not exist"
                                              : "JPIP channel has ended",
                      &request);
            return;
        }
        Channel &channel = *found->second;
        bool start = channel.exchange->client == NULL &&
                channel.state == Channel::OPEN;
        if (!start && channel.waiting) {
            SendError(client, 503, "Service Unavailable",
                      "JPIP channel is busy", &request);
            return;
        }
        unique_ptr<Exchange> exchange = CreateExchange(
                channel, client, std::move(request), std::move(head));
        if (!exchange)
            return;
        if (start) {
            StartRequest(channel, std::move(exchange));
        } else {
            channel.waiting = std::move(exchange);
        }
    }

    void NewChannel(Client &client, jpip::Request request,
                    server::RequestHead head) {
        if (!request.accepts_http) {
            SendError(client, 501, "Not Implemented",
                      "The requested JPIP channel transport is not supported",
                      &request);
            return;
        }
        if (channels.size() >= static_cast<size_t>(cfg.max_channels())) {
            SendError(client, 503, "Service Unavailable",
                      "JPIP channel limit has been reached", &request);
            return;
        }
        string id;
        int result = GenerateChannelId(&id);
        if (result != 0) {
            ERROR("A secure JPIP channel ID can not be generated: "
                  << uv_strerror(result));
            SendError(client, 500, "Internal Server Error",
                      "Could not create JPIP channel", &request);
            return;
        }
        if (channels.count(id) != 0) {
            ERROR("A generated JPIP channel ID collides with an active channel");
            SendError(client, 500, "Internal Server Error",
                      "Could not create JPIP channel", &request);
            return;
        }
        unique_ptr<Channel> created(new Channel(this, id, next_channel++,
                                                 &loop, cfg));
        if (!created->work.IsInitialized() || !created->timer_initialized) {
            SendError(client, 500, "Internal Server Error",
                      "The JPIP channel could not be initialized", &request);
            return;
        }
        Channel *channel = created.get();
        unique_ptr<Exchange> exchange = CreateExchange(
                *channel, client, std::move(request), std::move(head));
        if (!exchange)
            return;
        channels[id] = std::move(created);
        StartRequest(*channel, std::move(exchange));
        channel->state = Channel::OPEN_QUEUED;
        open_queue.push_back(channel->id);
        StartOpens();
    }

    unique_ptr<Exchange> CreateExchange(Channel &channel, Client &client,
                                        jpip::Request request,
                                        server::RequestHead head) {
        if (client.exchange) {
            ERROR("An HTTP connection is already attached to a JPIP exchange");
            client.connection->Abort();
            return unique_ptr<Exchange>();
        }
        unique_ptr<Exchange> exchange(new Exchange(
                &channel, &client, std::move(request), std::move(head)));
        client.exchange = exchange.get();
        client.connection->BlockRequests();
        return exchange;
    }

    void StartRequest(Channel &channel, unique_ptr<Exchange> exchange) {
        if (channel.timer_initialized)
            uv_timer_stop(&channel.timer);
        channel.exchange = std::move(exchange);
        Client &client = *channel.exchange->client;

        if (channel.exchange->request.has.cclose) {
            channel.state = Channel::OPEN;
            channel.exchange->cleanup_complete = true;
            client.connection->StartResponse();
            string response = "HTTP/1.1 200 OK\r\n" +
                    OptionalHeaders(channel.exchange->request) + COMMON_HEADERS +
                    "Content-Length: 0\r\nConnection: close\r\n\r\n";
            if (!client.connection->Send(
                        &client.write, std::move(response),
                        [this, &channel](bool ok) {
                            channel.exchange->response_complete = true;
                            if (channel.exchange->client) {
                                if (ok)
                                    channel.exchange->client->connection->CloseGracefully();
                                else
                                    channel.exchange->client->connection->Abort();
                            }
                            End(channel);
                        })) {
                channel.exchange->response_complete = true;
                client.connection->Abort();
                End(channel);
            }
        } else if (!channel.exchange->request.has.cnew) {
            Begin(channel);
        }
    }

    void StartOpens() {
        while (!stopping && active_opens < max_concurrent_opens &&
               !open_queue.empty()) {
            string id = std::move(open_queue.front());
            open_queue.pop_front();
            auto found = channels.find(id);
            if (found == channels.end())
                continue;
            Channel *channel = found->second.get();
            if (channel->state != Channel::OPEN_QUEUED)
                continue;
            channel->state = Channel::OPENING;
            active_opens++;
            if (!channel->work.Open(Target(*channel))) {
                active_opens--;
                Fail(*channel, 500, "Internal Server Error",
                     "The JPIP channel could not be scheduled");
            }
        }
    }

    int FreeBuffer(Channel &channel) {
        for (size_t i = 0; i < channel.buffers.size(); ++i)
            if (!channel.buffers[i].busy)
                return static_cast<int>(i);
        return -1;
    }

    void Begin(Channel &channel) {
        int index = FreeBuffer(channel);
        if (index < 0)
            return;
        Buffer &buffer = channel.buffers[index];
        if (buffer.data.empty())
            buffer.data.resize(static_cast<size_t>(cfg.max_chunk_size()));
        channel.state = Channel::OPEN;
        channel.exchange->work_buffer = index;
        buffer.busy = true;
        if (!channel.work.Begin(channel.exchange->request, UseGzip(channel),
                                buffer.data.data(), buffer.data.size()))
            Fail(channel, 500, "Internal Server Error",
                 "The JPIP response could not be scheduled");
    }

    void Generate(Channel &channel) {
        if (channel.work.IsActive() ||
            channel.exchange->generation_complete ||
            channel.state == Channel::ENDING)
            return;
        int index = FreeBuffer(channel);
        if (index < 0)
            return;
        Buffer &buffer = channel.buffers[index];
        if (buffer.data.empty())
            buffer.data.resize(static_cast<size_t>(cfg.max_chunk_size()));
        channel.exchange->work_buffer = index;
        buffer.busy = true;
        if (!channel.work.Generate(buffer.data.data(), buffer.data.size()))
            Fail(channel, 500, "Internal Server Error",
                 "The JPIP response could not be scheduled");
    }

    void SendChunk(Channel &channel, int index, int length, bool final) {
        Buffer &buffer = channel.buffers[index];
        string first;
        if (!channel.exchange->headers_sent) {
            channel.exchange->headers_sent = true;
            channel.exchange->client->connection->StartResponse();
            first = SuccessHeaders(channel, UseGzip(channel));
        }
        string last;
        if (length > 0) {
            first += ChunkHeader(static_cast<size_t>(length));
            last = final ? "\r\n0\r\n\r\n" : "\r\n";
        } else if (final) {
            first += "0\r\n\r\n";
        }
        channel.exchange->writes++;
        if (!channel.exchange->client->connection->Send(
                    &buffer.write, std::move(first), buffer.data.data(),
                    static_cast<size_t>(length), std::move(last),
                    [this, &channel, index](bool ok) {
                        Buffer &completed = channel.buffers[index];
                        completed.busy = false;
                        if (channel.exchange->writes > 0)
                            channel.exchange->writes--;
                        if (!ok) {
                            End(channel);
                            return;
                        }
                        if (channel.exchange->generation_complete &&
                            channel.exchange->writes == 0)
                            channel.exchange->response_complete = true;
                        if (!channel.exchange->generation_complete)
                            Generate(channel);
                        FinishChannel(channel);
                    })) {
            channel.exchange->writes--;
            buffer.busy = false;
            End(channel);
        }
    }

    void StartCleanup(Channel &channel) {
        if (channel.exchange->cleanup_complete || channel.work.IsActive())
            return;
        if (!channel.work.Cleanup()) {
            channel.exchange->cleanup_complete = true;
            channel.state = Channel::ENDING;
        }
    }

    void CompleteWork(Channel &channel) {
        const server::ChannelWork::Result &result = channel.work.GetResult();
        if (result.kind == server::ChannelWork::Kind::OPEN) {
            if (active_opens > 0)
                active_opens--;
            StartOpens();
            if (channel.state == Channel::ENDING)
                StartCleanup(channel);
            else if (result.open != jpeg2000::FileManager::OpenResult::OPENED)
                OpenFailed(channel, result.open);
            else {
                LOG("The channel " << channel.number
                                    << " has been opened for the image '"
                                    << EscapeForLog(Target(channel)) << "'");
                Begin(channel);
            }
            return;
        }
        if (result.kind == server::ChannelWork::Kind::CLEANUP) {
            channel.exchange->cleanup_complete = true;
            FinishChannel(channel);
            return;
        }
        int index = channel.exchange->work_buffer;
        channel.exchange->work_buffer = -1;
        if (index < 0) {
            Fail(channel, 500, "Internal Server Error",
                 "The JPIP response lost its output buffer");
            return;
        }
        Buffer &buffer = channel.buffers[index];
        if (channel.state == Channel::ENDING) {
            buffer.busy = false;
            StartCleanup(channel);
            return;
        }
        if (!result.error.empty()) {
            buffer.busy = false;
            int code = result.request_rejected ? 400 : 500;
            Fail(channel, code,
                 code == 400 ? "Bad Request" : "Internal Server Error",
                 result.error);
            return;
        }
        bool final = result.generation ==
                server::ChannelEngine::GenerateResult::COMPLETE;
        channel.exchange->generation_complete = final;
        SendChunk(channel, index, result.length, final);
        if (!final)
            Generate(channel);
        else
            StartCleanup(channel);
    }

    void OpenFailed(Channel &channel, jpeg2000::FileManager::OpenResult result) {
        switch (result) {
            case jpeg2000::FileManager::OpenResult::OPENED:
                Fail(channel, 500, "Internal Server Error",
                     "The image-open operation returned an inconsistent result");
                return;
            case jpeg2000::FileManager::OpenResult::NOT_FOUND:
                Fail(channel, 404, "Not Found",
                     "The requested image was not found");
                return;
            case jpeg2000::FileManager::OpenResult::INVALID_PATH:
                Fail(channel, 404, "Not Found",
                     "The requested image path is invalid");
                return;
            case jpeg2000::FileManager::OpenResult::UNSUPPORTED:
                Fail(channel, 404, "Not Found",
                     "The requested image type is not supported");
                return;
            case jpeg2000::FileManager::OpenResult::UNREADABLE:
                Fail(channel, 500, "Internal Server Error",
                     "The requested image could not be read");
                return;
            case jpeg2000::FileManager::OpenResult::INVALID:
                Fail(channel, 404, "Not Found",
                     "The requested image is not a valid supported JPEG 2000 source");
                return;
        }
    }

    void BeginTermination(Channel &channel) {
        channel.state = Channel::ENDING;
        CloseChannelTimer(channel);
        channel.work.CancelQueued();
        RejectWaiting(channel, "JPIP channel has ended");
    }

    void Fail(Channel &channel, int code, const char *reason,
              const string &message) {
        Client *client = channel.exchange->client;
        BeginTermination(channel);
        if (client && !channel.exchange->headers_sent) {
            client->connection->StartResponse();
            if (!client->connection->Send(
                        &client->write,
                        ErrorResponse(code, reason, message,
                                      &channel.exchange->request),
                        [this, &channel](bool) {
                            channel.exchange->response_complete = true;
                            if (channel.exchange->client)
                                channel.exchange->client->connection->CloseGracefully();
                            StartCleanup(channel);
                            FinishChannel(channel);
                        })) {
                channel.exchange->response_complete = true;
                client->connection->Abort();
            }
        } else {
            channel.exchange->response_complete = true;
            if (client)
                client->connection->Abort();
        }
        StartCleanup(channel);
        FinishChannel(channel);
    }

    void End(Channel &channel) {
        if (channel.state == Channel::ENDING) {
            CloseChannelTimer(channel);
            FinishChannel(channel);
            return;
        }
        BeginTermination(channel);
        if (channel.exchange->client &&
            !channel.exchange->client->connection->IsClosing())
            channel.exchange->client->connection->Abort();
        channel.exchange->response_complete = true;
        StartCleanup(channel);
        FinishChannel(channel);
    }

    void CloseChannelTimer(Channel &channel) {
        if (channel.timer_initialized && !channel.timer_closed &&
            !uv_is_closing(reinterpret_cast<uv_handle_t *>(&channel.timer))) {
            uv_timer_stop(&channel.timer);
            uv_close(reinterpret_cast<uv_handle_t *>(&channel.timer),
                     ChannelTimerClosed);
        }
    }

    void RejectWaiting(Channel &channel, const char *message) {
        if (!channel.waiting)
            return;
        unique_ptr<Exchange> waiting = std::move(channel.waiting);
        Client *client = waiting->Detach();
        if (!client) {
            ERROR("Waiting JPIP exchange has no HTTP connection");
            return;
        }
        SendError(*client, 503, "Service Unavailable", message,
                  &waiting->request);
    }

    void FinishChannel(Channel &channel) {
        if (!channel.exchange->cleanup_complete ||
            !channel.exchange->response_complete ||
            channel.exchange->writes != 0)
            return;
        Client *client = channel.exchange->Detach();
        bool close_connection = channel.exchange->head.close;
        if (channel.state == Channel::ENDING) {
            if (!channel.timer_closed)
                return;
            dead_channels.push_back(channel.id);
            uv_async_send(&reaper);
        } else {
            channel.state = Channel::OPEN;
            if (channel.waiting) {
                unique_ptr<Exchange> waiting = std::move(channel.waiting);
                StartRequest(channel, std::move(waiting));
            } else if (channel.timer_initialized) {
                uv_timer_start(&channel.timer, ChannelExpired,
                               static_cast<uint64_t>(cfg.connection_timeout()) * 1000,
                               0);
            }
        }
        if (client && !client->connection->IsClosing()) {
            if (close_connection) {
                client->connection->CloseGracefully();
            } else {
                client->connection->FinishResponse();
            }
        }
    }

    void SendError(Client &client, int code, const char *reason,
                   const string &message, const jpip::Request *request = NULL) {
        client.connection->StartResponse();
        if (!client.connection->Send(
                    &client.write, ErrorResponse(code, reason, message, request),
                    [&client](bool) { client.connection->CloseGracefully(); }))
            client.connection->Abort();
    }

    void EndRequestedChannel(const jpip::Request &request) {
        if (!request.has.cid && !request.has.cclose)
            return;
        auto found = channels.find(request.channel);
        if (found != channels.end())
            End(*found->second);
    }

    void ReadFailed(Client &client, server::Connection::ReadFailure failure) {
        if (!client.connection->HasJPIPRoute()) {
            ClientDisconnected(client);
            client.connection->Abort();
            return;
        }
        jpip::Request request;
        string error;
        request.ParseTarget(client.connection->GetRequestTarget(), &error);
        if (failure == server::Connection::ReadFailure::TOO_LARGE)
            SendError(client, 431, "Request Header Fields Too Large",
                      "HTTP request head is too large", &request);
        else if (failure == server::Connection::ReadFailure::MALFORMED)
            SendError(client, 400, "Bad Request", "Invalid HTTP request head",
                      &request);
        else {
            ClientDisconnected(client);
            client.connection->Abort();
            return;
        }
        EndRequestedChannel(request);
    }

    void Deadline(Client &client) {
        Exchange *exchange = client.exchange;
        Channel *channel = exchange ? exchange->channel : NULL;
        if (exchange && channel->waiting.get() == exchange) {
            unique_ptr<Exchange> waiting = std::move(channel->waiting);
            waiting->Detach();
            SendError(client, 503, "Service Unavailable",
                      "JPIP channel request timed out",
                      &waiting->request);
        } else if (exchange &&
                   (channel->state == Channel::OPEN_QUEUED ||
                    channel->state == Channel::OPENING)) {
            Fail(*channel, 503, "Service Unavailable",
                 "JPIP channel creation timed out");
        } else {
            ClientDisconnected(client);
            client.connection->Abort();
        }
    }

    void ClientDisconnected(Client &client) {
        Exchange *exchange = client.exchange;
        if (!exchange)
            return;
        Channel *channel = exchange->channel;
        if (channel->waiting.get() == exchange) {
            channel->waiting.reset();
        } else {
            exchange->Detach();
            End(*channel);
        }
    }

    void Closed(Client &client) {
        ClientDisconnected(client);
        if (client.counted) {
            client.counted = false;
            num_connections--;
        }
        dead_clients.push_back(client.id);
        uv_async_send(&reaper);
    }

    void ReapObjects() {
        for (uint64_t id : dead_clients)
            clients.erase(id);
        dead_clients.clear();
        for (const string &id : dead_channels)
            channels.erase(id);
        dead_channels.clear();
        FinishStop();
    }

    void Stop() {
        if (stopping)
            return;
        stopping = true;
        LOG("Server stopping");
        uv_close(reinterpret_cast<uv_handle_t *>(&listener), HandleClosed);
        for (uv_signal_t &signal : stop_signals) {
            uv_signal_stop(&signal);
            uv_close(reinterpret_cast<uv_handle_t *>(&signal), HandleClosed);
        }
        for (auto &entry : clients)
            entry.second->connection->Abort();
        for (auto &entry : channels)
            End(*entry.second);
        open_queue.clear();
        FinishStop();
    }

    void FinishStop() {
        if (!stopping || !clients.empty() || !channels.empty() ||
            open_handles != 1)
            return;
        uv_close(reinterpret_cast<uv_handle_t *>(&reaper), HandleClosed);
    }

public:
    Server(const Config &_cfg, const net::InetAddress &_listen_address,
           unsigned int worker_threads)
        : cfg(_cfg), listen_address(_listen_address),
          max_concurrent_opens(worker_threads / 2) {}

    int Initialize() {
        int result = uv_loop_init(&loop);
        if (result != 0)
            return result;
        result = uv_tcp_init(&loop, &listener);
        if (result != 0)
            return InitializationFailed(result);
        listener.data = this;
        open_handles++;
        result = uv_tcp_bind(&listener, listen_address.GetSockAddr(), 0);
        if (result != 0)
            return InitializationFailed(result);
        for (uv_signal_t &signal : stop_signals) {
            result = uv_signal_init(&loop, &signal);
            if (result != 0)
                return InitializationFailed(result);
            signal.data = this;
            open_handles++;
        }
        result = uv_async_init(&loop, &reaper, Reap);
        if (result != 0)
            return InitializationFailed(result);
        reaper.data = this;
        open_handles++;
        const int signal_numbers[] = {SIGINT, SIGTERM};
        for (size_t i = 0; i < 2; ++i) {
            result = uv_signal_start(&stop_signals[i], Signal,
                                     signal_numbers[i]);
            if (result != 0)
                return InitializationFailed(result);
        }
        result = uv_listen(reinterpret_cast<uv_stream_t *>(&listener),
                           SOMAXCONN, Listen);
        return result == 0 ? 0 : InitializationFailed(result);
    }

    int Run() {
        int run_result = uv_run(&loop, UV_RUN_DEFAULT);
        int close_result = uv_loop_close(&loop);
        if (run_result != 0)
            ERROR("The serving event loop stopped with active work");
        if (close_result != 0)
            ERROR("The serving event loop could not be closed: "
                  << uv_strerror(close_result));
        return run_result == 0 && close_result == 0 ? 0 : -1;
    }

    void OnRequest(Client &client, server::RequestHead &&head) {
        Route(client, std::move(head));
    }
    void OnReadFailure(Client &client, server::Connection::ReadFailure failure) {
        ReadFailed(client, failure);
    }
    void OnDeadline(Client &client) { Deadline(client); }
    void OnClosed(Client &client) { Closed(client); }

    friend void WorkDone(server::ChannelWork &, void *owner);
};

Client::Client(Server *_server, uint64_t _id, const Config &cfg)
    : server(_server), id(_id) {
    connection.reset(new server::Connection(
            cfg.initial_timeout(), cfg.connection_timeout(),
            [this](server::Connection &, server::RequestHead &&head) {
                server->OnRequest(*this, std::move(head));
            },
            [this](server::Connection &, server::Connection::ReadFailure failure) {
                server->OnReadFailure(*this, failure);
            },
            [this](server::Connection &) {
                server->OnDeadline(*this);
            },
            [this](server::Connection &) { server->OnClosed(*this); }));
}

Channel::Channel(Server *_server, string _id, uint64_t _number,
                 uv_loop_t *loop, const Config &cfg)
    : server(_server), id(std::move(_id)), number(_number),
      work(loop, cfg.max_chunk_size(), cfg.image_directory(), WorkDone, this) {
    timer_initialized = work.IsInitialized() && uv_timer_init(loop, &timer) == 0;
    if (timer_initialized)
        timer.data = this;
    timer_closed = !timer_initialized;
}

void WorkDone(server::ChannelWork &, void *owner) {
    Channel *channel = static_cast<Channel *>(owner);
    channel->server->CompleteWork(*channel);
}

} // namespace

int RunServer(const Config &cfg, const net::InetAddress &listen_address,
              const string &log_name, const string &description,
              unsigned int worker_threads) {
    if (worker_threads < 2)
        return CERR("The worker-pool size must be at least 2");
    if (!trace::Initialize(log_name))
        return -1;
    LOG(description << " started (PID = " << getpid() << ")");

    Server server(cfg, listen_address, worker_threads);
    int initialize_result = server.Initialize();
    if (initialize_result != 0) {
        ERROR("The server can not be initialized: "
              << uv_strerror(initialize_result));
        uv_library_shutdown();
        trace::Drain();
        return -1;
    }
    int result = server.Run();
    if (result == 0)
        uv_library_shutdown();
    trace::Drain();
    return result;
}
