# build on ubuntu  
sudo apt install cmake  
sudo apt install openssl  
sudo apt install libssl-dev  

git clone https://libwebsockets.org/repo/libwebsockets  
cd libwebsockets  
mkdir build  
cd build  
cmake .. -DCMAKE_BUILD_TYPE=DEBUG
make && sudo make install

## When there are multiple versions of OpenSSL present  
cmake .. -DLWS_OPENSSL_INCLUDE_DIRS=/usr/local/include/openssl -DLWS_OPENSSL_LIBRARIES="/usr/local/lib64/libssl.so;/usr/local/lib64/libcrypto.so"  
cmake .. -DLWS_OPENSSL_INCLUDE_DIRS=/usr/local/include/openssl -DLWS_OPENSSL_LIBRARIES="/usr/local/lib64/libssl.a;/usr/local/lib64/libcrypto.a"  

## build with LWS_WITH_MINIMAL_EXAMPLES 
cmake .. -DLWS_WITH_MINIMAL_EXAMPLES=ON // but it will be installed into /usr/local/bin when make install  