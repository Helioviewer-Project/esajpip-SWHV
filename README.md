
log4cpp:   http://log4cpp.sourceforge.net

libconfig: https://github.com/hyperrealm/libconfig

## How to build ##

Example:
```
apt-get install build-essential cmake pkg-config libglib2.0-dev zlib1g-dev git
git clone https://github.com/Helioviewer-Project/esajpip-SWHV.git
mkdir build && cd build
cmake ../esajpip-SWHV/ -DCMAKE_INSTALL_PREFIX=$HOME/esajpip -DSWHV_PORT_JPIP=8090 -DSWHV_DIR_IMAGE=$HOME/esajpip/images -DSWHV_DIR_LOG=$HOME/esajpip/log
make install
mkdir $HOME/esajpip/{images,log}
```
