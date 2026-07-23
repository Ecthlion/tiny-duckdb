# 开发环境搭建：Ubuntu 24.04 / macOS

tiny-duckdb 的 C++ 部分只依赖 C++17 编译器、Make 或 CMake，测试框架已经放在仓库中；Lab 4 额外使用 Python。以下命令从一台干净机器开始。

## 1. Ubuntu 24.04

安装工具链：

```bash
sudo apt update
sudo apt install -y build-essential cmake git python3 python3-venv python3-pip
```

确认版本：

```bash
g++ --version       # Ubuntu 24.04 通常为 GCC 13
cmake --version     # 要求 >= 3.16
python3 --version
```

克隆并构建：

```bash
git clone YOUR_REPOSITORY_URL tiny-duckdb
cd tiny-duckdb
make -j"$(nproc)"
./tdbtest Lab0
```

看到 `5 passed, 0 failed` 说明 C++ 环境就绪。也可以使用 CMake：

```bash
cmake -S . -B cmake-build -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build -j
ctest --test-dir cmake-build --output-on-failure
```

Debug 构建适合做实验：断言、堆栈和变量不会被 `-O2` 优化掉。提交前再运行 Makefile 的默认优化构建。

## 2. macOS

支持 Apple Silicon 和 Intel Mac。先安装 Apple Command Line Tools：

```bash
xcode-select --install
```

确认系统的 Clang 和 Make 可用：

```bash
clang++ --version
make --version
```

仓库的 Makefile 不依赖 GNU Make 特性，可以直接构建：

```bash
git clone YOUR_REPOSITORY_URL tiny-duckdb
cd tiny-duckdb
make -j"$(sysctl -n hw.logicalcpu)"
./tdbtest Lab0
```

macOS 没有 Linux 的 `nproc`，不要直接复制 `make -j$(nproc)`。不想记平台差异时，`make -j4` 在两边都可用。

若要使用 CMake 和 Lab 4，推荐通过 Homebrew 安装：

```bash
brew install cmake python
cmake -S . -B cmake-build -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build -j
ctest --test-dir cmake-build --output-on-failure
```

若同时安装了多个编译器，可显式选择：

```bash
make clean
make -j4 CXX=clang++
```

不要在同一个 `build/` 中混用 GCC 与 Clang 的旧目标文件；切换编译器前先 `make clean`，CMake 则删除并重建 `cmake-build/`。

## 3. Lab 4 的 Python 环境

在项目内创建虚拟环境，避免污染系统 Python：

```bash
python3 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install duckdb pytest
cd lab4_lakebase
python -m pytest test_lakebase.py -v
```

退出虚拟环境用 `deactivate`。Ubuntu 24.04 默认启用 externally-managed Python，直接对系统环境运行 `pip install` 可能失败；使用 `.venv` 是预期做法。

## 4. 推荐的实验工作流

```bash
# 1. 只编译
make -j4

# 2. 只跑正在做的任务
./tdbtest Lab1StorageTest.ColumnChunk

# 3. 跑当前 Lab
./tdbtest Lab1

# 4. 提交前跑全部回归
make test

# 5. 手工执行 SQL
./tiny_duckdb_shell
```

测试过滤器做的是名称子串匹配。比如 `./tdbtest Lab5` 会运行 Lab 5 的全部用例，`./tdbtest Lab5VectorExpressionTest.DistanceKernels` 只运行一个任务组。

## 5. 调试与常见问题

### `command not found: nproc`

这是 macOS，不是构建失败。改用：

```bash
make -j"$(sysctl -n hw.logicalcpu)"
```

### `fatal error: ... file not found`

必须在仓库根目录运行 `make`。用 `pwd` 和 `ls CMakeLists.txt` 确认当前位置。不要单独编译某个 `.cpp`，否则会丢失 `src/include` 与测试头文件的 include path。

### 修改头文件后出现奇怪的类型或链接错误

Makefile 是简化的教学版本，没有自动生成头文件依赖。做一次干净构建：

```bash
make clean
make -j4
```

### macOS 出现 C++ 标准库类型不匹配，但 Ubuntu 正常

不要把 `size_t`、`uint64_t` 和项目的 `idx_t` 混着交给模板推导（例如 `std::min/std::max`）；先显式转换为同一类型。这类差异是同时保留 GCC 与 Clang CI 的原因。

### 并发测试偶发失败

先确认语义测试在单线程通过，再重复压力测试：

```bash
for i in {1..20}; do ./tdbtest Lab0MorselTest.ConcurrentExactlyOnce || break; done
```

不要用日志输出“修复”竞态；I/O 会改变线程调度，让问题暂时消失。

### 使用 AddressSanitizer

最容易排查越界和 use-after-free：

```bash
cmake -S . -B asan-build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer"
cmake --build asan-build -j
./asan-build/tdbtest
```

Apple Clang 与 Ubuntu GCC 都支持上述配置。
