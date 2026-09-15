#ifndef _JPIP_JPIP_H_
#define _JPIP_JPIP_H_

/**
 * Definitions from the JPIP protocol in Part 9 of the JPEG 2000 standard.
 */
namespace jpip {
    /**
     * Data-bin classes defined by the JPIP protocol.
     */
    namespace DataBinClass {
        enum {
            /**
             * Class identifier for precinct data-bins.
             */
                    PRECINCT = 0,

            /**
             * Class identifier for extended precinct data-bins.
             */
                    EXTENDED_PRECINCT = 1,

            /**
             * Class identifier for tile header data-bins.
             */
                    TILE_HEADER = 2,

            /**
             * Class identifier for tile data-bins.
             */
                    TILE_DATA = 4,

            /**
             * Class identifier for extended tile data-bins.
             */
                    EXTENDED_TILE = 5,

            /**
             * Class identifier for main header data-bins.
             */
                    MAIN_HEADER = 6,

            /**
             * Class identifier for meta-data data-bins.
             */
                    META_DATA = 8
        };
    }


    /**
     * End-of-response reasons defined by the JPIP protocol.
     */
    namespace EOR {
        enum {
            /**
             * EOR code sent when the server has transferred all available image
             * information (not just information relevant to the requested view-window)
             * to the client.
             */
                    IMAGE_DONE = 1,

            /**
             * EOR code sent when the server has transferred all available information
             * that is relevant to the requested view-window.
             */
                    WINDOW_DONE = 2,

            /**
             * EOR code sent when the server is terminating its response in order to
             * service a new request.
             */
                    WINDOW_CHANGE = 3,

            /**
             * EOR code sent when the server is terminating its response because the
             * byte limit specified in a byte limit specified in a max length request
             * field has been reached.
             */
                    BYTE_LIMIT_REACHED = 4,

            /**
             * EOR code sent when the server is terminating its response because the
             * quality limit specified in a quality request field has been reached.
             */
                    QUALITY_LIMIT_REACHED = 5,

            /**
             * EOR code sent when the server is terminating its response because some
             * limit on the session resources, e.g. a time limit, has been reached. No
             * further request should be issued using a channel ID assigned in that
             * session.
             */
                    SESSION_LIMIT_REACHED = 6,

            /**
             * EOR code sent when the server is terminating its response because some
             * limit, e.g., a time limit, has been reached. If the request is issued in
             * a session, further requests can still be issued using a channel ID
             * assigned in that session.
             */
                    RESPONSE_LIMIT_REACHED = 7,

            /**
             * EOR code sent when there is not any specific EOR reason.
             */
                    NON_SPECIFIED = 0xFF
        };
    }
}

#endif /* _JPIP_JPIP_H_ */
