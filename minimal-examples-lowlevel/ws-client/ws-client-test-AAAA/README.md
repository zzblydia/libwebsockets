# ws-client-test-AAAA

验证 `lib/core-net/client/sort-dns.c` 中「按 `wsi->ipv6` 过滤 IPv6(AAAA)DNS 结果」
改动的最小测试客户端。

```
https://github.com/warmcat/libwebsockets/pull/3627
https://github.com/warmcat/libwebsockets/pull/3628
```

## 功能

- 作为一个 **TLS WebSocket 客户端**,通过**域名**连接目标 WS 服务端。
- 域名解析走 lws 异步 DNS,会同时查询 **A(IPv4)与 AAAA(IPv6)** 记录,解析结果在
  客户端 wsi 上进入 `lws_sort_dns()`,从而触发 `sort-dns.c` 里的 IPv6 过滤逻辑。
- 通过**启动参数**在三种场景间切换,验证 `LWS_SERVER_OPTION_DISABLE_IPV6` 分别设在
  **context 级**与 **vhost 级**时都能正确过滤掉 AAAA 结果。
- 连接逻辑直接放在 `main()` 中(用手动创建的 vhost 发起),不经过
  `LWS_CALLBACK_PROTOCOL_INIT` 回调,时序更直观。

## 启动参数

程序读取 `argv[1]` 作为测试开关,缺省 `0`:

| 参数 | 场景 | 设置点 | 预期(IPv6 已启用时) |
|------|------|--------|----------------------|
| `0`  | 对照组 | 都不设 | A + AAAA 均保留 |
| `1`  | vhost 级禁用 IPv6 | `vh_info.options \|= LWS_SERVER_OPTION_DISABLE_IPV6` | AAAA 被过滤,只剩 A |
| `2`  | context 级禁用 IPv6 | `ctx_info.options \|= LWS_SERVER_OPTION_DISABLE_IPV6` | AAAA 被过滤,只剩 A |

`wsi->ipv6` 由 `LWS_IPV6_ENABLED(vhost)` 决定,即 **context 或 vhost 任一**设置
`DISABLE_IPV6` 即为 0,故场景 1、2 均触发过滤。

## vhost 路径(三场景各走各的自然路径)

不同场景使用不同的 vhost 创建方式,`main()` 里按 `argv[1]` 分支:

| 场景 | vhost 路径 | 说明 |
|------|-----------|------|
| `0` / `2` | **context 隐式默认 vhost** | 不加 `EXPLICIT_VHOSTS`,由 `lws_create_context()` 自动建名为 `default` 的 vhost,再用 `lws_get_vhost_by_name(context, "default")` 取得 |
| `1` | **手动创建 vhost** | 加 `EXPLICIT_VHOSTS` 阻止隐式 vhost,再用 `lws_create_vhost()` 单独建一个并在其上设 `DISABLE_IPV6` |

要点:

- **只有 S1 需要手动建 vhost**——因为 vhost 级 `DISABLE_IPV6` 必须落在一个独立配置的
  vhost 上;S0/S2 是 context 级(或都不设),用隐式默认 vhost 即可,更贴近真实用法。
- **client SSL 上下文**:隐式默认 vhost 从 `ctx_info` 继承 `LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT`,
  自动创建 client SSL ctx;而手动裸建的 vhost(S1)**必须在 `vh_info.options` 里自带**
  `LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT`,否则 `wss` 连接会报 `null ssl ctx`。

## 连接目标(需自行准备)

源码中硬编码了测试用的域名/端口,请按你的环境修改:

- 目标域名:`www.goodluck.com`(需解析到 **A + AAAA 双栈**,才能观测 AAAA 过滤)
- 目标端口:`8001`
- TLS SNI / Host:`goodluck.com`

示例默认放宽了证书校验(`LCCSCF_ALLOW_SELFSIGNED` /
`LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK` / `LCCSCF_ALLOW_INSECURE`),便于自签名
服务端联调。

## 编译选项(lws 构建时)

CMake 层面本示例要求(见本目录 `CMakeLists.txt`):

- `LWS_ROLE_WS = 1` —— WebSocket 角色
- `LWS_WITH_SERVER = 1` —— 需要 vhost / server option 相关 API

要**真正观测到 AAAA 过滤**,lws 还需按下列选项构建:

- `LWS_IPV6 = ON` —— 否则 `sort-dns.c` 的 `#if defined(LWS_WITH_IPV6)` 过滤块不编译,
  且 `LWS_IPV6_ENABLED()` 恒为 0。
- `LWS_WITH_SYS_ASYNC_DNS = ON` —— 使用异步 DNS,解析域名时会同时查 A + AAAA。
- `LWS_WITH_TLS = ON`(及某个 TLS 后端,如 OpenSSL)—— 本示例用 `wss://`。
- `CMAKE_BUILD_TYPE = DEBUG` 或 `RelWithDebInfo` —— 若需 gdb 抓 `lws_sort_dns` 调用栈。

参考构建命令:

```bash
cmake -S . -B build \
      -DLWS_IPV6=ON \
      -DLWS_WITH_SYS_ASYNC_DNS=ON \
      -DLWS_WITH_TLS=ON \
      -DCMAKE_BUILD_TYPE=DEBUG
cmake --build build --target ws-client-test-AAAA -j$(nproc)
```

## 运行

```bash
./build/bin/ws-client-test-AAAA 0   # 对照组: A + AAAA 保留
./build/bin/ws-client-test-AAAA 1   # vhost 级 DISABLE_IPV6: AAAA 被过滤
./build/bin/ws-client-test-AAAA 2   # context 级 DISABLE_IPV6: AAAA 被过滤
```

## 观察点

若已在 `sort-dns.c` 中加了调试日志,关注三段(NOTICE 级默认已开):

- `sort_dns BEFORE[*]` —— 过滤前完整列表,三场景都应含 A(af=2)与 AAAA(af=10)。
- `sort_dns chk` —— 逐条 keep / FILTERED 判定。
- `sort_dns AFTER[*]` —— 过滤后存活项,场景 1/2 只剩 IPv4。

连接成功时回调打印 `Connected to server ok!`。
