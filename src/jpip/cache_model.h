#ifndef _JPIP_CACHE_MODEL_H_
#define _JPIP_CACHE_MODEL_H_

#include <limits.h>
#include <stdexcept>
#include <vector>

#include "jpip.h"

namespace jpip {

    // Records the initial bytes of each data-bin known to be cached by one
    // client. INT_MAX means that the complete data-bin is cached.
    class CacheModel {
    private:
        struct Codestream {
            int header = 0;
            int tile_header = 0;
            std::vector<int> precincts;
            int min_precinct = 0;
        };

        bool full_meta = false;
        std::vector<int> meta_data;
        std::vector<Codestream> codestreams;

        static int AddAmount(int &current, int amount, bool complete) {
            if (current != INT_MAX) {
                if (complete || amount == INT_MAX || amount > INT_MAX - current)
                    current = INT_MAX;
                else
                    current += amount;
            }
            return current;
        }

        Codestream &GetCodestream(int num_codestream) {
            if (num_codestream >= static_cast<int>(codestreams.size()))
                codestreams.resize(num_codestream + 1);
            return codestreams[num_codestream];
        }

        int AddToMetadata(int id, int amount, bool complete = false) {
            if (full_meta)
                return INT_MAX;
            if (id >= static_cast<int>(meta_data.size()))
                meta_data.resize(id + 1, 0);
            return AddAmount(meta_data[id], amount, complete);
        }

        static int *Slot(Codestream &codestream, int id) {
            if (id < codestream.min_precinct)
                return NULL;
            int index = id - codestream.min_precinct;
            if (index >= static_cast<int>(codestream.precincts.size()))
                codestream.precincts.resize(index + 1, 0);
            return &codestream.precincts[index];
        }

        static int GetPrecinct(const Codestream &codestream, int id) {
            if (id < codestream.min_precinct)
                return INT_MAX;
            size_t index = static_cast<size_t>(id - codestream.min_precinct);
            return index < codestream.precincts.size()
                    ? codestream.precincts[index] : 0;
        }

        static int AddToPrecinct(Codestream &codestream, int id, int amount,
                                 bool complete) {
            int *slot = Slot(codestream, id);
            return slot == NULL ? INT_MAX : AddAmount(*slot, amount, complete);
        }

        static void Pack(Codestream &codestream) {
            size_t count = 0;
            while (count < codestream.precincts.size() &&
                   codestream.precincts[count] == INT_MAX)
                ++count;
            if (count == 0)
                return;
            codestream.precincts.erase(codestream.precincts.begin(),
                                       codestream.precincts.begin() + count);
            codestream.min_precinct += static_cast<int>(count);
        }

    public:
        int GetDataBin(DataBinClass bin_class, int num_codestream, int id) const {
            const Codestream *codestream =
                    static_cast<size_t>(num_codestream) < codestreams.size()
                    ? &codestreams[num_codestream] : NULL;
            switch (bin_class) {
                case DataBinClass::META_DATA:
                    if (full_meta)
                        return INT_MAX;
                    return static_cast<size_t>(id) < meta_data.size()
                            ? meta_data[id] : 0;
                case DataBinClass::MAIN_HEADER:
                    return codestream == NULL ? 0 : codestream->header;
                case DataBinClass::TILE_HEADER:
                    return codestream == NULL ? 0 : codestream->tile_header;
                case DataBinClass::PRECINCT:
                    return codestream == NULL ? 0 : GetPrecinct(*codestream, id);
                case DataBinClass::EXTENDED_PRECINCT:
                case DataBinClass::TILE_DATA:
                case DataBinClass::EXTENDED_TILE:
                    throw std::logic_error("Unsupported JPIP data-bin class");
            }
            throw std::logic_error("Invalid JPIP data-bin class");
        }

        int AddToDataBin(DataBinClass bin_class, int num_codestream, int id,
                         int amount, bool complete = false) {
            switch (bin_class) {
                case DataBinClass::META_DATA:
                    return AddToMetadata(id, amount, complete);
                case DataBinClass::MAIN_HEADER: {
                    Codestream &codestream = GetCodestream(num_codestream);
                    return AddAmount(codestream.header, amount, complete);
                }
                case DataBinClass::TILE_HEADER: {
                    Codestream &codestream = GetCodestream(num_codestream);
                    return AddAmount(codestream.tile_header, amount, complete);
                }
                case DataBinClass::PRECINCT: {
                    Codestream &codestream = GetCodestream(num_codestream);
                    return AddToPrecinct(codestream, id, amount, complete);
                }
                case DataBinClass::EXTENDED_PRECINCT:
                case DataBinClass::TILE_DATA:
                case DataBinClass::EXTENDED_TILE:
                    throw std::logic_error("Unsupported JPIP data-bin class");
            }
            throw std::logic_error("Invalid JPIP data-bin class");
        }

        int AugmentDataBin(DataBinClass bin_class, int num_codestream, int id,
                           int amount) {
            int current = GetDataBin(bin_class, num_codestream, id);
            if (current == INT_MAX || amount <= current)
                return current;
            return AddToDataBin(bin_class, num_codestream, id,
                                amount == INT_MAX ? 0 : amount - current,
                                amount == INT_MAX);
        }

        bool IsFullMetadata() const {
            return full_meta;
        }

        void SetFullMetadata() {
            full_meta = true;
            meta_data.clear();
        }

        void Pack() {
            for (Codestream &codestream : codestreams)
                Pack(codestream);
        }

    };
}

#endif /* _JPIP_CACHE_MODEL_H_ */
