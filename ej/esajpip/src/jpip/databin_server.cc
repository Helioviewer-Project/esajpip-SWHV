#include "databin_server.h"
#include "data/file_segment.h"


namespace jpip
{

  bool DataBinServer::Reset(const ImageIndex::Ptr image_index)
  {
    files.clear();
    metareq = false;
    has_woi = false;
    im_index = image_index;

    file = File::Ptr(new File());

    if(!file->OpenForReading(im_index->GetPathName())) return false;
    else {
      if (!im_index->IsHyperLinked(0)) files.push_back(file);
      else
      {
        File::Ptr hyperlinked_file = File::Ptr(new File());
        if (!hyperlinked_file->OpenForReading(im_index->GetPathName(0))) return false;
        else files.push_back(hyperlinked_file);
      }

    	return true;
    }
  }

  bool DataBinServer::SetRequest(const Request& req)
  {
    bool res = true;
    bool reset_woi = false;

    data_writer.ClearPreviousIds();

    if((has_woi = req.mask.HasWOI())) {
      WOI new_woi;
      new_woi.size = req.woi_size;
      new_woi.position = req.woi_position;
      req.GetResolution(*im_index, &new_woi);

      if(new_woi != woi) {
        reset_woi = true;
        woi = new_woi;
      }
    }

    if(req.mask.items.model)
      cache_model += req.cache_model;

    if (req.mask.items.metareq)
      metareq = true;

    if(req.mask.items.stream || req.mask.items.context) {
      Range new_range = Range(req.min_codestream, req.max_codestream);

      if(new_range != range) {
        reset_woi = true;

        if(new_range.Length() != range.Length())
          files.resize(new_range.Length());

        range = new_range;
        current_idx = range.first;

        for(int i = 0; i < range.Length(); i++) {
          int idx = range.GetItem(i);

          if(!im_index->IsHyperLinked(idx)) files[i] = file;
          else {
            files[i] = File::Ptr(new File());
            res = res && files[i]->OpenForReading(im_index->GetPathName(idx));
          }
        }
      }
    }

    if(req.mask.items.len)
      pending = req.length_response;

    if(reset_woi) {
      end_woi_ = false;
      woi_composer.Reset(woi, *im_index);
    }

    return res;
  }
	
  bool DataBinServer::GenerateChunk(char *buff, int *len, bool *last)
  {
    int res;

    data_writer.SetBuffer(buff, min(pending, *len));

    if(pending > 0) {
      eof = false;

      if(!im_index->ReadLock(range)) {
        ERROR("The lock of the image '" << im_index->GetPathName() << "' can not be taken for reading");
        return false;
      }

      if(!cache_model.IsFullMetadata())
      {
        if(im_index->GetNumMetadatas() <= 0)
          WriteSegment<DataBinClass::META_DATA>(0, 0, FileSegment::Null);
        else {
          int bin_offset = 0;
          bool last_metadata = false;

          for (int i = 0; i < im_index->GetNumMetadatas(); i++)
          {
            last_metadata = (i == (im_index->GetNumMetadatas() - 1));
            res = WriteSegment<DataBinClass::META_DATA>(0, 0, im_index->GetMetadata(i), bin_offset, last_metadata);
            bin_offset += im_index->GetMetadata(i).length;

            if (last_metadata) {
              if(res > 0) cache_model.SetFullMetadata();

            } else {
              if (WritePlaceHolder(0, 0, im_index->GetPlaceHolder(i), bin_offset) <= 0) break;
              bin_offset += im_index->GetPlaceHolder(i).length();
            }
          }
        }
      }

      if (!eof)
      {
        for (int i = range.first; i <= range.last; i++)
        {
          WriteSegment<DataBinClass::MAIN_HEADER>(i, 0, im_index->GetMainHeader(i));
          WriteSegment<DataBinClass::TILE_HEADER>(i, 0, FileSegment::Null);
        }

        if(has_woi) {
          Packet packet;
          FileSegment segment;
          int bin_id, bin_offset;
          bool last_packet = false;

          while(data_writer && !eof) {
            packet = woi_composer.GetCurrentPacket();

            segment = im_index->GetPacket(current_idx, packet, &bin_offset);
            bin_id = im_index->GetCodingParameters()->GetPrecinctDataBinId(packet);
            last_packet = (packet.layer >= (im_index->GetCodingParameters()->num_layers - 1));

            res = WriteSegment<DataBinClass::PRECINCT>(current_idx, bin_id, segment, bin_offset, last_packet);

            if(res < 0) return false;
            else if(res > 0) {
              if (current_idx != range.last) current_idx++;
              else
              {
                if(!woi_composer.GetNextPacket()) break;
                else current_idx = range.first;
              }
            }
          }
        }
      }

      if(!eof) {
        data_writer.WriteEOR(EOR::WINDOW_DONE);
        end_woi_ = true;
        pending = 0;

      } else {
        pending -= data_writer.GetCount();

        if(pending <= MINIMUM_SPACE + 100) {
          data_writer.WriteEOR(EOR::BYTE_LIMIT_REACHED);
          pending = 0;
        }
      }

      if(!im_index->ReadUnlock(range)) {
        ERROR("The lock of the image '" << im_index->GetPathName() << "' can not be released");
        return false;
      }
    }

    *len = data_writer.GetCount();
    *last = (pending <= 0);

    if(*last) cache_model.Pack();

    return data_writer;
  }

}
