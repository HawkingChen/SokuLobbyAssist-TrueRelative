# TrueRelative 协作交接

最后更新：2026-10-16。本文用于本地代理与云端代理共享项目上下文。
它记录已知事实和用户决定，不代替最新任务指示。开始工作时仍需读取相关源码。

## 1. 当前状态

- 插件名：真实相对；英文项目名和 DLL 名：TrueRelative / TrueRelative.dll。
- 作者署名：凹帝。项目原创代码采用 MIT；第三方库与游戏素材保留各自权利声明。
- 当前版本：0.8.0；GitHub 和 Gitee 均已有 v0.8.0 Release 和插件安装包。
- 已验证兼容的联机大厅版本：0.7.5.2、0.7.5.4。具体 DLL 哈希、布局和证据见
  [VERIFICATION.md](VERIFICATION.md)。不改写大厅 DLL 文件。
- 本地游戏内已安装 0.8.0 DLL，且与本地构建产物 SHA-256 相同。
  游戏目录的 mod.json 曾误留 0.7.9，现已补成 0.8.0。
- 本交接文档创建前的代码提交：`1423e75`（打包脚本和验证文档更新）。
- v0.8.0 已发布标签曾核对为两边均指向 `d927a3f98af678b0a96f25eaa42ca6acef5c2f36`。
  之后 main 增加了文档、MIT 和打包脚本改进；不要把 main 与已发布标签视为同一快照。
- 当前新任务：建立共同交接文档。用户准备在云端继续开发，并希望借助云端解决
  Gitee Release 附件上传缓慢的问题；尚无云端上传方案已完成的记录。

## 2. 用户已确定的功能

- F9 打开 GUI 设置菜单，手柄 Start+Select 也可打开。
- 等级分显示有“自动 / 手动(F8) / 关闭”三种模式。
  F8 在手动模式下切换附近玩家；F10 切换自动/关闭。
- 自己的信息显示有独立开关，不受 F10 影响。F7 旧快捷键已取消。
- 菜单提供评分框位置预设、相对等级显示、阈值和全体标记设置。
  设置持久化到 SokuLobbyAssist.ini / relative-levels.txt。
- 相对等级默认隐藏。默认最低分数线从高到低为 2800、2500、2300、2000、1800。
  评分 <= 1400 且总局数 <= 100 为“相对1萌新”；其余低于相对3门槛的为“相对<3”。
  颜色跟随用户配置的分数线。
- 评分卡显示玩家名、总评分、小字号 RD，以及按对战局数选取的前三个角色评分。
  角色使用中文简称，例如幽幽子、妖梦。
- 只给选中的附近玩家及可选的自己画评分卡。旧的等待玩家精简卡已经去除，避免重叠。
- 全体标记默认关闭。低于自己指定分差的玩家用绿色保护光环，高于自己指定分差的
  用红框和 !!! 危险标记。原隐藏输入指令方案已转为 GUI 设置。
- 原网络预检功能已放弃并移除；不要恢复拦截加入连接、握手测延迟等旧逻辑。
  它曾影响正常联机；如将来重做，应按用户新指示处理。

## 3. 评分数据和匹配

- 网站：https://soku.cleista.cc/
- 正式 API 文档入口：https://soku.cleista.cc/llms.txt
- OpenAPI：https://soku.cleista.cc/api/v1/openapi.json
- 当前榜单使用 GET /api/v1/ratings/export.json。网站管理员推荐此预生成接口，
  便于服务端缓存、减少计算消耗。榜单缓存 10 分钟，支持 ETag / HTTP 304。
- 下载失败保留旧缓存。下载在工作线程中进行，不在切换玩家时重复下载整榜。
- 机签/认领名查询的具体路径和解析应查看 Ratings.c 的 claim_url / parse_claim。
  曾讨论 POST /api/v1/ratings/players、请求体 {"names": [...]}、最多 1000 个名字；
  讨论过不代表当前已使用，变更前核对最新正式 API。
- 优先匹配 profile / playedAs，再处理显示名、认领关系和本地手工关联。
  重名有歧义时应显示未知，不能为了提高命中率冒认另一个人的评分。
- 认领关系缓存与榜单缓存不同：认领结果约一天，其他玩家结果保存在
  other-claim-cache.tsv；手工关联为 ratings-map.tsv。
- 传输错误看 TrueRelative-network.log，榜单缓存为 ratings-cache.json。
- 0.7.9 已用静态 libcurl + Mbed TLS 代替独立 curl.exe，主要解决杀毒软件拦截
  下载程序后所有玩家评分未知的问题。不能承诺所有杀毒软件均不再拦截。

## 4. 仓库和 Release 协作

GitHub 主仓库：https://github.com/HawkingChen/SokuLobbyAssist-TrueRelative

Gitee 镜像：https://gitee.com/whzstsl/soku-lobby-assist-true-relative

- 用户已配置 Gitee 自动同步 GitHub。日常代码仅推 GitHub；同步有延迟，
  不必每次开发都检查两边是否一致，也不需要手动推 Gitee。
- Release 和附件此前由本地脚本分别发布到两边，不能依靠仓库自动同步完成。
- 创建新版 Release 时要核对该版本标签对应的提交，避免 Gitee 尚未同步就
  在旧 main 上自动创建同名标签。发布脚本目前没有自动防护，仍是待改进事项。
- 已发布标签不要随意移动。源代码 ZIP/tar.gz 由平台根据标签生成，无需另外上传源码包。
- 本地的 gitee/main 跟踪引用可能很旧；`ahead N` 本身不能证明 Gitee 服务器未同步。
- 两边并行开发时，先获取 GitHub 最新提交。修改同一文件前查看已有提交和工作区，
  尽量把任务拆为独立提交；不要覆盖对方提交或强制推送。

### Gitee 附件上传：已有事实与待办

- 本地过去遇到附件上传缓慢或超时。曾考虑 VPN、异地/云端上传，尚未证明哪种
  网络路线稳定。不能把“换到云端一定更快”写成结论。
- 不代表历史附件缺失：0.7.7、0.7.8、0.7.9、0.8.0 都已有 Release，
  其中 v0.8.0 的 TrueRelative-0.8.0-install.zip 已成功上传并核对过。
- 当前云端任务应先确认究竟需要上传哪个版本、现有附件是否已在，而不是重复上传。
- 如果只是补传某个已发布版本，优先取得 GitHub 上该版本已经发布的 ZIP，核对
  SHA-256 后将相同字节上传到 Gitee。不要用当前 main 重建来冒充历史附件。
- 特别注意：本地最新 0.8.0 ZIP 已加入 MIT LICENSE，而已发布 0.8.0 附件未替换；
  两者版本号相同，但 ZIP 内容不同。用户明确不为许可证单独发布新版。
- Gitee API 已用的路径（基址为 https://gitee.com/api/v5/repos/whzstsl/soku-lobby-assist-true-relative）：
  GET /releases、GET /releases/tags/{tag}、PATCH /releases/{id}；
  GET/POST /releases/{id}/attach_files。
- 现有上传请求使用 multipart 表单，字段有 owner、repo、release_id、file 和鉴权。
  本地脚本通过 curl stdin 配置传递凭据，使用 Expect: 空头避免等待 100-continue。
  创建/修改 Release 与上传附件是独立步骤，超时后应先查询状态再重试。
- 完成云端调查后，把环境、上传结果、耗时、附件名/大小/哈希以及下一步更新到本文。

## 5. 本地与云端环境差异

本地工作目录：`/home/deck/TrueRelative`。

本地游戏插件目录：`/run/media/deck/share/fxtz/th123/modules/SokuLobbyAssist/`。

- 这些绝对路径属于本地 SteamOS 主机，云端不可假设存在。
- 本地发布脚本是 `/home/deck/.local/bin/publish-truerelative-release.py`，
  尚未纳入仓库。它读取 mod.json / RELEASE_NOTES.md 和 build 下的 ZIP，
  并使用本地仓库外的令牌文件。不要复制凭据到公开仓库、交接文档或日志。
- 脚本支持 SKIP_GITHUB=1、SKIP_GITEE=1、SKIP_GITEE_UPLOAD=1；
  同名附件已存在时直接跳过，不会核对/覆盖其内容。
  SKIP_GITEE_ATTACHMENT_CHECK=1 会跳过查重，仅用于明确调查过的情况。
- 云端如需发布，应使用其环境中配置的凭据；本地凭据文件不会因绑定 GitHub 自动共享。
  后续可将发布工具整理为可移植脚本，但不要把上述本地脚本的绝对路径直接照搬。

### 构建、测试和资料

- `sh build.sh`：Python 3、zip、Zig 0.14.1；输出 build/TrueRelative-<version>-install.zip。
  版本号来自 mod.json，临时打包目录独立，成功后替换本地 ZIP。不会自动上传。
- `third_party/libcurl/` 有已编译 x86-windows-gnu 静态库、头文件、源归档和许可，
  主插件构建直接使用；重建依赖看其中 README.md 和 rebuild.py。
- `python3 test_ratings.py` 目前依赖本地硬编码的 Proton、Wine prefix 和已安装大厅 DLL。
  云端不能直接运行就断定失败；需说明缺失环境，或先做可移植改造。
- test_fixtures/SokuLobbiesMod-0.7.5.0.dll 是版本拒绝检查所需的旧 DLL，不能当垃圾删。
- vendor/ 是分析时保留的上游源码，用户明确要求保留；build/ 目前也不需要清理。
  zig-x86_64-linux-0.14.1/ 是有用的本地编译器，保留。
  这些目录由 .gitignore 排除，不会随仓库自动到云端。
- 细节和验证范围见 VERIFICATION.md。正常游戏运行已有用户反馈和截图，
  但尚未记录完整的 Windows 杀毒软件与 SteamOS 双模式手柄测试矩阵。

## 6. 用户的文档与沟通偏好

- 中文、简短、直接；已经明确授权的工作继续完成，少做重复确认。
- 游戏名写“东方非想天则（Touhou 12.3）”；大厅称“联机大厅”。
- 安装入口是 SokuLauncher“设置 - 插件 - 安装”，安装/解压后最后启用“真实相对”。
- README 放数据来源和新旧快捷键对照（0.7.9 及之前 / 0.8.0 之后）。
  保持简洁，不塞缓存实现细节、重复设置介绍。
- Release 写该版本支持的大厅版本、新版快捷键、简短更新和安装说明，
  不必特意写数据来源；附件称“插件安装包”，不要写“启动器安装包”。
- 截图发布前将头像上及评分卡内重复的玩家名一起打码。
  docs/screenshots/ 中两张是已处理的原图；README 用两张，0.8.0 Release 用第一张。
  Release 图片宽度设为 640，可点开原图；保留截图真实性，不重绘菜单/游戏画面。
- 当前没有用户批准的新功能待实现；此前功能建议讨论未形成新的实现任务。

## 7. 最近交接记录

| 日期 | 执行位置 | 实际完成 | 验证/未完成事项 |
| --- | --- | --- | --- |
| 2026-10-16 | 本地 | 0.8.0 文档、打码截图、所有已发布版本的安装路径修正；MIT 许可；动态版本及独立临时目录打包；验证记录更新 | 当前构建通过；临时 9.8.7 版本包名、旧文件排除、ZIP 替换与临时目录清理检查通过。已发布 0.8.0 附件未替换。 |
| 2026-10-16 | 本地 | 新增 HANDOFF.md 与 AGENTS.md 协作入口 | 本次只添加文档；云端 Gitee 上传调查和可移植发布工具尚未开展。 |

后续完成任务时更新相关章节，并在上表追加简短记录；如果旧事项已解决，应同时改掉
对应“待办”描述，避免文档一边写完成、一边仍写未完成。
