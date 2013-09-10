#ifndef _JPIP_WOI_COMPOSER_H_
#define _JPIP_WOI_COMPOSER_H_


#include "woi.h"
#include "jpeg2000/packet.h"
#include "jpeg2000/coding_parameters.h"


namespace jpip
{

  using namespace std;
  using namespace std::tr1;
  using namespace jpeg2000;


  /**
   * By means of this class it is possible to find out the
   * which packets of an image are associated to a WOI.
   * Given a WOI and the coding parameters of an image, the
   * code of this class allows to navigate, following the
   * LRCP order, through all the associated packets.
   *
   * @see WOI
   * @see CodingParameters
   */
  class WOIComposer
  {
  private:
    Point pxy1;				///< Upper-left corner of the WOI
    Point pxy2;				///< Bottom-right corner of the WOI
    bool more_packets;		///< Flag to control the last packet
    int max_resolution;		///< Maximum resolution
    Size min_precinct_xy;	///< Minimum precinct
    Size max_precinct_xy;	///< Maximum precinct
    Packet current_packet;	///< Current packet

    /**
     * Pointer to the associated coding parameters.
     */
    CodingParameters::Ptr coding_parameters;

  public:
    /**
     * Initializes the object. No packets are available.
     */
    WOIComposer()
    {
      more_packets = false;
    }

    /**
     * Copy constructor.
     */
    WOIComposer(const WOIComposer& composer)
    {
      *this = composer;
    }

    /**
     * Initializes the object. No packets are available.
     * @param coding_parameters Pointer to the coding parameters.
     */
    WOIComposer(CodingParameters::Ptr coding_parameters)
    {
      more_packets = false;
      this->coding_parameters = coding_parameters;
    }

    /**
     * Resets the packets navigation and starts a new one. Sets the
     * current packet to the first packet of the WOI, assuming a
     * LRCP order.
     * @param woi New WOI to use.
     * @param coding_parameters Coding parameters to use.
     */
    void Reset(const WOI& woi, CodingParameters::Ptr coding_parameters)
    {
      more_packets = true;
      current_packet = Packet();
      max_resolution = woi.resolution;
      this->coding_parameters = coding_parameters;

      pxy1 = woi.position * (1L << (coding_parameters->num_levels - woi.resolution));
      pxy2 = (woi.position + woi.size - 1) * (1L << (coding_parameters->num_levels - woi.resolution));

      min_precinct_xy = coding_parameters->GetPrecincts(current_packet.resolution, pxy1);
      if (min_precinct_xy.x != 0) min_precinct_xy.x--;
      if (min_precinct_xy.y != 0) min_precinct_xy.y--;

      max_precinct_xy = coding_parameters->GetPrecincts(current_packet.resolution, pxy2);
      if (max_precinct_xy.x != 0) max_precinct_xy.x--;
      if (max_precinct_xy.y != 0) max_precinct_xy.y--;

      current_packet.precinct_xy = min_precinct_xy;
    }

    /**
     * Copy assignment.
     */
    WOIComposer& operator=(const WOIComposer& composer)
    {
      pxy1 = composer.pxy1;
      pxy2 = composer.pxy2;
      more_packets = composer.more_packets;
      max_resolution = composer.max_resolution;
      current_packet = composer.current_packet;
      min_precinct_xy = composer.min_precinct_xy;
      max_precinct_xy = composer.max_precinct_xy;
      coding_parameters = composer.coding_parameters;

      return *this;
    }

    /**
     * Returns the current packet.
     */
    Packet GetCurrentPacket() const
    {
      return current_packet;
    }

    /**
     * Moves to the next packet of the WOI.
     * @param packet Pointer to store the current packet (not the next one).
     * @return <code>true</code> if successful.
     */
    bool GetNextPacket(Packet *packet = NULL)
    {
      if(!more_packets) return false;
      else {
        if(packet) *packet = current_packet;

        if (current_packet.precinct_xy.x < max_precinct_xy.x) current_packet.precinct_xy.x++;
        else
        {
          current_packet.precinct_xy.x = min_precinct_xy.x;

          if (current_packet.precinct_xy.y < max_precinct_xy.y) current_packet.precinct_xy.y++;
          else
          {
            current_packet.precinct_xy.y = min_precinct_xy.y;

            if (current_packet.component < (coding_parameters->num_components - 1)) current_packet.component++;
            else
            {
              current_packet.component = 0;

              if (current_packet.resolution < max_resolution) current_packet.resolution++;
              else
              {
                current_packet.resolution = 0;

                if (current_packet.layer < (coding_parameters->num_layers - 1)) current_packet.layer++;
                else {
                  more_packets = false;
                  return true;
                }
              }

              min_precinct_xy = coding_parameters->GetPrecincts(current_packet.resolution, pxy1);
              if (min_precinct_xy.x != 0) min_precinct_xy.x--;
              if (min_precinct_xy.y != 0) min_precinct_xy.y--;

              max_precinct_xy = coding_parameters->GetPrecincts(current_packet.resolution, pxy2);
              if (max_precinct_xy.x != 0) max_precinct_xy.x--;
              if (max_precinct_xy.y != 0) max_precinct_xy.y--;

              current_packet.precinct_xy = min_precinct_xy;
            }
          }
        }

        return true;
      }
    }

    virtual ~WOIComposer()
    {
    }
  };

}

#endif /* _JPIP_INDEX_MANAGER_H_ */

