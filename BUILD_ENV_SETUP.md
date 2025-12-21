# 开发环境搭建指南

## 1. MIPS交叉编译工具链安装

### 1.1 下载工具链

根据搜索结果，推荐下载以下MIPS交叉编译工具链：

- **下载链接**：https://sourcery.mentor.com/GNUToolchain/package11863/public/mips-sde-elf/mips-2013.05-65-mips-sde-elf.exe
- **工具链类型**：mips-sde-elf (适用于MIPS裸机程序开发)

### 1.2 安装步骤

1. 下载上述exe文件后，右键点击安装程序
2. 如果出现兼容性问题，选择"属性" -> "兼容性"，勾选"以兼容模式运行这个程序"，选择"Windows 7"
3. 双击运行安装程序
4. 在安装过程中，**务必勾选"Add to PATH"选项**，这样系统就能自动找到编译工具
5. 选择默认安装路径或自定义路径，建议使用默认路径

### 1.3 验证安装

安装完成后，打开命令提示符(CMD)，执行以下命令验证安装：

```bash
mips-sde-elf-gcc --version
```

如果能显示编译器版本信息，则说明安装成功。

## 2. 修改项目配置

由于我们安装的是`mips-sde-elf`工具链，而项目中使用的是`mips-mti-elf`，需要修改项目配置文件：

### 2.1 修改 include.mk 文件

打开 `e:\cse-os\include.mk`，将：

```makefile
CROSS_COMPILE := mips-mti-elf-
```

修改为：

```makefile
CROSS_COMPILE := mips-sde-elf-
```

## 3. OpenOCD 安装

OpenOCD用于硬件调试，需要单独安装。

### 3.1 下载OpenOCD

- **下载链接**：https://github.com/openocd-org/openocd/releases
- **选择版本**：选择最新的Windows版本(通常是zip格式)

### 3.2 安装步骤

1. 下载zip文件后，解压到任意目录
2. 将解压目录下的`bin`文件夹添加到系统环境变量`PATH`中

### 3.3 验证安装

打开命令提示符(CMD)，执行以下命令验证安装：

```bash
openocd --version
```

如果能显示OpenOCD版本信息，则说明安装成功。

## 4. 测试项目编译

安装完成后，进入项目目录，执行以下命令测试编译：

```bash
cd e:\cse-os
make
```

如果编译成功，将生成`vmlinux.elf`、`vmlinux.rec`等文件。

## 5. 常见问题解决

### 5.1 环境变量问题

如果执行`make`时提示找不到编译器命令，请检查：
- 工具链是否正确安装
- 环境变量`PATH`是否包含了工具链的`bin`目录
- 可以手动将工具链的`bin`目录添加到PATH中：
  ```bash
  set PATH=%PATH%;C:\Program Files (x86)\Mentor Graphics\Sourcery CodeBench Lite\mips-sde-elf\bin
  ```

### 5.2 编译器版本问题

如果出现编译错误，可能是编译器版本兼容性问题：
- 可以尝试调整`include.mk`中的编译选项
- 参考项目文档中的编译器版本要求

## 6. 其他开发工具

为了更好地进行开发，建议安装：

- **Visual Studio Code**：代码编辑器，支持C语言开发和调试
- **MIPS插件**：VS Code的MIPS支持插件
- **Git**：版本控制工具，用于项目管理

## 7. 参考资料

- MIPS交叉编译工具链安装：https://blog.csdn.net/weixin_40751723/article/details/120295374
- OpenOCD官方文档：https://openocd.org/doc/html/
