# 音乐播放器设计文档

## 项目概述

**项目名称**: VinylMusicBox
**项目类型**: 嵌入式 Linux 音乐播放器
**核心功能**: 通过蓝牙接收手机音频流，在圆形 OLED 屏幕上显示旋转唱片与同步歌词
**目标用户**: 音响开发板爱好者

---

## 硬件配置

| 配件 | 型号 | 预算 |
|------|------|------|
| 开发板 | Orange Pi Zero 3 (H618, 4核A53 1.5GHz, 1GB RAM) | ~$18 |
| 存储 | 32GB microSD 卡 | ~$8 |
| 显示屏 | GC9A01 1.28" 圆屏 (240×240, SPI) | ~$8 |
| 蓝牙适配器 | CSR 4.0 USB 蓝牙适配器 | ~$5 |
| **总计** | | **~$39** |

---

## 系统架构

```
┌─────────────────────────────────────────────────┐
│                 Orange Pi Zero 3                │
│                                                 │
│  ┌─────────────┐  ┌──────────────┐  ┌────────┐ │
│  │  Qt 6 App  │  │    BlueZ     │  │  WiFi  │ │
│  │  (QML UI)  │──│  (蓝牙协议栈) │  │Manager │ │
│  └──────┬──────┘  └──────┬───────┘  └────────┘ │
│         │                 │                      │
│  ┌──────▼─────────────────▼───────┐             │
│  │       Audio Pipeline            │             │
│  │   (PulseAudio / PipeWire)     │             │
│  └──────────────┬────────────────┘             │
│                 │                                │
│        ┌────────▼────────┐                      │
│        │ USB Bluetooth   │                      │
│        │    CSR 4.0      │                      │
│        └────────┬────────┘                      │
└─────────────────┼────────────────────────────────┘
                  │
         ┌────────▼────────┐
         │   手机 (A2DP)   │
         │  播放音乐/控制  │
         └─────────────────┘

                  │ WiFi
                  ▼
          ┌───────────────┐
          │   LRCLib API  │
          │  获取同步歌词  │
          └───────────────┘
```

---

## 技术栈

| 层级 | 技术选型 |
|------|----------|
| 操作系统 | Armbian (基于 Debian/Ubuntu) |
| UI 框架 | Qt 6 + QML |
| 蓝牙协议栈 | BlueZ 5.x |
| 音频服务 | PulseAudio 或 PipeWire |
| 蓝牙音频 | BlueZ A2DP profile + PulseAudio |
| 歌词获取 | C++ HTTP 客户端 (libcurl / Qt Network) |
| 歌词缓存 | SQLite |
| 构建工具 | CMake + Qt CMake 模块 |

---

## 功能模块

### 1. BluetoothManager

**职责**: 管理蓝牙连接和音频接收

**功能**:
- 扫描并配对手机蓝牙
- 建立 A2DP 音频连接
- 接收手机发送的音频流并输出到 PulseAudio
- 解析 AVRCP 元数据（歌曲名、歌手、专辑）
- 响应播放/暂停/切歌控制

**接口**:
```cpp
class BluetoothManager : public QObject {
    Q_OBJECT
signals:
    void deviceConnected(const QString& name);
    void deviceDisconnected();
    void metadataReceived(const TrackMetadata& meta);
    void playbackStateChanged(bool playing);

public:
    Q_INVOKABLE void startPairing();
    Q_INVOKABLE void disconnect();
};
```

### 2. LyricsFetcher

**职责**: 从网络获取歌词

**功能**:
- 根据歌曲名 + 歌手查询 LRCLib API
- 解析 LRC 格式歌词（时间轴 + 文本）
- 多版本选择，优先返回有同步时间轴的版本
- API 请求添加 User-Agent 防拦截

**API 端点**:
```
GET https://lrclib.net/api/get?artist_name={artist}&track_name={title}
GET https://lrclib.net/api/search?q={query}
```

**接口**:
```cpp
class LyricsFetcher : public QObject {
    Q_OBJECT
signals:
    void lyricsFetched(const LyricsData& lyrics);
    void fetchFailed(const QString& error);

public:
    Q_INVOKABLE void fetchLyrics(const QString& artist, const QString& title);
};
```

### 3. LyricsCache

**职责**: 本地缓存歌词

**功能**:
- SQLite 存储歌词记录
- Key: `artist + title` 的 MD5 哈希
- Value: LRC 原文 + 获取时间 + 版本来源
- 缓存过期策略：永久有效（歌词一般不变）

**数据库表**:
```sql
CREATE TABLE lyrics (
    hash TEXT PRIMARY KEY,
    artist TEXT,
    title TEXT,
    plain_lyrics TEXT,
    synced_lyrics TEXT,
    duration REAL,
    source TEXT,
    cached_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

### 4. VinylPlayer (QML 组件)

**职责**: 旋转唱片 UI

**功能**:
- 显示专辑封面（从 AVRCP metadata 获取，若无则显示默认唱片图）
- 播放时唱片持续旋转（33⅓ RPM 模拟真实唱片）
- 暂停时唱片停止在当前角度
- 动画使用 QML `RotationAnimation`

**QML 接口**:
```qml
VinylPlayer {
    id: vinyl
    coverImage: albumCover
    isPlaying: playbackState

    PropertyAnimation on rotation {
        running: isPlaying
        from: 0
        to: 360
        duration: 1800  // 33⅓ RPM = 1 revolution per 1.8s
        loops: Animation.Infinite
    }
}
```

### 5. LyricsDisplay (QML 组件)

**职责**: 歌词显示与同步

**功能**:
- 显示当前歌曲的歌词列表
- 当前行高亮（字体放大 + 颜色变化）
- 自动滚动到当前播放行
- 若无同步歌词，降级为纯文本静态显示

**歌词时间轴解析**:
```
LRC 格式: [mm:ss.xx]歌词文本
解析为: QList<QPair<double, QString>> (时间戳, 文本)
```

**同步逻辑**:
```cpp
void updateCurrentLine(double currentTime) {
    for (int i = lyrics.count() - 1; i >= 0; --i) {
        if (currentTime >= lyrics[i].timestamp) {
            setCurrentLine(i);
            break;
        }
    }
}
```

### 6. MainUI (QML 页面)

**职责**: 主界面整合

**布局**:
```
┌────────────────────────┐
│     ◉ 蓝牙状态图标      │
├────────────────────────┤
│                        │
│    ┌──────────────┐    │
│    │              │    │
│    │   旋转唱片    │    │
│    │  (专辑封面)   │    │
│    │              │    │
│    └──────────────┘    │
│                        │
│       歌曲名称         │
│       歌手名称         │
│                        │
│  ━━━━━━━━●━━━━━━━━━━  │  ← 进度条（可选）
│                        │
│  ┌──────────────────┐ │
│  │  ▶ 上一首  下一首 │ │
│  └──────────────────┘ │
│                        │
│  ┌──────────────────┐ │
│  │   当前歌词行      │ │
│  │   上一行歌词      │ │
│  │   下一行歌词      │ │
│  └──────────────────┘ │
└────────────────────────┘
```

---

## 数据流

```
1. 手机连接开发板蓝牙
          ↓
2. 手机播放音乐，A2DP 音频流传输到开发板
          ↓
3. AVRCP 协议发送元数据（歌名/歌手）
          ↓
4. BluetoothManager 解析元数据，emit metadataReceived
          ↓
5. MainUI 收到元数据，查询 LyricsCache
          ↓
     ├── 缓存命中 → 直接使用
     └── 缓存未命中 → LyricsFetcher 请求 LRCLib API
          ↓
6. 歌词数据返回，存入 LyricsCache
          ↓
7. VinylPlayer 开始旋转，LyricsDisplay 同步显示歌词
```

---

## 依赖项

### 系统依赖 (apt)

```bash
sudo apt install \
    bluez \
    pulseaudio \
    libpulse-dev \
    libbluetooth-dev \
    libsqlite3-dev \
    cmake \
    qt6-base-dev \
    qt6-declarative-dev \
    qt6-multimedia-dev \
    qt6-networkauth-dev \
    libcurl4-openssl-dev
```

### CMake 构建配置

```cmake
cmake_minimum_required(VERSION 3.16)
project(VinylMusicBox)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    QML
    Quick
    Network
    Multimedia
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(BLUEZ REQUIRED bluez)
pkg_check_modules(PULSEAUDIO REQUIRED libpulse)

target_link_libraries(vinylmusicbox PRIVATE
    Qt6::Core
    Qt6::QML
    Qt6::Quick
    Qt6::Network
    Qt6::Multimedia
    ${BLUEZ_LIBRARIES}
    ${PULSEAUDIO_LIBRARIES}
    curl
    sqlite3
)
```

---

## 项目结构

```
VinylMusicBox/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── BluetoothManager.h/cpp
│   ├── LyricsFetcher.h/cpp
│   ├── LyricsCache.h/cpp
│   ├── TrackMetadata.h
│   └── LyricsData.h
├── qml/
│   ├── main.qml
│   ├── MainUI.qml
│   ├── VinylPlayer.qml
│   ├── LyricsDisplay.qml
│   └── resources/
│       └── vinyl_default.png
├── resources/
│   └── icons/
└── tests/
    └── test_lyrics.cpp
```

---

## 已知限制与降级策略

| 情况 | 处理方式 |
|------|----------|
| 无专辑封面 | 显示默认唱片图（预设 PNG） |
| 无同步歌词 (`syncedLyrics == null`) | 显示纯歌词文本，不做时间同步滚动 |
| 歌词完全不匹配 | 显示"暂无歌词" |
| 蓝牙断开 | UI 显示"等待连接"，唱片停止 |
| LRCLib API 请求失败 | 3秒超时后显示"歌词加载失败" |
| 手机不支持 AVRCP 元数据 | 仅播放音频，无 UI 信息展示 |

---

## 测试计划

### 单元测试
- LRC 时间轴解析正确性
- 歌词缓存的存取
- MD5 hash 生成一致性

### 集成测试
- 蓝牙配对和 A2DP 连接
- LRCLib API 请求与解析
- QML 动画触发/停止

### 手动测试
- 手机连接后播放/暂停/切歌
- 歌词滚动与音频同步准确性
- 长时间运行稳定性

---

## 后续扩展

- [ ] 支持显示专辑封面（通过 MusicBrainz API 匹配）
- [ ] 支持DLNA/UPnP 作为播放源
- [ ] 换用更大屏幕 (2.0" GC20) 改善歌词显示
- [ ] 添加 EQ 音效控制
- [ ] 支持触摸控制播放

---

## 参考资料

- [LRCLib API 文档](https://lrclib.net/docs)
- [BlueZ A2DP 文档](http://www.bluez.org/)
- [Qt 6 官方文档](https://doc.qt.io/qt-6/)
- [Orange Pi Zero 3 官方文档](http://www.orangepi.org/)
