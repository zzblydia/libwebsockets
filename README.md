# libwebsockets Integration & Engineering Practice  

Since 2023.06, I have been using `libwebsockets` as a client to connect with `TTS`, `ASR`, and `audio/video streaming` servers.  

After evaluating `uWebsockets`, `websocketCPP`, `beast` and `Poco C++`, I chose `libwebsockets` for its lightweight footprint and high performance.

This repository documents my deep engineering practices with the library, including:

## my ISSUES
| Date | issues |  
| :--- | :--- |  
| **20260707** | [sync-dns: LWS_SERVER_OPTION_DISABLE_IPV6 does not suppress AAAA](https://github.com/warmcat/libwebsockets/issues/3625) |  
| **20251209** | [handle dns response error with log dropping unknown query tid](https://github.com/warmcat/libwebsockets/issues/3520)  |   
| **20250103** | [lws_ssl_client_bio_create with null point wsi->a.vhost->tls.ssl_client_ctx](https://github.com/warmcat/libwebsockets/issues/3306)  |  
| **20240523** | [wss client bind local port](https://github.com/warmcat/libwebsockets/issues/3150) |  


## my PRs
| Date | PR |  
| :--- | :--- |  
| **20260709** | [client: sort-dns: skip AAAA results when ipv6 is disable](https://github.com/warmcat/libwebsockets/pull/3628) |  
| **20260709** | [async-dns: send the A query before AAAA](https://github.com/warmcat/libwebsockets/pull/3627) |   
| **20251230** | [\[cleanup\] Simplify conditional route pointer declarations in lws_sort_dns()](https://github.com/warmcat/libwebsockets/pull/3532) |  
| **20240823** | [fix segmentation fault of lws_snprintf](https://github.com/warmcat/libwebsockets/pull/3212) |  
| **20240624** | [fix segmentation fault when generate client ws handshake failed](https://github.com/warmcat/libwebsockets/pull/3171) |  
| **20231118** | [append OpenSSL library (which is found by pkg-config) with an absolute path](https://github.com/warmcat/libwebsockets/pull/3010) |  


## my EXAMPLES
### 📂 Examples & Resources History

| Date | Example / Resource | Note |
| :--- | :--- | :--- |
| **20260713** | `ws-client-test-AAAA` | NA |
| **20241106** | `ws-client-tools` | NA |
| **20240313** | `ws-client-simple-schedule-send-tls`<br>`ws-client-simple-schedule-send` | NA |
| **20231108** | `ws-client-simple-send-recv-tls`<br>`ws-server-simple-send-recv-tls`<br>`ws-client-simple-send-recv`<br>`ws-server-simple-send-recv` | NA |
| **20231107** | `README.md` (Rewritten)<br>`READMEs/README.build-ubuntu.md`<br>`READMEs/README.build-windows-clion.md` | NA |
