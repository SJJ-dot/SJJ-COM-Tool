# SJJ-COM Tool（C++ / Qt6 版）

Windows 串口调试工具，功能与原 PySide6 版（`../sjj_com_tool.py`）完全一致：
模仿 SSCOM 软件功能，增加接收区滚动暂停、字符编码切换、搜索、筛选功能。

## 环境依赖

| 项目 | 要求 |
|------|------|
| Qt | **6.5 及以上**（需安装组件：Widgets、SerialPort、Network、Core5Compat） |
| 编译器 | MSVC 2019+（推荐）或 MinGW 11+（64 位） |
| CMake | 3.16+ |
| 操作系统 | Windows 10/11（DWM 阴影/圆角仅 Windows 生效） |

> Core5Compat 组件提供 `QTextCodec`，用于 GBK/GB2312 等编码转换。
> 安装 Qt 时在组件列表中勾选 **Qt 5 Compatibility Module** 即可。

## 构建

```bash
# 在 cpp 目录下执行
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.8.0/mingw_64"
cmake --build build --config Release
```

- MSVC 请把 `-DCMAKE_PREFIX_PATH` 指向 `C:/Qt/6.8.0/msvc2019_64`。
- 生成的可执行文件在 `build/` 下：`SJJ-COM-Tool.exe`。
- 可指定版本号注入（GitHub Actions 打包用）：`-DAPP_VERSION=v1.0.1`。
- 运行需要 Qt 运行库（把 `C:/Qt/.../bin` 加入 PATH，或用 windeployqt 部署）：

```bash
C:/Qt/6.8.0/mingw_64/bin/windeployqt.exe build/SJJ-COM-Tool.exe
```

## 目录结构

```
cpp/
├── CMakeLists.txt        # CMake 构建脚本（AUTOMOC + 资源 + dwmapi）
├── resources.qrc         # 应用图标（内嵌 imgs/ic_xue_xi.png）
└── src/
    ├── main.cpp          # 入口：字号/图标/主窗口
    ├── serialtool.h/.cpp # 主窗口：全部业务逻辑（~1700 行）
    ├── themes.h/.cpp     # 主题系统：Catppuccin Mocha/Latte + 全局 QSS 生成
    ├── utils.h/.cpp      # 编码转换、HEX 解析/显示、8 种校验算法、版本比较、图标
    ├── combobox.h/.cpp   # 主题化下拉框（箭头覆盖）+ 端口下拉
    ├── themechkbox.h/.cpp# 主题化复选框（手动绘制对勾）
    ├── titlebar.h/.cpp   # 自绘标题栏（拖动/双击/右键/版本号点击检查更新）
    └── updater.h/.cpp    # GitHub Releases 检查 + HTTP Range 多线程分块下载
```

## 与原 Python 版功能对照

| Python 版（PySide6） | C++ 版（Qt6） |
|---|---|
| pyserial 读线程 | `QSerialPort` readyRead 信号（无需线程，更简洁） |
| HEX/文本双模式收发 | 一致（`hexBody` 保留 0x0A 分包换行逻辑） |
| 7 种接收编码 | 一致（GBK/GB2312 走 QTextCodec，UTF-16LE/BE/ASCII 手写） |
| 8 种校验算法 + 第[x]字节范围 | 一致（含 0-ADD8/ADD16/ModbusCRC16/CCITT/CRC32，CRLF 参与计算） |
| 暂停刷新 / 搜索高亮 / 筛选 | 一致（ExtraSelection 高亮、200ms 防抖） |
| 定时发送 / 文件发送（可停止） | 一致（4096 字节分块，QTimer::singleShot 驱动） |
| 历史记录 / 快捷命令 / 循环发送 | 一致（动态行 + 右键改名 + 循环轮询） |
| 双主题（Catppuccin） | 一致（QSS 模板与 Python 版逐条对应） |
| 无边框窗口 + DWM 阴影/圆角 | 一致（FramelessWindowHint + DwmExtendFrameIntoClientArea） |
| GitHub 更新检查 + 分块下载 | 一致（QNetworkAccessManager + QThreadPool Range 分块） |
| sscom_config.json 配置持久化 | 一致（字段名完全兼容，可直接复用旧配置） |
| 自动更新替换重启（bat） | 一致（UTF-8 BOM 脚本 + cmd 静默执行） |

## 说明

- 配置保存位置：与 Python 版相同，`sscom_config.json` 放在**可执行文件所在目录**，
  字段名与原版一致，可直接继承已保存的配置。

## 静态单文件构建（零依赖 exe）

**一键脚本**：双击 `build_static.bat` 即可（自动检查工具链 → 首次自动编译静态 Qt → 静态编译应用 → 输出 `dist_static\SJJ-COM-Tool.exe`）。
已完成的步骤自动跳过（增量构建，再次运行约 1 分钟）。可通过环境变量覆盖路径：
`W64DEVKIT`（工具链目录）、`QT_SRC`（源码目录）、`QT_STATIC_PREFIX`（静态 Qt 安装目录）。

手动流程（Qt 官方没有预编译的 MinGW 静态包，需自行编译，`build_static.bat` 已封装以下步骤）：

```bash
# 1) 下载源码
git clone --depth 1 --branch v6.8.2 https://github.com/qt/qtbase.git
git clone --depth 1 --branch v6.8.2 https://github.com/qt/qt5compat.git
git clone --depth 1 --branch v6.8.2 https://github.com/qt/qtserialport.git

# 2) 编译静态 qtbase（shadow build，约 12 分钟）
mkdir qtbase-static-build && cd qtbase-static-build
export CC=D:/.../gcc.exe CXX=D:/.../g++.exe   # w64devkit 的 gcc
../qtbase/configure.bat -static -release -opensource -confirm-license \
  -prefix D:/dev/qt-static-6.8.2 -nomake examples -nomake tests \
  -no-opengl -no-dbus -no-feature-vulkan
ninja -j 8 && cmake --install .

# 3) 静态编译 qt5compat / qtserialport（-DCMAKE_PREFIX_PATH 指向静态 Qt）
cmake -S ../qt5compat  -B . -G Ninja -DCMAKE_PREFIX_PATH=D:/dev/qt-static-6.8.2 \
  -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Release && ninja && cmake --install .
# qtserialport 同理

# 4) 静态编译应用（CMakeLists 检测到静态 Qt 后自动链接插件并加 -static）
cmake -S . -B build_static -G Ninja -DCMAKE_PREFIX_PATH=D:/dev/qt-static-6.8.2 \
  -DCMAKE_CXX_COMPILER=D:/.../g++.exe -DCMAKE_MAKE_PROGRAM=D:/.../ninja.exe
cmake --build build_static
```

产物 `build_static/SJJ-COM-Tool.exe`（约 48MB）为**纯静态链接**：
`objdump -p` 确认只依赖 Windows 系统 DLL（KERNEL32/USER32/GDI32/WS2_32/SETUPAPI 等），
无任何 Qt / libgcc / libstdc++ / winpthread DLL，可单独拷贝到任意 Windows 10/11 运行。

> 注意：Qt 6.8 的样式插件已从 Vista 风格改为 ModernWindows 风格；
> 静态导入插件逻辑在 `main.cpp`（`SJJ_STATIC_BUILD` 宏）与 CMakeLists 中自动处理。
- 本机未安装 Qt 时无法直接编译；如需在 CI 中构建，参考 `.github/workflows` 中
  PySide6 版的打包流程改为 Qt6 CMake 即可。
