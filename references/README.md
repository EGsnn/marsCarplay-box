# 三方案例与来源

这里用于代码来源、固定版本、采用理由和本地补丁说明；完整上游检出在忽略的 `upstream/`，不会作为另一套运行程序。

## 采用：Sub0h264

- 原仓库：https://github.com/CraigHutchinson/Sub0h264
- 固定提交：`15421eeade48774929bc0221ea0418a25462c222`，MIT。原许可在代码 vendor 内保留。
- 上游主要面向 P4/桌面；本项目选择 generic C++ 路径，实际已通过 S3 Xtensa 编译，实际 S3 与手机性能记录见 versions/0.1.2–0.1.4。
- 原最大宽度为 640，本地改为 800；参考帧限制改为 4；新增 ESP32 协作调度、0.1.5可选抽样计时钩子及0.1.6位读取32位拼接（保持相同比特结果）。补丁及逐文件摘要见 `sub0h264-source.json`。
- 主机使用实际生产封装解完真实 124 帧，并走 RGB565 缩放；独立原内核输出与 PyAV 对照的亮度 PSNR 65.241 dB、最大误差 3。不是逐像素完全一致，不是 BOX FPS。

## 未采用为本版解码器

- [官方 esp_h264](https://github.com/espressif/esp-h264-component/blob/master/esp_h264/README.md)：官方当前软件解码功能表为 constrained baseline。本次真实源为 High / CABAC / 8×8 transform，不能直接据 Baseline 示例认定可用。
- [pschatzmann/TinyH264](https://github.com/pschatzmann/TinyH264)：声明 Baseline/CAVLC，不作为本次 High/CABAC 的已验证解码后端。

## 本地协议与 BSP 基线

- `base-source.json` 记录原 ESP-BOX-UI 的 `f714f495991def7e0664b8fd513d4c5429fdd5eb`、已验收微雪 0.8.2 协议和 BOX BSP 来源；本项目独立复制并修改，没有回写旧项目。
- `board-profile.json` 保存正点原子 ATK-DNESP32S3-BOX 的引脚、320×240 I80、16 MiB Flash / 8 MiB PSRAM 清单。BSP 冻结在代码组件中，不依赖运行 Mars 系统。
- USB/TinyUSB 依赖固定版本在 `code/firmware/upstream-pins.json`；本机依赖放 `local/deps/`。
- 各原组件的许可和第三方声明保留。实验认证资产的来源说明位于 `auth_software_provider/IDENTITY_NOTICE.md`；资产本身不入库。
