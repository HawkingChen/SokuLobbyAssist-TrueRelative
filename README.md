# 真实相对（TrueRelative）

凹帝制作的东方绯想天则（Soku 12.3）国内联机大厅评分显示模组。

- GitHub：<https://github.com/HawkingChen/SokuLobbyAssist-TrueRelative>
- Gitee 镜像：<https://gitee.com/whzstsl/soku-lobby-assist-true-relative>

## 功能

- 显示自己的总评分和附近玩家的评分、相对等级。
- F7 切换相对等级显示，默认开启。自动模式显示离自己最近的玩家详情；支持 F8 手动切换、F9 单独切换自己的评分框、F10 切换自动模式和自己的评分框。
- 评分榜每 24 小时更新一次，查询结果会缓存在模组目录中。
- `relative-levels.txt` 可调整相对等级分数线。
- 只对已核对的大厅 DLL 版本启用：简体中文大厅 0.7.5.2 和 0.7.5.4。其他版本会停用并写入 `TrueRelative.log`。

## 安装

从发布包下载 `TrueRelative-0.7.7-install.zip`，在 SokuLauncher 的模组下载页安装。压缩包内部已包含 `SokuLobbyAssist/` 目录和启动器需要的 `mod.json`。也可以把该目录解压到游戏的 `modules/` 目录。

安装后在启动器的模组设置中启用“真实相对”，然后重启游戏。

## 从源码构建

需要 Zig 0.14.1（支持从 Linux 交叉编译 32 位 Windows DLL）和 `zip` 命令。将 Zig 安装到系统 PATH，或通过 `ZIG=/path/to/zig` 指定：

```sh
ZIG=zig sh build.sh
```

构建会生成 `build/TrueRelative.dll`、`build/package/` 和启动器安装包 `build/TrueRelative-0.7.7-install.zip`。Windows 辅助程序 curl、CA 证书和对应许可证由 `third_party/curl/` 提供。

运行回归测试：

```sh
python3 test_ratings.py
```

该测试使用 GE-Proton 和本机 Soku 安装路径；其他系统需调整 `test_ratings.py` 中的 Wine 路径和大厅 DLL 路径。

兼容性校验和功能验证记录见 [VERIFICATION.md](VERIFICATION.md)。
