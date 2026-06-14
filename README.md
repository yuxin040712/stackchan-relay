# stackchan-relay

单进程 FastAPI 服务,合并了:

- **MCP server** (Streamable HTTP),挂载在 `/mcp/<TOKEN>/`,给 Claude.ai 的 MCP 连接器用
- **Relay**,给 ESP32 (M5Stack CoreS3) 长轮询用:
  - `GET /poll?token=<TOKEN>` — 拉取最新命令(latest-only,最多等 `STACKCHAN_POLL_TIMEOUT` 秒)
  - `POST /result?token=<TOKEN>` — 上报上一条命令的执行结果

命令队列存在内存里,只保留"最新一条未消费的命令",不做持久化、不做FIFO。

## 提供的 MCP 工具

- `stackchan_speak(text)`
- `stackchan_emote(expression)`
- `stackchan_move_head(pitch, yaw)`
- `stackchan_wiggle()`
- `get_stackchan_status()` — 返回 ESP32 是否在线、最近一条命令、最近一次上报结果

## 本地开发

```bash
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
cp .env.example .env   # 修改 STACKCHAN_TOKEN
.venv/bin/uvicorn app.main:app --host 127.0.0.1 --port 8011 --reload
```

## 部署到 VPS (43.129.25.76, Ubuntu 24.04)

1. 上传代码到 `/opt/stackchan-relay`(例如 `git clone` 或 `scp -r`)

2. 建虚拟环境并装依赖:

   ```bash
   cd /opt/stackchan-relay
   python3 -m venv .venv
   .venv/bin/pip install -r requirements.txt
   ```

3. 配置环境变量:

   ```bash
   cp .env.example .env
   # 生成一个随机 token:
   python3 -c "import secrets; print(secrets.token_urlsafe(24))"
   # 把生成的值填进 .env 的 STACKCHAN_TOKEN
   ```

4. 安装 systemd 服务:

   ```bash
   cp stackchan-relay.service /etc/systemd/system/
   systemctl daemon-reload
   systemctl enable --now stackchan-relay
   systemctl status stackchan-relay
   ```

5. 配置 Caddy 反代(参考 `Caddyfile.snippet`,追加到 `/etc/caddy/Caddyfile`):

   ```bash
   cat Caddyfile.snippet >> /etc/caddy/Caddyfile
   systemctl reload caddy
   ```

   这里用的是 `43-129-25-76.nip.io`(免费的 IP 通配 DNS,自动解析到你的VPS IP),Caddy 会自动申请 HTTPS 证书。以后换正式域名只需要把这一行换掉。

6. 验证:

   ```bash
   curl https://43-129-25-76.nip.io/healthz
   # => {"ok":true}
   ```

## 在 Claude.ai 里添加 MCP 连接器

- URL 填: `https://43-129-25-76.nip.io/mcp/<TOKEN>/` (注意结尾的斜杠)
- 保存后新开一个聊天窗口,Claude 就能看到 5 个 StackChan 工具

## ESP32 (CoreS3) 固件需要做的事

循环长轮询:

```
GET https://43-129-25-76.nip.io/poll?token=<TOKEN>
```

- 最多等待 ~25 秒(由服务端 `STACKCHAN_POLL_TIMEOUT` 控制)
- 响应示例:
  - 有命令: `{"id":"cmd_000001","action":"speak","text":"你好"}`
  - 没命令(超时): `{"action":null}`
- `action` 取值: `speak`(带 `text`)、`emote`(带 `expression`)、`move_head`(带 `pitch`/`yaw`)、`wiggle`
- 执行完后上报结果(可选但建议):

  ```
  POST https://43-129-25-76.nip.io/result?token=<TOKEN>
  Content-Type: application/json

  {"id":"cmd_000001","status":"ok"}
  ```

## 安全注意点

- 服务只监听 `127.0.0.1:8011`,外部只能通过 Caddy 的 443 访问
- `STACKCHAN_TOKEN` 同时用作 MCP 路径的一部分和 ESP32 接口的 `?token=`,务必用一个长随机字符串,不要用默认值
- `.env` 不要提交到 git
