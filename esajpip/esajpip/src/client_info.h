#ifndef _CLIENT_INFO_H_
#define _CLIENT_INFO_H_

/**
 * Contains information of a connected client.
 */
class ClientInfo {
private:
    int sock_;            ///< Client socket
    int base_id_;            ///< Base identifier
    int father_sock_;        ///< Father socket

public:
    /**
     * Initializes the object.
     * @param base_id Base identifier.
     * @param sock Client socket.
     * @param father_sock Father socket.
     */
    ClientInfo(int base_id, int sock, int father_sock) {
        sock_ = sock;
        base_id_ = base_id;
        father_sock_ = father_sock;
    }

    /**
     * Returns the base identifier.
     */
    int base_id() const {
        return base_id_;
    }

    /**
     * Returns the client socket.
     */
    int sock() const {
        return sock_;
    }

    /**
     * Returns the father socket.
     */
    int father_sock() const {
        return father_sock_;
    }
};

#endif /* _CLIENT_INFO_H_ */
