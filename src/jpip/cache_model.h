#ifndef _JPIP_CACHE_MODEL_H_
#define _JPIP_CACHE_MODEL_H_

#include <cassert>
#include <vector>
#include <iostream>
#include <limits.h>
#include "jpip.h"

namespace jpip {

    /**
     * The cache model of a JPIP client is handled using this class.
     * It allows to maintain a cache model recording the amount of
     * data sent by the server regarding the meta-datas, headers,
     * tile-headers and precincts. This implementation only allows to
     * record incremental amounts, from the beginning of each entity.
     * The value <code>INT_MAX</code> is used to specify that an
     * item is complete. This class is serializable.
     */
    class CacheModel {
    public:
        /**
         * Sub-class of the cache model class used to identify a
         * codestream. This class is serializable.
         */
        class Codestream {
        private:
            int header;                ///< Amount for the header
            int tile_header;            ///< Amount for the tile-header
            std::vector<int> precincts;    ///< Amount for the precincts

            /**
             * Minimum identifier of the non-consecutive precinct
             * completely sent. All the initial precincts already
             * sent completely to the client are removed, so this
             * value contains the next precinct. The vector
             * <code>precincts</code> is related to the precincts
             * starting from this index.
             */
            int min_precinct;

            static int AddAmount(int current, int amount) {
                if (current == INT_MAX || amount == INT_MAX || amount > INT_MAX - current)
                    return INT_MAX;
                return current + amount;
            }

        public:
            /**
             * Initializes all the members to zero.
             */
            Codestream() {
                header = 0;
                tile_header = 0;
                min_precinct = 0;
            }

            /**
             * Add the content of the given codestream cache model.
             */
            Codestream &operator+=(const Codestream &model) {
                AddToMainHeader(model.header);
                AddToTileHeader(model.tile_header);

                for (size_t i = 0; i < model.precincts.size(); ++i)
                    AddToPrecinct(model.min_precinct + i, model.precincts[i]);
                return *this;
            }

            /**
             * Returns the amount of the main header.
             */
            int GetMainHeader() const {
                return header;
            }

            /**
             * Returns the amount of the tile header.
             */
            int GetTileHeader() const {
                return tile_header;
            }

            /**
             * Increases the amount of the main header.
             * @param amount Amount increment.
             * @param complete <code>true</code> if the main header
             * is complete after the increment.
             * @return the new amount value.
             */
            int AddToMainHeader(int amount, bool complete = false) {
                if (header != INT_MAX) {
                    if (complete) header = INT_MAX;
                    else header = AddAmount(header, amount);
                }
                return header;
            }

            /**
             * Increases the amount of the tile header.
             * @param amount Amount increment.
             * @param complete <code>true</code> if the tile header
             * is complete after the increment.
             * @return the new amount value.
             */
            int AddToTileHeader(int amount, bool complete = false) {
                if (tile_header != INT_MAX) {
                    if (complete) tile_header = INT_MAX;
                    else tile_header = AddAmount(tile_header, amount);
                }
                return tile_header;
            }

            /**
             * Returns the amount of a precinct.
             * @param num_precinct Index number of the precinct.
             */
            int GetPrecinct(int num_precinct) {
                if (num_precinct < min_precinct) return INT_MAX;
                else {
                    int n = num_precinct - min_precinct;
                    if (n >= (int) precincts.size()) precincts.resize(n + 1, 0);
                    return precincts[n];
                }
            }

            /**
             * Increases the amount of a precinct.
             * @param num_precinct Index number of the precinct.
             * @param amount Amount increment.
             * @param complete <code>true</code> if the precinct
             * is complete after the increment.
             * @return the new amount value.
             */
            int AddToPrecinct(int num_precinct, int amount, bool complete = false) {
                if (num_precinct < min_precinct) return INT_MAX;
                else {
                    int n = num_precinct - min_precinct;
                    if (n >= (int) precincts.size()) precincts.resize(n + 1, 0);
                    int &p = precincts[n];
                    if (p != INT_MAX) {
                        if (complete) p = INT_MAX;
                        else p = AddAmount(p, amount);
                    }
                    return p;
                }
            }

            /**
             * Packs the information stored regarding the precincts,
             * removing those initial elements that are consecutive
             * and completes.
             * @param min_sum Only the packing is performed if there
             * are a number of items equal or greater than this value
             * (1 by default).
             */
            void Pack(int min_sum = 1) {
                int sum = 0;

                for (size_t i = 0; i < precincts.size(); ++i) {
                    if (precincts[i] == INT_MAX) sum++;
                    else break;
                }

                if (sum >= min_sum) {
                    precincts.erase(precincts.begin(), precincts.begin() + sum);
                    min_precinct += sum;
                }
            }
        };

    private:
        /**
         * Says if the meta-data has been totally sent.
         */
        bool full_meta;

        /**
         * Amounts for the meta-datas.
         */
        std::vector<int> meta_data;

        /**
         * Amounts for the codestreams.
         */
        std::vector<Codestream> codestreams;

        static int AddAmount(int current, int amount) {
            if (current == INT_MAX || amount == INT_MAX || amount > INT_MAX - current)
                return INT_MAX;
            return current + amount;
        }

    public:
        /**
         * Empty constructor.
         */
        CacheModel() {
            full_meta = false;
        }

        /**
         * Add the content of the given cache model.
         */
        CacheModel &operator+=(const CacheModel &model) {
            if (!full_meta) {
                for (size_t i = 0; i < model.meta_data.size(); ++i)
                    AddToMetadata(i, model.meta_data[i]);
            }
            for (size_t i = 0; i < model.codestreams.size(); ++i)
                GetCodestream(i) += model.codestreams[i];
            return *this;
        }

        /**
         * Returns the reference of a codestream.
         * @param num_codestream Index number of the codestream.
         */
        Codestream &GetCodestream(int num_codestream) {
            if (num_codestream >= (int) codestreams.size()) codestreams.resize(num_codestream + 1);
            return codestreams[num_codestream];
        }

        /**
         * Returns the amount of a meta-data.
         * @param id Index number of the meta-data.
         */
        int GetMetadata(int id) {
            if (full_meta) return INT_MAX;
            else {
                if (id >= (int) meta_data.size()) meta_data.resize(id + 1, 0);
                return meta_data[id];
            }
        }

        /**
         * Increases the amount of a meta-data.
         * @param id Index number of the meta-data.
         * @param amount Amount increment.
         * @param complete <code>true</code> if the meta-data
         * is complete after the increment.
         * @return the new amount value.
         */
        int AddToMetadata(int id, int amount, bool complete = false) {
            if (full_meta) return INT_MAX;
            else {
                if (GetMetadata(id) != INT_MAX) {
                    if (complete) meta_data[id] = INT_MAX;
                    else meta_data[id] = AddAmount(meta_data[id], amount);
                }
                return meta_data[id];
            }
        }

        int GetDataBin(int bin_class, int num_codestream, int id) {
            switch (bin_class) {
                case DataBinClass::META_DATA:
                    return GetMetadata(id);
                case DataBinClass::MAIN_HEADER:
                    return GetCodestream(num_codestream).GetMainHeader();
                case DataBinClass::TILE_HEADER:
                    return GetCodestream(num_codestream).GetTileHeader();
                case DataBinClass::PRECINCT:
                    return GetCodestream(num_codestream).GetPrecinct(id);
            }
            assert(false);
            return 0;
        }

        int AddToDataBin(int bin_class, int num_codestream, int id, int amount,
                         bool complete = false) {
            switch (bin_class) {
                case DataBinClass::META_DATA:
                    return AddToMetadata(id, amount, complete);
                case DataBinClass::MAIN_HEADER:
                    return GetCodestream(num_codestream).AddToMainHeader(amount, complete);
                case DataBinClass::TILE_HEADER:
                    return GetCodestream(num_codestream).AddToTileHeader(amount, complete);
                case DataBinClass::PRECINCT:
                    return GetCodestream(num_codestream).AddToPrecinct(id, amount, complete);
            }
            assert(false);
            return 0;
        }

        /**
         * Returns the full flag of the meta-datas.
         */
        bool IsFullMetadata() const {
            return full_meta;
        }

        /**
         * Sets the full flag for the meta-datas to true.
         */
        void SetFullMetadata() {
            full_meta = true;
            meta_data.clear();
        }

        /**
         * Calls the <code>Pack</code> method of all the codestreams.
         */
        void Pack(int min_sum = 1) {
            for (size_t i = 0; i < codestreams.size(); ++i)
                codestreams[i].Pack(min_sum);
        }

        /**
         * Clear all the amounts.
         */
        void Clear() {
            full_meta = false;
            meta_data.clear();
            codestreams.clear();
        }

    };
}

#endif /* _JPIP_CACHE_MODEL_H_ */
