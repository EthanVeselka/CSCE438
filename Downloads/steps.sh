export MY_INSTALL_DIR=$HOME/.grpc
mkdir -p $MY_INSTALL_DIR
export PATH="$MY_INSTALL_DIR/bin:$PATH"
sudo apt-get install -y autoconf
sudo apt-get install -y libtool
sudo apt-get install -y git
sudo apt-get install -y g++
wget -q -O cmake-linux.sh https://github.com/Kitware/CMake/releases/download/v3.19.6/cmake-3.19.6-Linux-x86_64.sh
sh cmake-linux.sh -- --skip-license --prefix=$MY_INSTALL_DIR
rm cmake-linux.sh
git clone --recurse-submodules -b v1.43.0 https://github.com/grpc/grpc
cd grpc
mkdir -p cmake/build
pushd cmake/build

/home/438/.grpc/bin/cmake -DgRPC_INSTALL=ON -DgRPC_BUILD_TESTS=OFF -DCMAKE_INSTALL_PREFIX=$MY_INSTALL_DIR ../..
make
make install
export PKG_CONFIG_PATH=$MY_INSTALL_DIR/lib/pkgconfig/
export PKG_CONFIG_PATH=$MY_INSTALL_DIR/lib64/pkgconfig:$PKG_CONFIG_PATH
cd /home/438/grpc/examples/cpp/helloworld
mkdir -p cmake/build
pushd cmake/build
/home/438/.grpc/bin/cmake -DCMAKE_PREFIX_PATH=$MY_INSTALL_DIR ../..
make
./greeter_server
cd
wget --quiet -O - https://repo.linuxbabe.com/linuxbabe-pubkey.asc | sudo tee /etc/apt/trusted.gpg.d/linuxbabe-pubkey.asc
echo "deb [signed-by=/etc/apt/trusted.gpg.d/linuxbabe-pubkey.asc arch=$( dpkg --print-architecture )] https://repo.linuxbabe.com $(lsb_release -cs) main" | sudo tee /etc/apt/sources.list.d/linuxbabe.list
sudo apt -y update

sudo apt install -y systemback