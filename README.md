# ESP32-S3 2.0 寸 ST7789V2 屏幕

这个项目已经实测跑通：ESP32-S3 控制 2.0 寸 SPI TFT 屏幕，屏幕控制器是资料包参考代码里的 `ST7789V2`，物理分辨率 `240x320`，当前程序用横屏逻辑坐标 `320x240` 绘制。

当前 `esp32.ino` 不再使用 `TFT_eSPI`，而是直接用 ESP32 硬件 SPI 发送 ST7789V2 初始化命令。这样最接近资料包里的 Arduino 参考代码，也解决了之前“只有背光，没有图像”的问题。

## 已验证结果

- 背光正常点亮。
- 屏幕能显示彩色内容。
- 当前程序上电后显示参考图风格的横屏像素小猫 UI：粉色背景、粗黑边框、`12:00`-`12:07` 时间、电池图标和底部探头小猫。
- 包含 8 种表情：默认、开心、撒娇、生气、困了、饿了、害羞、晕晕。
- 触摸 `TTP223` 模块会依次切换表情。
- 打开 `wiring.html` 可以用 Web Serial 虚拟按钮向 ESP32 发送 `0`-`7`，直接切换对应表情。
- ESP32-S3-N16R8 内置 Wi-Fi；当前固件会创建 Wi-Fi 热点 `PixelCat-S3`，支持网页控制和网页 OTA 更新。
- 接上 HW-390 电容式土壤传感器，土壤湿润时屏幕叠加蓝色雨滴动画。
- 接上 SHT30 温湿度传感器后，屏幕顶部会显示温度和湿度。
- 接上 SU-03T 语音模块和喇叭后，网页可以让设备播报，也能接收 SU-03T 识别到的语音命令。
- 上传需要用低速 `115200`，高速上传时这块板子可能会中途断开。

## 无线控制

### Wi-Fi 热点控制

ESP32 上电后会创建自己的热点，不需要家里路由器：

```text
SSID: PixelCat-S3
密码: cat12345
地址: http://192.168.4.1/
```

手机或电脑连上这个热点后，浏览器打开 `http://192.168.4.1/`，页面上会有 8 个表情按钮。也可以直接请求：

```text
http://192.168.4.1/set?id=0
http://192.168.4.1/set?id=7
http://192.168.4.1/speak
http://192.168.4.1/speak?id=1
http://192.168.4.1/voice?cmd=HELLO
```

`/speak` 会把当前表情对应命令发给 SU-03T；`/speak?id=1` 会发送指定表情命令。`/voice?cmd=HELLO` 会把自定义命令透传给 SU-03T。

### OTA 远程烧录

第一次必须用 USB 线烧录带 OTA 功能的固件；之后可以不直连电脑，通过 Wi-Fi 热点网页上传 `.bin`：

```sh
npm run bin
```

编译导出的 `.bin` 会在项目的 `build` 目录里。手机或电脑连接 `PixelCat-S3` 热点后，打开：

```text
http://192.168.4.1/update
```

选择 `build/esp32.esp32.esp32s3/esp32.ino.bin` 上传，完成后 ESP32 会自动重启。

## 接线

按屏幕和 TTP223 模块丝印接线，不要只相信线色。如果你的线色和下面不一样，以排针文字为准。

### 屏幕

| 屏幕引脚 | 作用 | ESP32-S3 |
| --- | --- | --- |
| `BL` / `LED` | 背光 | `GPIO5` |
| `CS` | SPI 片选 | `GPIO10` |
| `DC` | 命令/数据选择 | `GPIO11` |
| `RST` | 屏幕复位 | `GPIO8` |
| `SDA` | SPI 数据，也叫 `MOSI` | `GPIO12` |
| `SCL` | SPI 时钟，也叫 `SCLK` | `GPIO13` |
| `VCC` | 电源 | `3V3` |
| `GND` | 地 | `GND` |

注意：

- `SDA` 不是 I2C 的 SDA，这里是 SPI 数据线。
- `SCL` 不是 I2C 的 SCL，这里是 SPI 时钟线。
- `VCC` 先接 `3V3`，不要直接接 `5V`。
- 断电接线，检查无误后再插 USB。

### TTP223 触摸模块

TTP223 模块通常只有 3 个针脚：`VCC`、`GND`、`OUT` / `SIG` / `I/O`。

| TTP223 引脚 | ESP32-S3 |
| --- | --- |
| `VCC` | `3V3` |
| `GND` | `GND` |
| `OUT` / `SIG` / `I/O` | `GPIO4` |

不需要面包板也可以，三根杜邦线直接连就能用。用面包板会更稳，尤其是同时接屏幕和触摸模块时，建议把 `3V3` 和 `GND` 先接到面包板电源轨，再从电源轨分给屏幕和 TTP223。

注意：

- TTP223 和屏幕必须共地，也就是都要接到 ESP32-S3 的 `GND`。
- 代码按常见 TTP223 默认模式写：未触摸为低电平，触摸为高电平。
- 如果你的模块焊了模式跳线导致逻辑反过来，把 `esp32.ino` 里的 `digitalRead(TTP223_PIN) == HIGH` 改成 `== LOW`。

### HW-390 电容式土壤传感器

板子底部丝印只有 3 个：`GND` / `VCC` / `AOUT`。

| 传感器引脚 | ESP32-S3 |
| --- | --- |
| `GND` | `GND` |
| `VCC` | `3V3`（**不要接 5V**，ESP32 ADC 上限约 3.1V）|
| `AOUT` | `GPIO2`（ADC1_CH1）|

注意：

- 选 `GPIO2` 是因为它是 ADC1 通道，且不是 strapping 引脚。`GPIO0/3/45/46` 是 strapping，避开。
- 电容式传感器输出**反向**：越湿，ADC 越低。代码默认阈值 `SOIL_WET_THRESHOLD = 2400`，低于这个值算湿润。
- **第一次接好后必须用串口校准**。在串口监视器里看 `[soil] adc=XXXX wet=0/1`，记录空气、湿土、干土三种状态的值，把阈值设在湿土和干土之间。

```sh
npm run monitor
```

典型读数（仅供参考，每根传感器都不一样）：

| 状态 | ADC 值 |
| --- | --- |
| 空气中 | 约 3000 |
| 干土 | 约 2700 |
| 湿土 | 约 2100 |
| 泡水里 | 约 1700 |

判定为湿后，屏幕会在 UI 之下、猫之上叠加蓝色雨滴动画，每 ~180ms 更新一帧。

### SHT30 温湿度传感器

SHT30 是 I2C 模块。当前固件使用 `GPIO6/GPIO7`，避免和 TFT 屏幕的 `GPIO8` 复位脚冲突。

| SHT30 引脚 | ESP32-S3 |
| --- | --- |
| `VCC` | `3V3` |
| `GND` | `GND` |
| `SDA` | `GPIO6` |
| `SCL` | `GPIO7` |

注意：

- 模块必须和 ESP32 共地。
- 常见 I2C 地址是 `0x44`。
- 屏幕顶部中间会显示类似 `24.6C 58%`。如果没读到，会显示 `--.-C --%`。

### SU-03T 语音模块 + 喇叭

SU-03T 负责真正驱动喇叭。ESP32 只通过 UART 给它发送命令，也可以接收 SU-03T 识别到的命令。

| SU-03T 引脚 | 接到 |
| --- | --- |
| `VCC` | `3V3`（如果你的模块明确要求 5V，再接 `5V`） |
| `GND` | `GND` |
| `RXD` / `RX` | `GPIO17`（ESP32 TX） |
| `TXD` / `TX` | `GPIO18`（ESP32 RX） |
| `SPK+` / 喇叭+ | 喇叭红线 |
| `SPK-` / 喇叭- | 喇叭黑线 |

注意：

- TX/RX 要交叉接：SU-03T 的 `RX` 接 ESP32 的 `TX GPIO17`，SU-03T 的 `TX` 接 ESP32 的 `RX GPIO18`。
- 喇叭只接 SU-03T 的 `SPK+` / `SPK-`，不要接 ESP32 GPIO。
- 当前固件使用 `9600` 波特率。
- 网页点“让它说话”时会发送 `CAT_DEFAULT`、`CAT_HAPPY`、`CAT_COQUETTISH`、`CAT_ANGRY`、`CAT_SLEEPY`、`CAT_HUNGRY`、`CAT_SHY`、`CAT_DIZZY` 这些文本命令。
- SU-03T 需要在配套工具里配置串口命令和播报内容。如果你的命令名不同，改 `esp32.ino` 里的 `SU03T_EXPRESSION_COMMANDS`。
- SU-03T 发回 `0`-`7`、`NEXT`、`SPEAK` 或上面的 `CAT_*` 命令时，ESP32 会切换表情或触发播报。

### 有源蜂鸣器模块

`GPIO16` 上的有源蜂鸣器逻辑仍保留，作为 SU-03T 没配置好时的备用短音提示。

## 屏幕参数

来自资料包参考代码：

```text
控制器: ST7789V2
物理分辨率: 240x320
当前绘制: 320x240 横屏
接口: SPI
像素格式: RGB666 / 18-bit
每像素发送: 3 字节 R, G, B
SPI 模式: Mode 0
当前 SPI 频率: 10 MHz
```

关键初始化点：

- `0x11`: Sleep Out
- `0x36 = 0x60`: 横屏扫描方向
- `0x3A = 0x66`: RGB666 / 18-bit
- `0x21`: Display Inversion On
- `0x29`: Display On
- 写像素前发送 `0x2A`、`0x2B`、`0x2C`

## 编译

```sh
arduino-cli compile --fqbn esp32:esp32:esp32s3 .
```

## 查看串口

```sh
arduino-cli board list
```

当前这块板子通常显示为：

```text
/dev/cu.usbserial-1410
```

## 上传

推荐用低速上传：

```sh
arduino-cli upload -p /dev/cu.usbserial-1410 --fqbn esp32:esp32:esp32s3 --upload-property upload.speed=115200 .
```

如果端口变了，把 `/dev/cu.usbserial-1410` 换成 `arduino-cli board list` 看到的新端口。

## 排查记录

### 只有背光，没有图像

已经遇到过一次。结论：

- `BL -> GPIO5` 正常时，背光会亮。
- 背光亮但没图，不代表屏幕坏了。
- 当时 `TFT_eSPI` 初始化没有真正匹配这块 ST7789V2 屏，所以屏幕只亮背光。
- 改成裸 SPI，并按资料包发送 ST7789V2 初始化命令后，屏幕正常显示。

优先检查：

1. `SDA -> GPIO12`，`SCL -> GPIO13`，不要接反。
2. `DC -> GPIO11`，`RST -> GPIO8`，不要接反。
3. `CS -> GPIO10` 必须接好。
4. 代码里 `TFT_BL` 必须是 `5`。

### 上传失败

如果看到类似 `The chip stopped responding`，通常是上传波特率太高。用下面命令：

```sh
arduino-cli upload -p /dev/cu.usbserial-1410 --fqbn esp32:esp32:esp32s3 --upload-property upload.speed=115200 .
```

### 猫脸只剩描边没有五官

如果屏幕上只能看到猫的轮廓、时间和电池，里面是空白没有眼睛/嘴/腮红——这是 `catPixel` 绘制顺序写反了。

`catPixel` 按"先匹配先返回"工作，所以**视觉上在前面的图层必须在代码里先检查**。如果头部填充椭圆 (`return C_FUR`) 放在五官之前，整个脑袋范围内的像素会立刻返回奶油色，永远走不到画眼睛/嘴/腮红的代码；耳朵填充也会挡住耳朵粉色三角。

正确顺序（从前到后）：

1. 边框
2. UI（时间、电池）
3. 雨滴叠加
4. 猫描边
5. 脸颊外的胡须线
6. 眼睛、睫毛和高光
7. 鼻子和嘴
8. 腮红和头顶纹路
9. 耳朵粉色
10. 脸部白色填充
11. 头部和耳朵奶油色填充（兜底）
12. 背景

## 当前程序结构

- `initDisplay()`：按资料包发送 ST7789V2 初始化命令，并设置横屏扫描方向。
- `setAddressWindow()` / `writeCommand()` / `writeData()`：裸 SPI 操作底层。
- `catPixel(x, y, expression)`：核心函数，对每个像素按图层从前往后判断颜色，并用 2x2 取样做像素风边缘。
- `rainOverlayPixel(x, y, t)`：雨滴叠加层，在湿润时叠加在猫前面。
- `renderCat(expression)`：扫描整屏调用 `catPixel` 写入显存。
- `setupWifiControl()`：启动 Wi-Fi AP、HTTP 控制页和 OTA 上传页。
- `readSerialExpression()`：读取串口里的 `0`-`7` 或 `N`，用于网页虚拟按钮控制表情。
- `readTouched()`：读取并消抖 TTP223 触摸状态。
- `loop()`：500ms 读土壤、180ms 推进雨滴帧、检测触摸/串口/Wi-Fi 并按需重画。
