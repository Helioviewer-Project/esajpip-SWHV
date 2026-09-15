#include <sys/socket.h>
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "trace.h"
#include "app_config.h"
#include "net/socket.h"
#include "server/channel.h"
#include "server/child.h"
#include "server/initial_request.h"

using namespace std;

using net::Socket;

struct ChannelInfo {
    uint64_t id;
    string channel;
    shared_ptr<ConnectionQueue> queue;
};

static const AppConfig *cfg;
static Socket control_socket;
static Socket channel_notification_socket;

static void NotifyParent(uint64_t id);
static bool StartChannel(const pthread_attr_t *pattr, uint64_t id,
                         const string &channel,
                         const shared_ptr<ConnectionQueue> &queue);
static void *ChannelThread(void *arg);

int RunChild(const AppConfig &config, int parent_fd, int control_fd) {
    cfg = &config;
    control_socket = control_fd;

    signal(SIGPIPE, SIG_IGN);

    LOG("Child process created (PID = " << getpid() << ")");

    int notification_fds[2];
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, notification_fds) != 0) {
        ERROR("The channel notification socket can not be created");
        return -1;
    }
    Socket channel_socket(notification_fds[0]);
    channel_notification_socket = notification_fds[1];

    pthread_attr_t pattr;
    int pthread_error = pthread_attr_init(&pattr);
    if (pthread_error != 0)
        return CERR("The channel thread attributes can not be initialized: "
                    << strerror(pthread_error));
    pthread_error = pthread_attr_setdetachstate(&pattr, PTHREAD_CREATE_DETACHED);
    if (pthread_error != 0) {
        pthread_attr_destroy(&pattr);
        return CERR("Detached channel threads can not be configured: "
                    << strerror(pthread_error));
    }

    vector<ChannelInfo> channels;
    for (;;) {
        pollfd fds[] = {
            {parent_fd, POLLIN, 0},
            {control_socket, POLLIN, 0},
            {channel_socket, POLLIN, 0}
        };
        int result;
        do {
            result = poll(fds, 3, -1);
        } while (result < 0 && errno == EINTR);
        if (result < 0) {
            ERROR("The child poll failed: " << strerror(errno));
            pthread_attr_destroy(&pattr);
            return -1;
        }

        // The parent never writes to this pipe. Any event means that its only
        // write end was closed, so the serving process must exit as well.
        if (fds[0].revents) {
            pthread_attr_destroy(&pattr);
            return 0;
        }

        if (fds[2].revents & POLLIN) {
            uint64_t id;
            if (channel_socket.Receive(&id, sizeof id) == sizeof id) {
                vector<ChannelInfo>::iterator channel =
                        find_if(channels.begin(), channels.end(), [id](const ChannelInfo &entry) {
                            return entry.id == id;
                        });
                if (channel != channels.end()) {
                    LOG("The channel " << channel->channel << " has ended");
                    channels.erase(channel);
                }
            } else {
                ERROR("Could not receive the completed channel identifier");
            }
        }

        if (!(fds[1].revents & POLLIN))
            continue;

        int fd;
        uint64_t connection_id;
        if (!control_socket.ReceiveDescriptor(&fd, &connection_id)) {
            ERROR("The JPIP socket can not be received by the child process: "
                  << strerror(errno));
            continue;
        }

        InitialRequest request = InspectInitialRequest(fd);
        if (request.state != REQUEST_ACCEPTED) {
            LOG("The identified connection [" << connection_id
                                                << "] has no usable initial request");
        } else if (request.new_channel) {
            channels.erase(remove_if(channels.begin(), channels.end(), [](ChannelInfo &entry) {
                return entry.queue->IsClosed();
            }), channels.end());
            if (channels.size() >= static_cast<size_t>(cfg->max_connections())) {
                LOG("A new channel was refused because the limit has been reached");
            } else {
                string channel = to_string(connection_id);
                shared_ptr<ConnectionQueue> queue = make_shared<ConnectionQueue>();
                if (!queue->IsValid()) {
                    ERROR("The connection queue can not be created");
                } else if (!queue->Push({connection_id, fd})) {
                    ERROR("The initial channel connection can not be queued");
                } else if (StartChannel(&pattr, connection_id, channel, queue)) {
                    channels.push_back({connection_id, channel, queue});
                    LOG("Creating channel " << channel << " for connection ["
                                            << connection_id << "]");
                    continue;
                }
            }
        } else {
            vector<ChannelInfo>::iterator channel =
                    find_if(channels.begin(), channels.end(), [&request](const ChannelInfo &entry) {
                        return entry.channel == request.channel;
                    });
            if (channel == channels.end()) {
                LOG("The connection [" << connection_id << "] references unknown channel "
                                       << request.channel);
            } else if (channel->queue->Push({connection_id, fd})) {
                continue;
            } else if (channel->queue->IsClosed()) {
                channels.erase(channel);
            }
        }

        shutdown(fd, SHUT_RDWR);
        close(fd);
        NotifyParent(connection_id);
    }
}

static void NotifyParent(uint64_t id) {
    if (control_socket.Send(&id, sizeof id) == sizeof id)
        return;
    ERROR("The completed connection [" << id << "] could not notify the parent");
}

static bool StartChannel(const pthread_attr_t *pattr, uint64_t id,
                         const string &channel,
                         const shared_ptr<ConnectionQueue> &queue) {
    ChannelInfo *channel_info = new ChannelInfo{id, channel, queue};
    pthread_t service_tid;
    int pthread_error = pthread_create(&service_tid, pattr, ChannelThread, channel_info);
    if (pthread_error == 0)
        return true;

    ERROR("A thread for channel " << channel << " can not be created: "
          << strerror(pthread_error));
    delete channel_info;
    return false;
}

static void *ChannelThread(void *arg) {
    ChannelInfo *channel_info = static_cast<ChannelInfo *>(arg);
    uint64_t id = channel_info->id;
    string channel = channel_info->channel;
    shared_ptr<ConnectionQueue> queue = channel_info->queue;
    delete channel_info;

    RunChannel(*cfg, channel, queue, NotifyParent);

    if (channel_notification_socket.Send(&id, sizeof id) != sizeof id)
        ERROR("The completed channel " << channel << " could not notify the child process");
    return NULL;
}
