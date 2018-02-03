#include "trace.h"
#include "app_config.h"
#include "jpip/woi_composer.h"
#include "jpeg2000/index_manager.h"

using namespace std;
using namespace jpip;
using namespace jpeg2000;

struct ui {
    template<typename T>
    static void read(T &v, T mn, T mx) {
        do {
            cin >> v;
        } while ((v < mn) || (v > mx));
    }
};

int main(void) {
    AppConfig cfg;

    if (cfg.Load("server.cfg"))
        cout << endl << cfg << endl;
    else {
        cerr << "The configuration file can not be read" << endl;
        return -1;
    }

    string name_image_file;

    IndexManager im;
    im.Init(cfg.images_folder());

    ImageIndex::Ptr it_node;

    int option;
    do {
        cout << endl
             << "MENU:" << endl
             << "-----" << endl
             << "1) Open image file" << endl
             << "2) Close image file" << endl
             << "3) Get WOI packets from image file" << endl
             << "4) Image file list" << endl
             << "5) Exit" << endl << endl
             << "Option: ";

        ui::read(option, 1, 5);

        switch (option) {
            case 1:
                cout << "Name image file (type 'exit' to finish): ";
                cin >> name_image_file;

                while (name_image_file.compare("exit") != 0) {
                    if (im.OpenImage(name_image_file, &it_node)) cout << "Image file loaded..." << endl;
                    else cout << "Image file not loaded..." << endl;

                    cout << "Name image file (type 'exit' to finish): ";
                    cin >> name_image_file;
                }
                break;

            case 2:
                if (im.GetSize() <= 0) cout << "Image list is empty!" << endl;
                {
                    if (im.CloseImage(it_node)) cout << "Image file closed." << endl;
                    else cout << "Image file not closed!" << endl;
                }
                break;

            case 3:
                if (im.GetSize() <= 0) cout << "Image list is empty!" << endl;
                {
                    int wx, wy, ww, wh, wr, ind_codestream;
                    CodingParameters cp = *it_node->GetCodingParameters();

                    cout << "Number codestream [0-" << (it_node->GetNumCodestreams() - 1) << "]: ";
                    ui::read(ind_codestream, 0, (int) it_node->GetNumCodestreams() - 1);

                    cout << "Resolution [0-" << cp.num_levels << "]: ";
                    ui::read(wr, 0, cp.num_levels);

                    int max_x = ceil((double) cp.size.x / (1L << (cp.num_levels - wr))) - 1;
                    cout << "Coord. X [0-" << max_x << "]: ";
                    ui::read(wx, 0, max_x);

                    int max_y = ceil((double) cp.size.y / (1L << (cp.num_levels - wr))) - 1;
                    cout << "Coord. Y [0-" << max_y << "]: ";
                    ui::read(wy, 0, max_y);

                    int max_width = ceil((double) cp.size.x / (1L << (cp.num_levels - wr))) - wx;
                    cout << "Width [0-" << max_width << "]: ";
                    ui::read(ww, 0, max_width);

                    int max_height = ceil((double) cp.size.y / (1L << (cp.num_levels - wr))) - wy;
                    cout << "Height [0-" << max_height << "]: ";
                    ui::read(wh, 0, max_height);

                    WOIComposer woi_composer;
                    WOI woi(Point(wx, wy), Size(wh, ww), wr);
                    cout << woi << endl;

                    woi_composer.Reset(woi, it_node->GetCodingParameters());

                    cout << "ID\tL\tR\tC\tPY\tPX\tOFFSET:LENGTH" << endl;
                    cout << string(60, '-') << endl;

                    it_node->ReadLock(Range(0, 0));

                    Packet packet;
                    while (woi_composer.GetNextPacket(&packet))
                        cout << it_node->GetCodingParameters()->GetPrecinctDataBinId(packet)
                             << "\t" << packet << "\t" << it_node->GetPacket(ind_codestream, packet) << endl;

                    cout << string(60, '-') << endl;

                    it_node->ReadUnlock(Range(0, 0));
                }
                break;

            case 4:
                int i = 0;
                cout << endl;
                for (ImageIndex::Ptr it = im.GetBegin(); it != im.GetEnd(); ++it) {
                    cout
                            << "Node " << i++ << ":" << endl
                            << string(60, '-') << endl
                            << *it << endl
                            << string(60, '-') << endl
                            << endl;
                }
                break;
        }
    } while (option != 5);

    return 0;
}
