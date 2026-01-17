# CSE-OS 开发环境搭建指南

## 问题诊断

根据您的反馈，C:\Program Files\Imagination Technologies\Toolchains目录为空，说明MIPS交叉编译工具链没有正确安装。

## 解决方案

### 1. 重新安装MIPS交叉编译工具链

**下载链接：**
- MIPS SDE-ELF工具链: https://sourcery.mentor.com/GNUToolchain/package11863/public/mips-sde-elf/mips-2013.05-65-mips-sde-elf.exe

**安装步骤：**
1. 下载上述安装程序
2. 右键点击安装程序，选择"属性" → "兼容性" → 勾选"以兼容模式运行这个程序"并选择"Windows 7"
3. 运行安装程序，在安装过程中确保勾选"Add to PATH"选项
4. 默认安装路径通常为：`C:\Program Files\Mentor Graphics\Sourcery Tools for MIPS`

### 2. 验证工具链安装

安装完成后，在PowerShell中运行以下命令验证：

```powershell
# 检查工具链是否在PATH中
mips-sde-elf-gcc --version

# 或者手动检查安装目录
Get-ChildItem "C:\Program Files\Mentor Graphics" -Recurse -Filter "mips-sde-elf-gcc.exe" -ErrorAction SilentlyContinue
```

### 3. 更新项目配置

修改 `include.mk` 文件中的交叉编译工具链配置：

```makefile
CROSS_COMPILE = mips-sde-elf-
```

### 4. 安装Make工具

**方法一：使用Chocolatey（推荐）**
```powershell
# 安装Chocolatey（如果尚未安装）
Set-ExecutionPolicy Bypass -Scope Process -Force; [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072; iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))

# 安装make
choco install make -y
```

**方法二：手动下载**
- 下载地址：http://gnuwin32.sourceforge.net/packages/make.htm
- 下载"Complete package, except sources"版本

### 5. 测试编译环境

```powershell
# 进入项目目录
cd e:\cse-os

# 测试编译（使用正确的PowerShell语法）
make
```

## 常见问题解决

### 问题1：PowerShell中`&&`语法错误
PowerShell 5不支持`&&`操作符，请使用分号`;`：
```powershell
cd e:\cse-os; make
```

### 问题2：找不到make命令
确保make工具已正确安装并添加到PATH环境变量中。

### 问题3：找不到MIPS编译器
检查工具链是否正确安装，并验证PATH环境变量包含工具链的bin目录路径。

## 快速配置脚本

创建 `setup_env.bat` 文件：

```batch
@echo off
echo 设置MIPS交叉编译工具链环境变量
set PATH=C:\Program Files\Mentor Graphics\Sourcery Tools for MIPS\bin;%PATH%
echo 环境变量设置完成
cmd /k
```