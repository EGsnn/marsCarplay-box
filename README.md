# marsCarplay-box

在正点原子 ATK-DNESP32S3-BOX 上实验 **iPhone USB-C → CarPlay 接收 → H.264 软件解码 → 板载屏幕与触摸**。运行时画面由 ESP32-S3 解码和显示，电脑用于开发与可选诊断。

> 这是研究源码，不是即刷即用的 CarPlay 产品。公开默认固件使用 **无认证身份的 stub**，不能完成真实 iPhone 认证。仓库不提供或下载认证私钥、证书、成品固件和 Flash 备份；需要实际连接时自行集成有权使用的认证实现，见 [认证接口](code/firmware/components/auth_software_provider/AUTH_PROVIDER.md)。本项目不声称 Apple/MFi 认证。

## 发布范围

本仓库只包含 BOX **本地直显** 实现、必要共享协议组件、离线检查、版本记录和数值性能数据。**不包含 web CarPlay、浏览器预览/转发、WebRTC页面、网站/后台、其他板卡应用，也不带原工作区的Git历史。** 原作者和上游来源说明会提及相关公开项目；这些归属说明不代表收录其完整应用。详情见 [发布边界](PUBLISH_SCOPE.md)。

## 硬件与当前状态

| 项目 | 内容 |
|---|---|
| 板卡 | ATK-DNESP32S3-BOX，ESP32-S3，16MiB Flash / 8MiB Octal PSRAM |
| 屏幕 | ST7789 I80，320×240；CarPlay实验声明320×192，保持比例并留黑边 |
| 触控 | CHSC5432，触摸命令经USB回传；黑边不响应 |
| 解码 | 固定Sub0h264 generic C++，8bit YUV420，High/CABAC，逐行单slice I/P，最多4参考帧 |
| 能力声明 | maxFPS8实验；尚无真实iPhone服从此值的验收记录 |
| 未完成 | 流畅高帧率、音频播放、稳定性与广泛iOS兼容性 |

本地私有认证实验中，真实手机已出图且可触摸；0.1.6有新输入的采样区间约 **6.12FPS**，仍有缺口恢复。固定320×192测试样本的解码均值从48.47降至42.01ms；它不代表所有手机画面或端到端延迟。0.1.7的8FPS模拟100帧全部提交，但不是手机限帧验收。公开0.1.8仅改变源码打包、默认认证与可移植构建，尚未烧录验收。详见 [版本记录](versions/CHANGELOG.md)。

## 快速开始：无需身份的离线检查

需要Python3.10+及支持C++23的Clang。macOS先安装Xcode Command Line Tools；Ubuntu24.04可安装`clang`。

```sh
git clone https://github.com/EGsnn/marsCarplay-box.git
cd marsCarplay-box
python3 code/tools/audit_public.py
python3 code/tools/check_host.py
```

检查覆盖位流读取、YUV/RGB565、触摸、队列/配置去重、统计与阶段计时。不会连接手机、烧录设备或要求私人视频。可选真实样本分析使用`--sample /absolute/path/sample-annex-b.h264`；该回归夹具要求已有124帧800×480样本，仓库不附该私人文件。可选加密模拟发送器需`pip install -r requirements-dev.txt`，它是测试对端，不是iPhone/iAP2认证模拟器。

## 无身份固件构建

本项目固定ESP-IDF **v6.1**（提交见`code/firmware/upstream-pins.json`），需要该版本安装的ESP32-S3工具链，以及PATH中的CMake、Ninja（macOS可用`brew install cmake ninja`，Ubuntu可用`sudo apt install cmake ninja-build`）。先按Espressif的ESP-IDF安装方式安装并激活环境；示例：

```sh
git clone --branch v6.1 --recursive https://github.com/espressif/esp-idf.git /your/path/esp-idf
cd /your/path/esp-idf
./install.sh esp32s3
. ./export.sh
cd /your/path/marsCarplay-box
python3 code/prepare_local.py --fetch-deps
python3 code/build.py
```

`IDF_PATH`来自已激活的环境，也可传`--idf-path`；不依赖原作者工作区或固定Python目录。准备脚本只下载固定esp-usb/TinyUSB源码，并在忽略的`local/settings.json`生成随机诊断热点密码；无认证输入。产物在`build/firmware/atk_box_carplay.bin`。该默认构建用于驱动、显示/网络诊断，**不能对真实iPhone认证**。构建脚本不烧录设备。

外部认证实现通过`python3 code/build.py --auth-provider /absolute/path/provider.c`接入。调用者负责该实现、资产与使用权限；不要提交私钥或含身份的二进制。

## 目录与开发

```text
code/          BOX固件、构建/诊断工具与原生测试
versions/      每版修改、构建、烧录和实物验收边界
references/    固定依赖、来源、补丁摘要及数值性能数据
LICENSES/      继承的许可文本
local/ build/  忽略：本地设置、依赖和构建产物
```

- 参考帧按序解码；缺口后重新同步，不能任意跳过P帧。单任务持有解码器及LCD，DMA完成后复用缓冲。
- LCD不显示周期性诊断数据；同一会话恢复时保留上一张完整画面。HTTP仅提供JSON状态和SPS/PPS诊断，没有浏览器视频预览。
- 涉及实机更新前，请自行核对板型/芯片/分区、备份并确认恢复方式。当前脚本只构建，不默认写设备。
- 欢迎提交性能分析、解码正确性、可复现问题及文档改进；不要上传凭据、个人视频或Flash镜像。

## 许可证与来源

组合项目按 **GPL-3.0-or-later** 发布，见[LICENSE](LICENSE)。保留继承代码的MIT、zlib、Apache-2.0与其他原始许可/版权，见[NOTICE](NOTICE.md)、[三方说明](code/firmware/THIRD_PARTY_NOTICES.md)和组件内文件。许可证不授予第三方身份资产、商标或认证资格。
