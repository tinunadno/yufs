mkdir build_ko && cd build_ko
cmake -DKERNEL_BUILD=ON ..
make
cp ../src/yufs.ko ..
make yufs_clean