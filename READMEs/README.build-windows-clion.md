# Some notes for the windows with clion  

## env prepare  
### 1.install OpenSSL-Win64  
https://slproweb.com/download/Win64OpenSSL-3_0_9.msi  

### 2.install clion (2023.1.1)  

### 3.code prepare  
git clone https://libwebsockets.org/repo/libwebsockets  

### 4.create project  
open clion ---> File(menu bar) ---> open libwebsockets root folder  

### 5.build with LWS_WITH_MINIMAL_EXAMPLES  
modify CMakeLists.txt to set option LWS_WITH_MINIMAL_EXAMPLES ON (or add Clion Cmake options -DLWS_WITH_MINIMAL_EXAMPLES=ON)  
Project libwebsockets ---> Reload CMake Project  

### 6.build  
Clion ---> Build(menu bar) ---> Build Project   