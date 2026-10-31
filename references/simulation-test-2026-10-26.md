# 模拟发送端与测试结果（2026-10-26）

## 已执行：主机回环生产代码测试

独立 Python 发送器与本项目实际 C/C++ 接收代码通过本机 IPv6 TCP 连接。使用已验证的真实 CarPlay H.264 样本（不随仓库分发），800×480 High / CABAC / 4 参考帧；样本摘要 `34013f07a777fdafb2b928211233e1e14d25416a89e9aa7bf6c79206fe0fcf86`。

- SRP-6a Pair-Setup、真实 Ed25519 签名验证、X25519 Pair-Verify 通过。
- 加密 RTSP 初始 SETUP、RECORD、模式通知、视频 SETUP 和 TEARDOWN 均返回 200。
- Event 通道发送 forceKeyFrame，发送器正确加密应答，并从 IDR 重新开始源序列。
- 请求发送节奏 30 FPS、持续约 10 秒；发送 **300** 帧，实际生产接收路径解密 **300** 帧、解码及 RGB 转换 **300** 帧，错误 **0**。
- 发送器单元检查通过：TLV 分片/截断、多 AEAD 帧拼接、读取超时后保留半包、拒绝损坏认证标签。

实际复用的接收代码：`airplay_rtsp_session` / pairing / SRP / Ed25519 / X25519 / HKDF / `airplay_screen_stream` / Annex-B 转换 / `box_decoder` / `box_pixels`。

## 必须保留的边界

**这是主机接收夹具测试，不是 ESP32-S3 性能测试。** 30 FPS 是主机发包目标；300 帧处理成功不能证明 BOX 可达 30 FPS。替代部分包括 POSIX 传输适配、内存分配、屏幕输出、事件生命周期及计时端口。没有覆盖 iPhone/iAP2/MFi、FreeRTOS 排程、BOX 压缩帧队列、真实屏幕颜色或触摸。

主机夹具结果仅代表上述覆盖范围。随后已完成下文的物理 BOX 测试，两组结果分别记录。

## 已执行：物理 BOX 加密视频发送

用户释放 BOOT 并实体复位后，USB 枚举为 `ATK BOX CarPlay / ATK-BOX-0001`，NCM 接口 en9。状态接口确认 `0.1.2-atk-psram80`、显示任务就绪、RTSP 监听和编译配置 PSRAM 80 MHz / CPU 240 MHz。未重新烧录固件。

Mac 独立发送器经过真实配对、加密控制/事件/视频通道，通过 USB NCM 向实际 ESP32-S3 发送同一真实样本。解码与 LCD 提交均在 BOX 上完成。该模拟绕过 iPhone/iAP2/MFi，不是完整手机互通验收；LCD 成功提交计数也不能代替目视检查。

| 测项 | 高负载 | 低负载对照 |
|---|---:|---:|
| 目标发送 FPS | 30 | 1 |
| 实际发送 FPS | 22.07 | 1.00 |
| 发送时间 | 30.00 s | 20.00 s |
| 停止发送后保留会话 | 3 s | 3 s |
| 发送 / 板端收到 | 662 / 662 | 20 / 20 |
| 解码 / LCD 成功提交 | 15 / 15 | 20 / 20 |
| 实际序列缺口 gaps | 15 | 0 |
| 解码 / LCD 错误计数增量 | 0 | 0 |
| 解码器重建次数（含会话初始化） | 16 | 1 |
| 等待 IDR 跳过帧 | 492 | 0 |
| forceKeyFrame 请求 | 17 | 1 |
| 解码墙钟平均，扣除显示回调 | 约 1309 ms | 约 518 ms |
| 颜色转换 / 缩放平均 | 约 50 ms | 约 23 ms |
| LCD 提交平均 | 约 17 ms | 约 16 ms |

上述帧数是开始发送前到收尾后的计数差，避免混入同次启动的前一轮结果。耗时平均由前后累计尝试数与已舍入的累计均值差分推算，存在小量舍入误差；解码仍包含解析、调度与协作等待，不是纯 CPU 或端到端耗时。高负载按完整 33.01 秒观测区间计算为 0.454 FPS；低负载按含收尾的 23.01 秒为 0.869 FPS，不能把收尾等待也算作持续输入。

高负载只达到约 22 FPS，说明发送/传输侧仍有限制，不能将其称为已达成稳定 30 FPS 的压测。即便如此，输入速率仍远高于显示处理能力；较慢解码使有界队列出现缺口，恢复流程反复请求 IDR，增加了等待和重复关键帧负载。低负载 20 帧全部完成且没有缺口，支持“解码跟不上并触发恢复”的判断。当前证据不支持先以 LCD 加速或补帧解决主要瓶颈。

下一步优先尝试让手机输出接近屏幕尺寸的视频，并实测手机是否接受该尺寸/能力声明；然后针对解码热区、内存访问和可支持的编码特征优化。不能把只缩小最终 RGB 画布当作减少 H.264 解码量，也不能直接跳过依赖 P 帧。两组源内容不同（高负载反复回到 IDR），不能将耗时差全部归因于频率或传输。

发送器本次修复 Darwin IPv6 UDP scope_id 丢失；增加真实接口作用域解析回归检查。诊断采样独立线程执行，避免 HTTP 查询阻塞视频定时；记录实际发送 FPS，发送结束后等待 3 秒再取最终状态并 TEARDOWN。四项单元检查与修改后的生产代码回环 300 帧回归均通过。

可公开数值摘要：[simulation-device-2026-10-26.json](simulation-device-2026-10-26.json)。完整 `report.json`、逐秒状态和日志保存在忽略的 `build/simulation-device/20261026-run-4-paced30/`、`20261026-run-5-paced1/`；私人 H.264 不入库。初次高负载测试（run-2）存在同步 HTTP 采样干扰，run-3 未等待末帧收尾，最终以上述 run-4/run-5 为准。

## 复现（当前工作区）

需 Python cryptography、本机 clang/C++23、匹配 IDF 的 TF-PSA-Crypto 头文件与原生静态库。已有 IDF Python 环境含 cryptography；主机 crypto 库由本工作区先前密码学测试准备，可通过 `--crypto-lib` 指定其他匹配产物。

```sh
python3 atk-box-carplay/code/tools/build_simulation_receiver.py
.esp-idf-tools/python_env/idf6.1_py3.14_env/bin/python \
  atk-box-carplay/code/tests/test_simulation_sender.py
.esp-idf-tools/python_env/idf6.1_py3.14_env/bin/python \
  atk-box-carplay/code/tools/check_simulation.py --sample /absolute/path/to/sample-annex-b.h264
```

夹具只监听本机 IPv6 回环，测试身份随机生成、仅保存在进程内存。测试日志、动态库与 JSON 在忽略的 `build/simulation-host/`，不会上传私人视频。

## 复现物理 BOX 发送

保持 BOX USB 接 Mac，核对新出现的 USB NCM 接口名称及状态版本。接口名称可能不是 en9；不能把另一个同类设备当作目标。使用对应接口的 USB IPv6，例如：

```sh
.esp-idf-tools/python_env/idf6.1_py3.14_env/bin/python \
  atk-box-carplay/code/tools/simulate_phone.py \
  --host 'fe80::2:11ff:fe22:3302%en9' \
  --sample /absolute/path/to/sample-annex-b.h264 --fps 30 --seconds 30 \
  --output atk-box-carplay/build/simulation-device/run-1
```

这是显式测试发送器，正常 CarPlay 仍由 iPhone USB 直连 BOX；不增加电脑运行时依赖。正式板端结果必须核对 `report.json`、板上 `local_display.performance`、gaps / decoder_resets / waiting_idr，并与主机结果分开记录。
