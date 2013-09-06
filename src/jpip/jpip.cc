#include "jpip.h"


namespace jpip
{

  const char *DataBinClass::GetName(int class_name)
  {
    static const char *names[] = {
        "PRECINCT",
        "EXTENDED_PRECINCT",
        "TILE_HEADER",
        "UNKNOWN",
        "TILE_DATA",
        "EXTENDED_TILE",
        "MAIN_HEADER",
        "UNKNOWN",
        "META_DATA"
    };

    if((class_name < 0) || (class_name >= (int)sizeof(names))) return "UNKNOWN";
    else return names[class_name];
  }

}
