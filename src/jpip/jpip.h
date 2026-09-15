#ifndef _JPIP_JPIP_H_
#define _JPIP_JPIP_H_

/**
 * Definitions from the JPIP protocol in Part 9 of the JPEG 2000 standard.
 */
namespace jpip {
    /**
     * Data-bin classes defined by the JPIP protocol.
     */
    enum DataBinClass {
        PRECINCT = 0,
        EXTENDED_PRECINCT = 1,
        TILE_HEADER = 2,
        TILE_DATA = 4,
        EXTENDED_TILE = 5,
        MAIN_HEADER = 6,
        META_DATA = 8
    };

    /**
     * End-of-response reasons defined by the JPIP protocol.
     */
    enum EOR {
        IMAGE_DONE = 1, ///< All available image information was sent.
        WINDOW_DONE = 2, ///< All information relevant to the window was sent.
        WINDOW_CHANGE = 3, ///< A new request superseded this response.
        BYTE_LIMIT_REACHED = 4, ///< The request byte limit was reached.
        QUALITY_LIMIT_REACHED = 5, ///< The request quality limit was reached.
        SESSION_LIMIT_REACHED = 6, ///< The session can accept no further requests.
        RESPONSE_LIMIT_REACHED = 7, ///< The response limit was reached; the session remains usable.
        NON_SPECIFIED = 0xFF
    };
}

#endif /* _JPIP_JPIP_H_ */
