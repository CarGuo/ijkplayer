# FFmpeg 5 RTSP 网络兼容性与本次修复范围

本次基于 FFmpeg `ff87490262107e004f6a16765484e36e1973d705`、IJK `bb1d1b9fa07019155c88fe69fea9db896a108d0f` 做普通协议场景回归。测试使用本机回环 RTSP 服务、有效 SDP/PCMU RTP 和进程内解析器注入；没有修改系统 DNS，也没有使用旧设备地址。

## 同一个 IP、不同端口，需要分三种情况

1. **分别打开两个播放地址**：在同一进程按 A 端口 → B 端口 → A 端口重新打开，基线即正常，三次包内容身份为 A → B → A。没有复现端口缓存串流。
2. **服务器返回 302 + Location 跳到另一个端口**：基线在 OPTIONS/DESCRIBE 阶段正常，在 SETUP 或初次 PLAY 阶段失败。本次补齐这两个初始化阶段的跳转；每次重建会话，清理旧 SDP 流、时长和标题信息。同 IP 不同端口仍按不同 origin 处理，旧认证状态和 Session 不传给新 origin。总计最多跟随 8 次跳转。
3. **没有 302，只在 Content-Base 或 SDP 控制 URL 中写另一个端口**：仍有单控制连接限制。最小复现：A 端口返回成功 DESCRIBE，Content-Base 为 B 端口；播放器随后把指向 B 的 SETUP URI 发在 A 的原连接上。若 A 不代理 B，则 A 返回 404，B 未收到请求。这个条件可能与“同地址不同端口”描述接近，但没有旧现场 URL，不能断定它就是原故障。

第三种情况不能简单通过“看到不同端口就重连”修复。对照测试中，同样的 Content-Base，若 A 是接受绝对控制 URI 的网关，播放在 A 原连接上正常；强行改连 B 会改变它的路由语义。RTSP 标准允许不同媒体使用不同服务器；正确通用实现需要独立控制会话、认证、Transport 和 RTP 同步管理。本次没有冒险增加这套功能。服务器可明确用 302 指定播放入口，或应用直接打开已验证的最终播放 URL；不能把 Content-Base 自动当成 Location。

规范背景：[RFC 2326 的控制 URL 与多服务器示例](https://www.rfc-editor.org/rfc/rfc2326.html#appendix-C)；请求的绝对 URI 也可由代理处理。Location 在 RTSP 1.0 引用的规则中使用绝对 URL，因此相对 Location 仍返回错误，未擅自猜测路径。[Location 定义](https://www.rfc-editor.org/rfc/rfc2068.html#section-14.30)

## timeout 的单位和行为

FFmpeg 5 RTSP 的 FORMAT `timeout` 是**微秒**。20 秒应设 `20_000_000L`；旧示例中的 `20000` 只有 20 毫秒。本次 IJK 不再把 RTSP 的 timeout 当作 RTMP 参数删除，没有自动乘单位，也没有改变默认值。

```java
player.setOption(IjkMediaPlayer.OPT_CATEGORY_FORMAT, "rtsp_transport", "tcp");
player.setOption(IjkMediaPlayer.OPT_CATEGORY_FORMAT, "timeout", 20_000_000L);
```

如果需要 UDP 首选、收不到包时回退 TCP，应允许两种 transport 并设置有限 timeout。实测：服务器接受 UDP SETUP 却不发 RTP 时，200000 微秒设置会触发 TCP 回退并收到有效包；零 timeout 保持等待，直到用户中断。底层轮询有时间粒度，timeout 不是严格的总耗时 deadline。

有限 timeout 的握手/读包错误和用户中断现在保留原始返回码。IJK 对明确的 RTSP I/O 致命错误（如超时、连接重置）停止重复读包，走原有音视频尾部排空流程，再发一次带原始错误码的错误事件；EAGAIN 和损坏 RTP 数据保持可恢复。成功 seek 或新会话清除锁存错误。正常 EOF 仍按原有结束语义处理。

## DNS 与运行中断线

- 新一次 open 会重新解析域名；解析结果的地址列表中，失败地址之后的有效地址可以连接
- DNS 改变不会把已有 TCP 会话搬到新 IP；显式重新打开后才使用新解析结果
- 本次没有增加自动重连、DNS 轮询或重试次数策略
- 系统同步 getaddrinfo 期间无法由 FFmpeg 的普通中断回调抢占。进程内注入 500ms 解析延迟时，150ms 的调用方中断 deadline 仍需等待解析返回。字面 IP 对照不受该延迟影响

## 验证边界

回归覆盖：直接播放、同 IP 不同端口、不同回环 IP、不同路径、OPTIONS/DESCRIBE/SETUP/初次 PLAY 302、部分 SETUP 后跳转、混合跳转链、8/9 跳边界、环路、中断后重新打开、认证/Session 状态、旧点播 SDP → 直播及反向切换、UDP → TCP 回退、DNS 下一次 open、活动连接断开。

这是初始化阶段跳转修复，不包括播放中服务端 REDIRECT 请求、暂停后再次 PLAY/seek 的重定向，或 initial_pause 后的延迟 PLAY 跳转。主机测试使用准确源代码构建的 FFmpeg，以及从实际 IJK 控制流提取的 C 回归；它们不能替代 Android MediaCodec/AudioTrack、真实网络、TLS 和实际设备上的验收。
