# PipBoy

[English](README.md)

![PipBoy 界面](docs/preview/overview.png)

跑在 S3AI Game（ESP32-S3）掌机上的绿色终端风格系统监视器。四个页面：时钟与状态、
只读文件浏览、Wi-Fi 与 BLE 扫描、以及带电池校准和按键检测的系统面板。

> **同人项目。** Fallout、Pip-Boy、Vault Boy 均为 Bethesda Softworks LLC 的商标。
> 本项目是非官方、非商业的个人固件，与 Bethesda 无任何隶属或背书关系。
> `assets/` 和 `research/user-frames/` 下的人物帧改编自 Vault Boy 美术，
> **不在**本仓库 MIT 许可的覆盖范围内——MIT 只适用于源代码。

## 页面

| | | |
|---|---|---|
| ![STAT](docs/preview/01-stat.png) | ![DATA](docs/preview/02-data.png) | ![RADIO](docs/preview/03-radio.png) |
| STAT | DATA | RADIO |
| ![SYS](docs/preview/04-sys.png) | ![WIFI](docs/preview/05-wifi.png) | ![DEVICE](docs/preview/06-device.png) |
| SYS | RADIO > WI-FI | SYS > DEVICE |

![人物动画](docs/preview/walk.gif)

预览图由 `preview_ui.py` 和 `preview.py` 用固件自己的字库和精灵帧、配示例数据渲染，
是布局检查，不是实机截图。

- **STAT** — 24 小时制北京时间点阵时钟、英文星期和日期、四帧人物、带估算百分比的
  电池图标、Wi-Fi 信号图标。未校时显示 `--:--` 和 `DATE NOT SYNCED`；电池读数无效时
  显示 `--% / CHECK`，不会假装满电。
- **DATA** — 四行文件列表，显示路径、选中位置和文件大小。不执行、不删除、不改写所
  浏览的文件。长名称截断，中文文件名暂不能正确渲染。
- **RADIO** — 用列表进入 WI-FI / BLUETOOTH LE。扫描结果不因切页中断。配网和扫描互斥。
- **SYS** — DEVICE、BATTERY、CLOCK、KEY TEST 四项。DEVICE 收纳 CPU、PSRAM、剩余堆
  内存、TF 卡和读写校验结果。

## 按键

| 操作 | 按键 |
|---|---|
| 关屏 / 恢复显示 | APP（GPIO10）短按 |
| 前后切页 | 左 / 右 |
| 跳到 SYS 页 | SELECT |
| 列表内移动 | 上 / 下 |
| 进入目录或子页 | A |
| 返回上一级 | B |
| TF 挂载失败后重试 | DATA 页按 A |
| TF 读写校验 | SYS > DEVICE 按 A |
| 重载电池校准 / 重新校时 | BATTERY / CLOCK 页按 A |
| 手机配网 | Wi-Fi 状态页按 X，或任意页按 START |
| 扫描附近网络 / BLE 广播 | Wi-Fi / BLE 页按 A |
| 扫描结果翻页 | X / Y |

按 A 进入子页本身不会触发扫描、校准或 TF 测试。没有组合键。

无操作 10 秒自动休眠。配网中、扫描中、文件操作中暂停倒计时，这些结束后重新计满 10 秒。

## 安装

镜像是**纯应用镜像**，用 Launcher 的 TF 卡文件浏览器安装。不能写到 Flash `0x0`，
也不要安装 `.pio` 里的 `firmware.factory.bin`、`partitions.bin` 或 `bootloader.bin`。

1. 把 `dist/PipBoy-v1.3.bin` 复制到 TF 卡任意位置。
2. 在 Launcher 文件浏览器里选中它，Launcher 会分配 Flash 应用分区并重启运行。

本项目的 `partitions.csv` 只服务编译，实际安装位置由 Launcher 决定。

**返回 Launcher 没有实现。** 本机 Launcher 位于 `test` 分区，现有引导程序没有启动
test 的快捷方式，所以复位可能仍然进入本应用。START 是配网快捷键，不是返回键。
本固件不会修改分区表。

## 配网和时钟

按 **START**，或在 Wi-Fi 状态页按 X。所有 S3AI 应用共用同一个热点：

| 名称 | 密码 |
|---|---|
| `samestick` | `samestick` |

手机连上后一般会自动弹出页面，没弹就访问 `http://192.168.4.1/`。选 **2.4 GHz**
网络——ESP32-S3 没有 5 GHz 射频。只有隐藏网络才需要手填名称。开放网络留空密码即可，
不支持企业认证。

只有连接成功才把凭据写入 NVS 的 `pipboy-net` 命名空间，所以填错密码不会覆盖原本能用的
配置，也不会改动 Launcher 自己的 Wi-Fi 设置。密码不会出现在串口、屏幕或网页上。
离线时每 30 秒重试。配网不依赖 TF 卡。

时间来自 NTP，固定 UTC+8。校时成功后即使断网或经过浅睡眠也继续走时。断电后时间未知，
显示 `--:--`；设备没有独立的断电走时 RTC，不会把上次保存的时间冒充当前时间。

配网由 [wifi-portal](https://github.com/sameclub/wifi-portal) 提供，以 submodule
形式引入。其页面样式改编自 [78/esp-wifi-connect](https://github.com/78/esp-wifi-connect)
（MIT），许可文本随 `lib/wifi-portal/LICENSE` 和 `dist/esp-wifi-connect-MIT.txt` 分发。

## 电池采样和校准

GPIO8，`analogReadMilliVolts`，12 位 ADC，6 dB 衰减，每两秒取 16 次平均并平滑显示：

```
电压 = ADC 毫伏 * 4 / 1000 * battery_scale
```

系数 4 来自上阻 300 / 下阻 100 的分压。超出 2.5–4.5 V 显示 `CHECK ADC`，不伪造百分比。

百分比按单节锂电池电压曲线估算，3.4 V 为 0%、4.2 V 为 100%，用 `~` 标识。实际容量
还受负载、温度和电池老化影响。没有充电检测 GPIO，`CHARGE: UNKNOWN` 就是字面意思。

校准方法：把 `pipboy.ini.example` 复制到 TF 卡根目录并改名 `pipboy.ini`。读数稳定后
用万用表量电池端电压：

```
新系数 = 旧系数 * 万用表电压 / 显示电压
```

把 `battery_scale` 设在 0.8–1.2 之间，在 SYS > BATTERY 页按 A 重载。默认 1.0，尚未
经过实机校准。如果需要超出这个范围的系数，先去查分压电路，不要硬补偿。

## 无线

RADIO > WI-FI 状态页显示连接状态、SSID、IPv4、RSSI、信道和本机 STA MAC。按 A 异步
扫描，X / Y 翻看 SSID、BSSID、信号、信道和开放/加密状态，B 返回。

BLE 默认不启动。按 A 做 5 秒被动广播扫描，显示名称（广播中提供时）、地址和 RSSI，
最多 12 条，超出标 `+`。这里显示的是正在广播的设备，不是已配对设备，不会建立连接。
ESP32-S3 不支持经典蓝牙。Wi-Fi 和 BLE 扫描串行执行。

## 存储

每次启动以 1-bit SDMMC 挂载 TF 卡，在根目录创建随机名 `/pipboy-check-xxxxxxxx.tmp`，
执行写入、关闭、重新打开、读回校验、删除。只删除本次创建的这个文件，重名则跳过。
失败会显示并记录日志，不会自动格式化，也不会静默切到 SPI。

## 显示与动画

正文使用 2 倍整数缩放点阵（12x16 像素字符单元），首页时间用同字体 5 倍缩放，页签用
11 像素字距以容纳 RADIO。每帧先在内存画布完成再逐行比较，只传输变化的行，不会先把
屏幕清黑。

人物使用四张 72x104 的 RGB565 帧，每 160 ms 换一帧，由 `prepare_user_sprites.py` 从
`research/user-frames/` 生成并编进 `src/walk_frames.h`（59904 字节）。色相统一映射到
界面的 `GREEN`（0x07ec），只保留原图的明暗和扫描线间隙。字体和精灵都打包在固件里，
不需要往 TF 卡拷图片。

## 构建

```bash
pio run -e s3ai-pipboy
python package.py
```

`pio run` 只生成 `.pio/build/s3ai-pipboy/firmware.bin`，`package.py` 才会校验并写入
`dist/`。固件和 `package.py` 的版本号必须一致，否则打包直接报错退出。

依赖全部从 PlatformIO registry 解析，板级定义已放进 `boards/`
（见 [boards/NOTICE.md](boards/NOTICE.md)），干净 clone 不需要任何其他仓库就能构建。

Windows 上首次构建前要先启用长路径，否则工具链解压会失败：

```powershell
New-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem" -Name LongPathsEnabled -Value 1 -PropertyType DWORD -Force
```

重新生成素材和预览图（需要 Pillow，并先跑一次 `pio run` 把字库拉下来）：

```bash
python -m pip install Pillow
python prepare_user_sprites.py   # 重建 src/walk_frames.h
python preview_ui.py             # 六张静态页 + overview
python preview.py                # walk.gif
```

重新生成精灵头文件后必须重新编译打包。

## 当前状态

固件能编译打包，esptool 确认为 ESP32-S3 镜像、checksum 和 validation hash 有效。
配网、时钟、电池、扫描**均未在实机上验证**，返回 Launcher 受引导程序限制无法实现。
上面这些数字是设计目标，不是实测结果。

## 相关项目

- [wifi-portal](https://github.com/sameclub/wifi-portal) — 共享配网库
- [DotMic](https://github.com/sameclub/dotmic) — 按住说话的 USB 麦克风 + 点阵时钟
- [VoxStick](https://github.com/sameclub/voxstick) — 本地 coding agent 的桌面终端

## 许可

源代码为 MIT，见 [LICENSE](LICENSE)。**不包括** `assets/` 和 `research/user-frames/`
下改编自 Vault Boy 的美术素材，以及由它编译而成的 `src/walk_frames.h` 和固件二进制中
的精灵数据。
