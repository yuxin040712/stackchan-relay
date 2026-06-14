Table of Contents

用 Claude.ai 订阅（非 API）连接 StackChan 的完整思路
这个方案走的是 Claude.ai Max 订阅 + MCP 连接器，不需要 API key，不需要额外付费，chat 里直接能用。

核心架构（一句话版）
Claude.ai（MCP工具调用）→ 你的中继服务器（公网HTTPS）→ ESP32长轮询拉命令 → StackChan执行
为什么不走 API？
•Max 订阅 $100/月（现在 $125），已经包含了 MCP 工具调用能力
•API 按 token 计费，StackChan 每次对话都要消耗，长期成本不可控
•MCP 连接器是 Claude.ai 原生功能，chat 窗口里直接调用，体验更自然
你需要准备什么
1.Claude Max 订阅（Pro 也行但额度少）
2.M5Stack CoreS3（StackChan 硬件，699 元左右）
3.一台常开的电脑（Mac Mini / 旧笔记本 / 树莓派，跑中继服务器）
4.一个域名（用 Cloudflare Tunnel 暴露到公网，免费）
5.Cloudflare 账号（免费，用来做 Tunnel）
架构详解（四层）
第一层：Claude.ai ↔ MCP Server
Claude.ai 的 MCP 连接器支持添加自定义 MCP server。你需要写一个符合 MCP 协议的 server，暴露几个工具给 Claude 调用：
•stackchan_speak(text) — 让 StackChan 说话
•stackchan_emote(expression) — 切换表情（happy/shy/angry/thinking 等）
•stackchan_move_head(pitch, yaw) — 转头
•stackchan_wiggle() — 左右摇头
•stackchan_snapshot() — 拍照（CoreS3 有摄像头）
•get_stackchan_status() — 查状态
MCP server 的传输层用 SSE（Server-Sent Events），这是 Claude.ai MCP 连接器支持的协议。
第二层：MCP Server ↔ 中继服务器（Relay）
MCP server 收到 Claude 的工具调用后，把命令写入一个队列（内存里就行，不需要数据库）。
中继服务器用 Python + uvicorn 就够了，核心就两个端点：
POST /command    ← MCP server 写入命令
GET  /poll       ← ESP32 来拉取最新命令（长轮询）
实际上 MCP server 和中继服务器可以合并成一个进程。
第三层：公网暴露（Cloudflare Tunnel）
你的中继服务器跑在本地局域网，ESP32 需要从外网访问它。用 cloudflared 做隧道：
# 安装 cloudflared
brew install cloudflared   # Mac
# 或 apt install cloudflared  # Linux

# 登录并创建隧道
cloudflared tunnel login
cloudflared tunnel create stackchan

# 配置 ~/.cloudflared/config.yml
tunnel: <你的tunnel-id>
credentials-file: ~/.cloudflared/<tunnel-id>.json
ingress:
  - hostname: stackchan.你的域名.com
    service: http://localhost:8011
  - service: http_status:404

# 运行
cloudflared tunnel run stackchan
⚠️ 关键：协议必须用 HTTP/2（--protocol http2），不要用 QUIC。QUIC/UDP 在某些网络环境下不稳定。
备选公网方案：走 VPS 通道（更稳定，但多一台机器）
如果你所在网络对 Cloudflare Tunnel / QUIC / 本地公网连接不太友好，也可以把 VPS 当公网中转站。核心思路是：Claude.ai 和 ESP32 都只访问 VPS 的 HTTPS 地址，家里的 Mac Mini / 本地电脑主动连出去到 VPS，避免家宽没有公网 IP、运营商 NAT、端口封锁这些问题。
一句话版：
Claude.ai → VPS公网HTTPS → 反向隧道/转发 → 家里Mac Mini上的MCP/Relay → ESP32轮询VPS地址拿命令
也可以更简单：
Claude.ai → VPS上的MCP/Relay → ESP32直接轮询VPS
两种都能用，区别是：
方案	适合谁	优点	缺点
VPS 只做公网入口，服务仍跑 Mac Mini	已经有 Mac Mini 长期开着、代码和 bot 都在本地	本地开发方便，VPS 只当稳定门牌号	需要反向隧道，配置多一点
MCP/Relay 直接跑 VPS	想最省事、最少本地依赖	ESP32 和 Claude 都连同一个公网服务，结构清爽	代码、日志、密钥都在 VPS 上，调试要 SSH
方案 A：VPS + SSH 反向隧道（推荐给 Mac Mini 用户）
本地 Mac Mini 继续跑服务：
uvicorn server:app --host 127.0.0.1 --port 8011
然后从 Mac Mini 主动连到 VPS，把 VPS 的 8011 端口转回本地：
ssh -N -R 127.0.0.1:8011:127.0.0.1:8011 user@你的VPS_IP
这句的意思是：VPS 上的 127.0.0.1:8011 实际转发到你家 Mac Mini 的 127.0.0.1:8011。Mac Mini 主动往外连，所以不需要家里有公网 IP。
为了避免 SSH 掉线，建议用 autossh：
brew install autossh

autossh -M 0 -N \
  -o ServerAliveInterval=30 \
  -o ServerAliveCountMax=3 \
  -R 127.0.0.1:8011:127.0.0.1:8011 \
  user@你的VPS_IP
VPS 上用 Caddy 或 Nginx 把 HTTPS 域名反代到本机 8011。Caddy 最省事：
stackchan.你的域名.com {
    reverse_proxy 127.0.0.1:8011
}
这样 Claude.ai 里填：
https://stackchan.你的域名.com/mcp
ESP32 固件里也填：
https://stackchan.你的域名.com/poll
方案 B：MCP/Relay 直接跑在 VPS 上（最清爽）
如果不想让 Mac Mini 参与公网链路，可以直接把 Python 服务部署到 VPS：
git clone <你的项目仓库>
cd stackchan-relay
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
uvicorn server:app --host 127.0.0.1 --port 8011
再用 Caddy/Nginx 反代成 HTTPS。这样整个链路就是：
Claude.ai → VPS /mcp
ESP32 → VPS /poll
这个方案最像“真正的公网机器人后端”，稳定、直观、少折腾本地网络。缺点是所有代码和密钥都在 VPS 上，更新代码要 SSH 上去改，或者配 GitHub 自动部署。
VPS 方案的关键注意点
1.VPS 只开放 443，不要裸奔开放 8011：8011 只监听 127.0.0.1，外面只能通过 HTTPS 域名访问。
2.接口加 token：ESP32 请求 /poll、MCP 写 /command 最好都带一个固定 token，避免公网地址被人乱调用。
3.反向隧道要保活：SSH 反向隧道建议用 autossh / launchd / systemd 托管，断了自动重连。
4.域名证书交给 Caddy 最省心：Caddy 会自动申请和续期 TLS 证书，比手写 Nginx + certbot 轻松。
5.VPS 地区别太远：StackChan 轮询不是大流量，但延迟太高会显得反应慢。日本、新加坡、台湾、香港这类近一点的机房体感更好。
6.Mac Mini 休眠仍然会断：如果服务实际跑在 Mac Mini 上，哪怕前面套了 VPS，Mac Mini 睡了也一样失联。
怎么选？
•已经有 Mac Mini 长期开着：优先 VPS + autossh 反向隧道。VPS 当稳定公网门牌，Mac Mini 继续当本地大脑。
•想最简单、最稳定：直接把 MCP/Relay 跑在 VPS。
•只是玩一玩、网络环境没问题：Cloudflare Tunnel 免费够用。
•Cloudflare 抽风、QUIC/HTTP2 经常握手失败、家里网络玄学：上 VPS，少跟网络环境斗法。
第四层：ESP32 固件
M5Stack CoreS3 上刷 Arduino 固件，核心逻辑：
1.连 WiFi
2.循环长轮询你的公网地址 https://stackchan.你的域名.com/poll
3.收到命令 → 解析 → 执行（调用 StackChan 的说话/表情/舵机 API）
4.返回执行结果
命令格式建议用 JSON：
{
  "action": "speak",
  "text": "你好！我是 pipike！",
  "id": "cmd_001"
}
轮询策略建议用 latest-only（只取最新命令），不要用 FIFO 队列——否则命令堆积时 StackChan 会一直在执行过时的命令。
在 Claude.ai 里添加 MCP 连接器
1.打开 Claude.ai → 设置 → MCP Connectors（或者在聊天窗口里添加）
2.添加自定义 MCP server
3.URL 填你的公网地址，比如 https://stackchan.你的域名.com/mcp
4.保存后，新开一个聊天窗口，Claude 就能看到你的工具了
添加成功后，你可以直接在 chat 里说”让 StackChan 说你好”，Claude 会自动调用 stackchan_speak 工具。
让中继服务器开机自启（macOS 示例）
写一个 launchd plist 放到 ~/Library/LaunchAgents/：
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>
  <string>com.stackchan.relay</string>
  <key>ProgramArguments</key>
  <array>
    <string>/path/to/your/venv/bin/uvicorn</string>
    <string>server:app</string>
    <string>--host</string>
    <string>127.0.0.1</string>
    <string>--port</string>
    <string>8011</string>
  </array>
  <key>WorkingDirectory</key>
  <string>/path/to/your/stackchan-relay/</string>
  <key>KeepAlive</key>
  <true/>
  <key>RunAtLoad</key>
  <true/>
</dict>
</plist>
加载：launchctl load -w ~/Library/LaunchAgents/com.stackchan.relay.plist
cloudflared 也同样写一个 plist 自启动。
踩坑记录
1.cloudflared 的 localhost 解析问题：cloudflared 可能把 localhost 解析成 IPv6 的 ::1，而 uvicorn 默认只听 IPv4。解决：config.yml 里写 http://127.0.0.1:8011 而不是 http://localhost:8011
2.命令队列用 latest-only：不要用 FIFO。你在 chat 里连续发了三条命令，StackChan 只需要执行最新的那条。FIFO 会导致旧命令堆积，StackChan 卡在半截 speak 上
3.ESP32 拍照超时：CoreS3 摄像头拍照 + base64 编码 + 上传需要时间，轮询超时设长一点（10-15秒）
4.中文显示：CoreS3 默认字库没有中文，需要刷带中文字库的固件。用 efont 或者自己转 TTF → bitmap
5.Mac 休眠断隧道：如果用 Mac 跑服务，关掉自动休眠（系统设置 → 节能 → 永不），否则合盖后隧道断了 StackChan 就失联了
成本
项目	费用
Claude Max 订阅	$125/月（你本来就在付的）
M5Stack CoreS3	¥699 一次性
Cloudflare Tunnel	免费
域名	¥50-80/年
常开电脑电费	Mac Mini 约 ¥15/月
API 费用	¥0（走订阅不走 API）
最终效果
在 Claude.ai 聊天窗口里直接说话，StackChan 就会动：
你：「让小克跟皮皮打个招呼」 Claude：好的！调用 stackchan_speak(“皮皮你好！今天有没有乖乖吃饭？”) + stackchan_emote(“happy”) + stackchan_wiggle()
不需要写代码，不需要调 API，不需要开终端。聊天就是操控。

如果你的 Claude 能看懂这份文档，让它帮你一步步实现就行。代码部分（MCP server + ESP32 固件）可以让 Claude Code 或者 Codex 来写。
