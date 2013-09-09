#include "trace.h"
#include "image_index.h"


namespace jpeg2000
{

  bool ImageIndex::Init(const string& path_name, const ImageInfo& image_info)
  {
    num_references = 1;
    this->path_name = path_name;
    this->coding_parameters = CodingParameters::Ptr(new CodingParameters(image_info.coding_parameters));

    meta_data = image_info.meta_data;

    if (image_info.paths.size() == 0) {
      base::copy(codestreams, image_info.codestreams);
      max_resolution.resize(codestreams.size(), -1);

      for (vector<CodestreamIndex>::const_iterator i = codestreams.begin(); i != codestreams.end(); i++)
      {
    	last_plt.push_back(0);
    	last_packet.push_back(0);
        last_offset_PLT.push_back(0);
        last_offset_packet.push_back(0);
        packet_indexes.push_back(PacketIndex());
      }
    }

    rdwr_lock = RdWrLock::Ptr(new RdWrLock());
    return rdwr_lock->Init();
  }

  bool ImageIndex::Init(const string& path_name, CodingParameters::Ptr coding_parameters, const ImageInfo& image_info, int index)
  {
    num_references = 1;
    this->path_name = path_name;
    this->coding_parameters = coding_parameters;

    meta_data=image_info.meta_data_hyperlinks[index];

    codestreams.push_back(image_info.codestreams[index]);
    max_resolution.push_back(-1);

    last_plt.push_back(0);
    last_packet.push_back(0);
    last_offset_PLT.push_back(0);
    last_offset_packet.push_back(0);
    packet_indexes.push_back(PacketIndex());

    rdwr_lock = RdWrLock::Ptr(new RdWrLock());
    return rdwr_lock->Init();
  }

  bool ImageIndex::BuildIndex(int ind_codestream, int r)
  {
    File file;
    bool res = true;

    if(!(res = res && rdwr_lock->Release()))
      ERROR("The lock of the image '" << path_name << "' can not be released");

    if(!(res = res && (rdwr_lock->WaitForWriting() == WAIT_OBJECT)))
      ERROR("The lock of the image '" << path_name << "' can not be taken for writing");

    // Open file for reading
    if((res = res && file.OpenForReading(path_name)))
    {
        // Check if PacketIndex has been created
        if (packet_indexes[ind_codestream].Size() == 0)
          packet_indexes[ind_codestream] = PacketIndex(file.GetSize());

        // Check the upper top of the index (to build)
        int max_index;

        if ((r < coding_parameters->num_levels) && (coding_parameters->IsResolutionProgression()))
        {
          // The max_index is the last packet index of the resolution r
          Packet packet(0, r + 1, 0, Size(0, 0));
          max_index = coding_parameters->GetProgressionIndex(packet) - 1;
        }
        else
        {
          // The max_index is the last packet of the image file
          Size precinct_point = coding_parameters->GetPrecincts(coding_parameters->num_levels, coding_parameters->size) - 1;
          Packet packet(coding_parameters->num_layers - 1, coding_parameters->num_levels, coding_parameters->num_components - 1, precinct_point);
          max_index = coding_parameters->GetProgressionIndex(packet);
        }

        uint64_t length_packet = 0;

        while (packet_indexes[ind_codestream].Size() <= max_index)
        {
          //GetPLTLength(file, ind_codestream, &length_packet);
          res = res && GetPLTLength(file, ind_codestream, &length_packet);
          GetOffsetPacket(file, ind_codestream, length_packet);
        }

        file.Close();
    }

    if(!(res = res && rdwr_lock->Release()))
      ERROR("The lock of the image '" << path_name << "' can not be released");

    if(!(res = res && (rdwr_lock->Wait() == WAIT_OBJECT)))
      ERROR("The lock of the image '" << path_name << "' can not be taken for reading");

    return res;
  }

  bool ImageIndex::GetPLTLength(const File& file, int ind_codestream, uint64_t *length_packet)
  {
	bool res = true;
    vector<FileSegment>& plt = codestreams[ind_codestream].PLT_markers;
    // Get packet plt offset
    if (last_offset_PLT[ind_codestream] == 0) res = res && file.Seek(plt[last_plt[ind_codestream]].offset, SEEK_SET);
    else res = res && file.Seek(last_offset_PLT[ind_codestream], SEEK_SET);

    // Get packet length
    uint8_t buf_packet;
    uint8_t length_packet_partial;
    uint8_t partial = 1;

    *length_packet = 0;
    while (partial)
    {
      res = res && file.Read(&buf_packet, 1);
      partial = buf_packet >> 7; // To get if the packet is final or not
      length_packet_partial = buf_packet & 127; // To get the packet length
      *length_packet = (*length_packet << 7) | length_packet_partial;
    }

    last_offset_PLT[ind_codestream] = file.GetOffset();
    if (last_offset_PLT[ind_codestream] == (plt[last_plt[ind_codestream]].offset + plt[last_plt[ind_codestream]].length))
    {
      last_plt[ind_codestream]++;
      last_offset_PLT[ind_codestream] = 0;
    }
    return res;
  }

  void ImageIndex::GetOffsetPacket(const File& file, int ind_codestream, uint64_t length_packet)
  {
    uint64_t offset;
    vector<FileSegment>& packets = codestreams[ind_codestream].packets;

    if (last_offset_packet[ind_codestream] == 0) offset = packets[last_packet[ind_codestream]].offset;
    else offset = last_offset_packet[ind_codestream];

    packet_indexes[ind_codestream].Add(FileSegment(offset, length_packet));
    last_offset_packet[ind_codestream] = offset + length_packet;

    if (last_offset_packet[ind_codestream] == (packets[last_packet[ind_codestream]].offset + packets[last_packet[ind_codestream]].length))
    {
      last_packet[ind_codestream]++;
      last_offset_packet[ind_codestream] = 0;
    }
  }

  FileSegment ImageIndex::GetPacket(int num_codestream, const Packet& packet, int *offset)
  {
    FileSegment segment = FileSegment::Null;

    if (hyper_links.size()>0)
    {
    	if(packet.resolution > hyper_links[num_codestream]->max_resolution.back()) {
    		if(!hyper_links[num_codestream]->BuildIndex(0,packet.resolution))
    			ERROR("The packet index could not be created");
    		hyper_links[num_codestream]->max_resolution.back() = packet.resolution;
    	}
    }
    else
    {
    	if(packet.resolution > max_resolution[num_codestream]) {
    		if(!BuildIndex(num_codestream, packet.resolution))
    			ERROR("The packet index could not be created");
    		max_resolution[num_codestream] = packet.resolution;
    	}
    }
    PacketIndex& packet_index = (hyper_links.size()>0) ? hyper_links[num_codestream]->packet_indexes[0]: packet_indexes[num_codestream];

    //PacketIndex& packet_index = packet_indexes[num_codestream];
    int idx = coding_parameters->GetProgressionIndex(packet);
    segment = packet_index[idx];

    if(offset != NULL) {
    	*offset = 0;

    	if(coding_parameters->progression == CodingParameters::RPCL_PROGRESSION) {
    		for(int l = packet.layer; l > 0; l--)
    			*offset += packet_index[--idx].length;
    	} else {
    		Packet p_aux = packet;
            for(int l = 0; l < packet.layer; l++) {
            	p_aux.layer = l;
                idx = coding_parameters->GetProgressionIndex(p_aux);
                *offset += packet_index[idx].length;
            }
        }
    }

    return segment;
  }

  bool ImageIndex::ReadLock(const Range& range)
  {
    bool res = true;

    if (hyper_links.size() <= 0)
      res = (rdwr_lock->Wait() == WAIT_OBJECT);
    else
    {
      for(int i = range.first; i <= range.last; i++)
        res = res && hyper_links[i]->ReadLock();
    }

    return res;
  }

  bool ImageIndex::ReadUnlock(const Range& range)
  {
    bool res = true;

    if (hyper_links.size() <= 0)
      res = rdwr_lock->Release();
    else {
      for(int i = range.first; i <= range.last; i++)
        res = res && hyper_links[i]->ReadUnlock();
    }

    return res;
  }

}

