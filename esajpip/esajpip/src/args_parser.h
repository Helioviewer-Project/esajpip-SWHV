#ifndef _ARGS_PARSER_H_
#define _ARGS_PARSER_H_

#include "app_info.h"

/**
 * Class that allows to parse and handle the application
 * command line parameters.
 */
class ArgsParser {
private:
    AppInfo &app_info;    ///< Application run-time information

public:
    /**
     * Initializes the object.
     * @param _app_info Application run-time information.
     */
    explicit ArgsParser(AppInfo &_app_info) : app_info(_app_info) {
    }

    /**
     * Parses and handles the application command line parameters.
     * @param argc Number of parameters.
     * @param argv Command line parameters.
     * @return <code>true</code> if successful.
     */
    bool Parse(int argc, char **argv);
};

#endif /* _ARGS_PARSER_H_ */
