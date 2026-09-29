#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "jpeg2000/hv_geometry.h"
#include "jpip/index/image_index.h"

static void check(bool ok, const std::string &message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}

// Keeps immutable bytes in memory; no FileManager or server runtime.
struct Sources : jpip::SourceProvider {
    struct Entry { std::vector<char> bytes; jpip::Source source; };
    std::map<std::string, Entry> files;
    const jpip::Source *GetSource(const std::string &path) override {
        auto found = files.find(path);
        if (found != files.end()) return &found->second.source;
        std::ifstream input(path, std::ios::binary);
        if (!input) return NULL;
        Entry &entry = files[path];
        entry.bytes.assign(std::istreambuf_iterator<char>(input), {});
        entry.source = jpip::Source(entry.bytes.data(), entry.bytes.size());
        return &entry.source;
    }
};

static void geometry() {
    std::mt19937 random(0x47454f4d);
    // Preserve the small-precinct cases, then cover dimensions just below,
    // at and above powers of two through 16384 in every progression order.
    const int small_cases = 20000;
    const int large_cases = 8 * 3 * 5;
    for (int trial = 0; trial < small_cases + large_cases; ++trial) {
        SizFixed fixed = {};
        fixed.xsiz = fixed.xtsiz = 1 + random() % 65;
        fixed.ysiz = fixed.ytsiz = 1 + random() % 65;
        bool large = trial >= small_cases;
        if (large) {
            int i = trial - small_cases;
            int side = 1 << (7 + i / 15);
            int edge = (i / 5) % 3 - 1;
            fixed.xsiz = fixed.xtsiz = side + edge;
            fixed.ysiz = fixed.ytsiz = (i % 2 ? side / 2 : side) + edge;
        }
        fixed.csiz = 1 + random() % 3;
        Component components[3] = {};
        for (Component &component : components) {
            component.depthMinus1 = 7;
            component.xrsiz = component.yrsiz = 1;
        }
        hv_siz siz = {&fixed, components, static_cast<size_t>(fixed.csiz)};
        Cod cod = {};
        cod.sgcod.layers = 1 + random() % 4;
        cod.sgcod.progression = trial % 5;
        cod.spcod.levels = random() % 7;
        cod.spcod.transform = 1;
        cod.scod.customPrecincts = trial % 4 != 0;
        if (cod.scod.customPrecincts) {
            cod.spcod.precincts.nCount = static_cast<int>(cod.spcod.levels) + 1;
            for (int r = 0; r < cod.spcod.precincts.nCount; ++r) {
                // Bound packet enumeration for the large images.
                cod.spcod.precincts.arr[r].ppx = large ? 8 + random() % 3 : (r != 0) + random() % 7;
                cod.spcod.precincts.arr[r].ppy = large ? 8 + random() % 3 : (r != 0) + random() % 7;
            }
        }
        jpip::CodingParameters params;
        params.size = jpip::Size(fixed.xsiz, fixed.ysiz);
        params.num_components = fixed.csiz;
        params.num_layers = cod.sgcod.layers;
        params.num_levels = cod.spcod.levels;
        params.progression = cod.sgcod.progression;
        for (int r = 0; r <= params.num_levels; ++r) {
            int px = cod.scod.customPrecincts ? cod.spcod.precincts.arr[r].ppx : 15;
            int py = cod.scod.customPrecincts ? cod.spcod.precincts.arr[r].ppy : 15;
            params.resolutions.emplace_back(1 << px, 1 << py);
        }
        check(params.FillPrecinctCounts(), "compact geometry rejected case " + std::to_string(trial));
        hv_geometry g;
        hv_geometry_limits limits = {21, 1000000, NULL};
        char error[256] = {};
        check(hv_geometry_init(&g, &siz, &cod, 0, &limits, error, sizeof error) == 0,
              "reference geometry rejected case " + std::to_string(trial) + ": " + error);
        hv_packet *packets = NULL;
        size_t count = 0;
        check(hv_geometry_packets(&g, &packets, &count, error, sizeof error) == 0, error);
        check(count == static_cast<size_t>(params.GetNumPackets()), "packet count differs");
        for (int c = 0; c < params.num_components; ++c) {
            for (int r = 0; r <= params.num_levels; ++r) {
                const hv_resolution *res = hv_geometry_res(&g,c,r);
                check(res->pw == params.resolutions[r].num_precincts.x &&
                      res->ph == params.resolutions[r].num_precincts.y, "precinct grid differs");
            }
        }
        for (size_t i = 0; i < count; ++i) {
            const hv_packet &p = packets[i];
            const hv_resolution &res = g.res[p.resolution];
            int64_t x, y;
            hv_precinct_cell(&res,p.precinct,&x,&y);
            jpip::Packet packet(p.layer,res.r,res.c,jpip::Point(x,y));
            check(params.GetProgressionIndex(packet) == static_cast<int>(i),
                  "progression differs in case " + std::to_string(trial) + " packet " + std::to_string(i));
            uint64_t precinct = res.first_precinct - hv_geometry_res(&g,res.c,0)->first_precinct + p.precinct;
            check(params.GetPrecinctDataBinId(packet) == static_cast<int>(precinct * fixed.csiz + res.c),
                  "precinct bin ID differs");
        }
        free(packets);
        hv_geometry_free(&g);
    }
}

struct Query { int stream; jpip::Packet packet; };

static void pauses() {
    std::ifstream manifest(std::string(VECTORS) + "/manifest.tsv");
    check(manifest.good(), "missing corpus manifest");
    std::string line;
    std::getline(manifest,line);
    std::mt19937 random(0x504c5421);
    size_t visited = 0, opened = 0, failed = 0;
    while (std::getline(manifest,line)) {
        std::istringstream row(line);
        std::string name, kind, standard, profile;
        std::getline(row,name,'\t'); std::getline(row,kind,'\t');
        std::getline(row,standard,'\t'); std::getline(row,profile,'\t');
        check(!name.empty() && (profile=="valid" || profile=="invalid"), "invalid manifest row");
        ++visited;
        Sources sources;
        std::string path = std::string(VECTORS) + "/" + name;
        const jpip::Source *source = sources.GetSource(path);
        check(source != NULL, "missing vector " + name);
        jpip::ImageIndex ordered(path), shuffled(path);
        bool ok = ordered.Open(*source,sources,kind=="jpx");
        check(shuffled.Open(*source,sources,kind=="jpx") == ok, "open disagreement: " + name);
        if (!ok) {
            check(profile=="invalid" && ordered.GetError()==shuffled.GetError(), "open failure: " + name);
            continue;
        }
        ++opened;
        std::vector<Query> queries;
        for (size_t stream = 0; stream < ordered.GetNumCodestreams(); ++stream) {
            const jpip::CodingParameters &p = *ordered.GetCodingParameters(stream);
            const jpip::Source *bytes = sources.GetSource(ordered.GetPathName(stream));
            check(bytes != NULL, "missing indexed source");
            // A nonzero packet needs a byte. Impossible declarations are
            // exercised by a late query, without allocating their claimed size.
            if (static_cast<size_t>(p.GetNumPackets()) > bytes->GetSize()) {
                const jpip::Size &n = p.resolutions.back().num_precincts;
                queries.push_back({static_cast<int>(stream),jpip::Packet(p.num_layers-1,p.num_levels,
                    p.num_components-1,jpip::Point(n.x-1,n.y-1))});
                continue;
            }
            size_t first = queries.size();
            queries.resize(first + p.GetNumPackets());
            for (int r=0;r<=p.num_levels;++r) for(int c=0;c<p.num_components;++c)
                for(int y=0;y<p.resolutions[r].num_precincts.y;++y)
                    for(int x=0;x<p.resolutions[r].num_precincts.x;++x) for(int l=0;l<p.num_layers;++l) {
                        jpip::Packet packet(l,r,c,jpip::Point(x,y));
                        int index=p.GetProgressionIndex(packet);
                        check(index>=0 && index<p.GetNumPackets(), "invalid progression index");
                        queries[first+index]={static_cast<int>(stream),packet};
                    }
        }
        std::vector<jpip::FileSegment> expected;
        for (const Query &q : queries) {
            jpip::FileSegment range;
            if (!ordered.GetPacket(sources.GetSource(ordered.GetPathName(q.stream)),q.stream,q.packet,&range)) break;
            expected.push_back(range);
        }
        check((expected.size()==queries.size()) == (profile=="valid"), "profile outcome differs: " + name);
        std::vector<size_t> order(queries.size());
        for(size_t i=0;i<order.size();++i)order[i]=i;
        for (size_t first=0;first<order.size();) {
            size_t end=first+1;
            while(end<order.size() && queries[end].stream==queries[first].stream)++end;
            std::shuffle(order.begin()+first,order.begin()+end,random);
            first=end;
        }
        bool random_failed=false;
        for(size_t i : order) {
            const Query &q=queries[i]; jpip::FileSegment range;
            bool success=shuffled.GetPacket(sources.GetSource(shuffled.GetPathName(q.stream)),q.stream,q.packet,&range);
            if (!success) {
                random_failed=true;
                check(expected.size()!=queries.size() && shuffled.GetError()==ordered.GetError(),
                      "pause order changed error or offset: " + name);
                break;
            }
            if (i<expected.size())check(range.offset==expected[i].offset && range.length==expected[i].length,
                                       "pause order changed packet range: " + name);
        }
        check(random_failed == (expected.size()!=queries.size()), "pause order changed acceptance: " + name);
        if(expected.size()!=queries.size())++failed;
    }
    check(visited && opened && failed, "corpus traversal omitted an outcome class");
    std::cout << "Corpus: " << visited << " vectors, " << opened << " structurally opened, "
              << failed << " rejected during indexing\n";
}

int main() {
    geometry();
    pauses();
    std::cout << "PASS: geometry differential and corpus indexing pause independence\n";
}
