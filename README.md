# Libai

x86-64 UEFI 内核。加载器把 ELF 放进物理内存，退出 Boot Services 后跳进高半区内核。

## 依赖

Ubuntu 上需要：

```bash
sudo apt install build-essential gnu-efi qemu-system-x86 ovmf gdb
sudo apt install dosfstools mtools
```

固件默认是 `/usr/share/OVMF/OVMF_CODE_4M.fd`。路径不同时：

```bash
make OVMF=/usr/share/OVMF/OVMF_CODE.fd
```

VSCode 调试需要安装 Microsoft 的 C/C++ 扩展。

## 编译

在项目根目录执行：

```bash
make
```

这会编译加载器和内核，并复制到 ESP 目录：

```text
build/libai-kernel.elf
build/BOOTX64.EFI
build/esp/libai-kernel.elf
build/esp/EFI/BOOT/BOOTX64.EFI
```

VSCode 里用 `Ctrl+Shift+B` 等价于 `make`。只改了一份源码时，Make 会按依赖重编对应的 `.o`。

清理后重编：

```bash
make clean
make
```

## 运行

```bash
make qemu
```

QEMU 使用 `q35`、512MiB 内存、OVMF，串口接在当前终端。出现 `libai>` 后，另开一个终端发按键：

```bash
telnet 127.0.0.1 45454
sendkey h
sendkey e
sendkey l
sendkey p
sendkey ret
```

`sendkey ret` 是回车。退出 QEMU 用 `Ctrl-A x`。

壳里的命令：

```text
help ticks mem gdt cr3 rip win heap task preempt sleep event lock mbox user pf halt
```

## 调试

调试用 QEMU 的 GDB 端口，不是在内核里再写一个调试器。

1. 在源码行号左侧点红点。内核函数要等加载器跳进高半区之后才会命中，入口可以选 `libai_kernel_entry` 或 `kbd_shell`。
2. 按 `F5`。VSCode 会先 `make qemu-gdb`。`-s` 在 `localhost:1234` 打开 GDB，`-S` 让 CPU 在第一条指令前停住。
3. 调试器连上后按继续。固件和加载器跑完，断点才会停在内核函数上。
4. 串口输出在启动 QEMU 的终端里，不在调试控制台里。

也可以分成两个终端：

```bash
make qemu-gdb
```

另一个终端：

```bash
make gdb
```

`scripts/gdbinit` 会连接 `localhost:1234`，加载 `build/libai-kernel.elf`，并在 `libai_kernel_entry` 下断点。

内核符号地址在 `0xFFFFFFFF80100000` 以上。加载器完成高半区映射之前，CPU 还在低地址执行，对内核函数下的断点不会命中。

## 看内存

断点停住后，监视窗口可以直接填变量名，例如 `current`、`demo_lock`、`shared_counter`。

看一段原始内存时，在调试控制台输入：

```text
-exec x/16gx 0xffffffff8000b050
```

`x/16gx` 是从该地址起显示 16 个 8 字节值。任务控制块和栈都在高半区别名上，地址应大于 `0xFFFFFFFF80000000`。

编译使用 `-g -O0`。`-g` 写入行号和变量名，`-O0` 避免编译器删掉局部变量，否则断点会跳行，监视窗口里的变量也会消失。