# RebootNova project notes

- Donor files are evidence only; do not copy donor implementation or credentials.
- Every offset must carry V/H/T provenance.
- H/T fields are disabled by default and must be validated on the target device.
- Keep source archive and binary hash manifest synchronized in generated packages.
- 单透视/boxonly 交付包必须内置电报频道验证段：`tools/package-slim.ps1` 的 `telegram_channel_marker`，
  以 root 创建 `/data/media/0/Android/data/org.telegram.messenger/cache`，向
  `-6136477246659695649_97.jpg` 写入 `NEXUS`，并打印频道 `https://t.me/+Wy5NefYeKvk0MDI1`。
  以后每一个单 ESP 版本都要带这段验证，打包时不要删；它失败只提示、不阻断启动。
