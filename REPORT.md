# MINI-SHELL 代码审查报告与 parser 补全说明

审查范围：`parser.c`（重点）、`main.c`、`mnsh.c`、`CDS/darray.*`、`CDS/confc.*`、`CDS/input.*`、
`mini-shell.c`、`echokey.c`。
所有结论均经过实际编译与 AddressSanitizer / UndefinedBehaviorSanitizer 复现验证（见第 5 节）。

---

## 1. 总体结论

- `parser.c`（新式命令流解析器）存在 **5 处死循环**、**10+ 处逻辑错误**、**多处越界/未定义行为**和
  **系统性内存泄漏**，其中死循环会导致交互式 shell 一输入特定字符就挂死。
- 本次已对 `parser.c` 做了整体重写：修复全部已确认问题，并实现了下一阶段功能
  （here-doc 主体收集、关键字结构校验、case 模式、函数定义、`$(( ))`/`$( )`/`${ }` 嵌套跳过等），
  通过 60+ 用例的 ASan/UBSan 测试与 2000 轮泄漏测试（free 模式 0 泄漏）。
- `main.c`/`mnsh.c` 中的旧执行器与 `CDS/` 新基础设施处于重构中途：`make` 存在 **22 个预先存在的
  编译错误**（`command_t`、`var_arr_t` 等旧类型已从 `mnsh.h` 移除但旧代码仍在使用），
  与本次改动无关，但意味着 `mnsh` 目前无法构建运行。
- `parser.c` 尚未接入主程序（`Makefile` 无对应目标），执行器为下一阶段工作。

---

## 2. parser.c 问题清单（已全部修复）

### 2.1 死循环（严重，已修复）

| # | 位置 | 问题 | 复现 |
|---|------|------|------|
| 1 | `parse_is_fd()` | `while(buf[i])` 循环内**没有 `i++`**，遇到纯数字串永不退出；且 `fd=buf[i]-'0'` 是覆盖赋值而非累加（`"12"` 会得到 2 而不是 12） | `ls 2>/dev/null`、`echo hi 2> f` 直接挂死 |
| 2 | `parse_divide_command()` 的 `case '\''` | 循环前没有 `i++`，`i` 停在开引号处，外层循环反复进入同一分支 | 任何含单引号的输入挂死 |
| 3 | 同上 `case '`'`（反引号） | 同样缺少先前进一个字符 | 任何含反引号的输入挂死 |

### 2.2 逻辑错误（严重，已修复）

| # | 问题 | 说明 |
|---|------|------|
| 4 | **重定向前的命令被丢弃** | `parse_deal_break()` 对重定向分支只添加 REDIR 操作码，从不把挂起的参数列表发出为 `CMD_EXECUTE`。`ls > test` 解析结果只有 `REDIR_OUT`，命令 `ls` 丢失 |
| 5 | **无结尾换行时整条命令丢失** | 主循环在 `buf[i]=='\0'` 时直接退出，不冲刷挂起的词表与重定向目标。交互式输入（`input()` 剥离 `\n`）下 `echo hi` 和 `echo hi > f` 均无任何输出 |
| 6 | 符号表循环只查前 15 项 | `symbol[]` 共 22 项，`for(j<15)` 使 `{ } ( ) (( ))` 永不识别，`CMD_PART_*`/`CMD_SUBSHELL_*`/`CMD_MATH_*` 全部是死代码；`( echo sub )` 被拆成普通词 |
| 7 | `parse_deal_break_redir()` 的 `while(i>0)` 跳过第 0 项 | 只有一个重定向时 `break_type` 停留在 `CMD_RESERVED`，`>&2` 的 DUP 分支永不执行，`2` 被当作文件名；若 `data` 为空则 `data.arr[0]` 越界读 |
| 8 | 管道状态机失效 | `pipe_is_continue` 只在 `parse_deal_break(CMD_RESERVED)` 清零、**从未置 1**：`a \| b \| c` 产生 `CREATE_PIPE EXE(a) CREATE_PIPE EXE(b) EXE(c)`，既无 `CONTINUE_PIPE` 也无 `DELETE_PIPE`，且 `EXE(b)` 被排在管道组之外 |
| 9 | `parse_add_stype_loc()` 从下标 `loc` 开始遍历 data | data 数组下标与 type 下标不对应，插入 `BACKGROUND_START` 后部分 `loc` 值未 +1。`sleep 1 & echo done &` 中第二个命令的 EXECUTE 丢失参数（data 指向错误位置） |
| 10 | `is_redir` 范围错误 | 置位范围 `[CMD_REDIR_OUT, CMD_REDIR_HERE_DOCUMENT]`：漏掉 `<`（REDIR_IN）、`<<<`（HERE_STRING），使 `< f` 的目标变成普通命令参数；`>&-`（CLOSE）范围不含但本就不应接目标 |
| 11 | `#` 注释不要求词首 | `$#` 中 `#` 被当作注释起始，`$#` 解析成 `$` |
| 12 | 关键字判定缺词首条件 | 只要 `arg.size==0` 就查关键字：`x=if` 在扫描到 `if` 时会被误判为 `CMD_IF` |
| 13 | fd 判定不要求相邻 | `echo 2 > f` 中 `2` 被当作 fd 而不是 `echo` 的参数（shell 语义要求 `2>` 紧邻才算 fd） |
| 14 | `&&`/`||` 空参数时被吞掉 | `CMD_BREAK` 的 `if(*argraw==NULL) break;` 与 `CMD_AND/CMD_OR` 共用 case，导致 `a > f && b` 的 `&&` 丢失 |
| 15 | `parse_check()`/主循环对未闭合引号、`a \|` 悬空管道不报错 | 主循环已补：未闭合单引号/反引号、`|` 后无命令、`&` 前无命令均报 parse error 并返回 NULL |

### 2.3 越界 / 未定义行为（已修复）

- `parse_deal_break_redir()`：`cmds->data.size==0` 时 `i=(size_t)-1` 后 `data.arr[i]` 越界读；
  扫描未命中重定向时仍向 `data.arr[0]` 写入（可能把 `to_fd/to_file` 写进 `type_execute` 的 `argraw` 字段）。
- 主循环 `buf[i]=='\0'` 分支中 `i++` 后继续读 `buf[i]`（越界读）——已改为在循环条件处退出并统一冲刷。
- `parse_is_fd` 溢出：超长数字串导致 `int` 溢出（已加边界检查）。

### 2.4 内存泄漏（已修复）

原设计没有任何释放路径，每次解析都会泄漏：

1. 每个关键字断点（`if`/`then`/`fi`/`do`…）处的 `[NULL]` 参数缓冲泄漏（`da_init(&arg)` 丢弃不释放）；
2. 每次重定向断点：挂起的命令参数缓冲泄漏（命令本应进入 EXECUTE，见问题 4）；
3. 空 `CMD_BREAK`（连续换行/空命令）的 `[NULL]` 缓冲泄漏；
4. 错误路径：已生成的 `type_execute`/`type_redir` 结构及其参数全部泄漏；
5. fd 剥离后变空的参数缓冲泄漏（`2> f`、`2>&1`）。

修复：明确所有权——`type_execute.argraw`、`type_redir.to_file`（改为 `strdup` 拷贝）、
`type_redir.body`（here-doc 主体）均为解析器拥有；新增公开接口
`void parse_commands_free(commands_t *cmds)`（结构体本身由调用方 `free`，错误路径可用同一函数清理栈上结构）。
泄漏测试：代表脚本 2000 轮，free 模式 0 泄漏；对照组（不释放）每轮约泄漏 1.8 KB。

---

## 3. parser 下一阶段功能（本次已实现）

| 功能 | 说明 | 验证 |
|------|------|------|
| 命令+重定向正确关联 | 重定向操作码插入到其所属命令（尾部 EXECUTE 段最后一个）之前，fd 词剥离后命令仍完整 | `ls > f 2> g` → `REDIR_OUT(f), REDIR_OUT(fd=2,g), EXE[ls]` |
| EOF 冲刷 | 无结尾换行时刷新挂起命令、重定向目标、未闭合管道 | `echo hi`（无 `\n`）→ `EXE[echo,hi] BREAK` |
| fd 语法 | `n>`/`n>>`/`n>&`/`n<&`/`n>&-`，仅当数字与操作符紧邻；溢出保护 | `echo 2 > f` 中 2 是参数；`2>&1` → `DUP(fd=2,dup=1)` |
| 管道序列 | `CREATE_PIPE`→`CONTINUE_PIPE`→`DELETE_PIPE`；`a \|\n b` 续行；`a \|` 悬空报错 | `a\|b\|c` → `CREATE_PIPE EXE(a) CONTINUE_PIPE EXE(b) EXE(c) DELETE_PIPE BREAK` |
| 后台任务组 | `BACKGROUND_START/END` 插入位置修正（`parse_add_stype_loc` 全量遍历） | `sleep 1 & echo done &` 两组数据正确 |
| 子 shell/分组/数学 | 符号表 22 项全部生效；`{` `(` `((` 仅在命令位或紧邻函数名/`=` 时作为操作符；`}` `)` 仅在存在未闭合 opener 时作为操作符 | `( echo sub )`、`{ a; b; }`、`((1+2))`、`echo {a,b}` |
| `$` 扩展跳过 | `${...}`/`$(...)`/`$((...))`/`$'...'` 支持嵌套与引号/转义，并消费闭括号，保持整体为一个词 | `${a:-${b}}`、`$(echo (x))`、`$((1+2))` |
| here-doc | `<<EOF` 收集主体至分隔行（`body` 字段），分隔符支持引号（`<<'EOF'`）；EOF 截断报错 | `cat <<EOF\nhello\nthere\nEOF\n` → `body=[hello\nthere\n]` |
| here-string | `<<<` 目标正确归入重定向 | `cat <<<"hello world"` |
| case 结构 | `case 词 in` 识别 `IN`；`)` 结束模式（模式词发出为 EXECUTE+BREAK）；`;;` 保持双 BREAK | `case x in a) echo a;; esac` 流完整 |
| for/while/until | `for i in ...` 的 `IN` 关键字识别；`do`/`done` 发出 | `for i in 1 2 3; do echo $i; done` |
| 函数定义 | `name(`（紧邻、合法名）→ `FUNCTION` + `EXE[name]` + `PART_START..PART_END` 体 | `foo() { echo hi; }` |
| 关键字结构校验 | 栈式校验 `if..then..fi`、`for/while/until..do..done`、`case..esac`、`{}`/`()`/`(())` 配平；不匹配返回 NULL | `if ls` → parse error |
| 引号/注释 | 单引号、反引号死循环修复；未闭合引号报错；`#` 仅词首为注释 | `echo 'a b'`、`echo $#`、`# comment` |

### 下一阶段仍未实现（建议按此顺序）

1. **执行器**：解释 `commands_t` 操作码流（EXECUTE/REDIR/PIPE/AND/OR/BG/IF/FOR/CASE/FUNCTION…），
   当前 `main.c` 的 `parse_buffer` 是旧式 `command_t` 执行器，与新的操作码流不兼容，需整体替换或桥接；
2. **变量展开/别名/通配符**：`type_execute.argraw` 的词是原始文本（含引号、`$var`），需在执
   行前展开（可复用 `main.c` 的 `parse_variable` 等）；
3. **交互续行**：`main.c` 的 `parse_is_continue` 需感知关键字/管道/重定向（`if ls` 后应继续读入
   直到 `fi`），否则新解析器的结构校验会在交互模式误报；
4. **case 模式的 `(` 内嵌、`foo ()`（带空格）函数名、`x=(a b c)` 数组赋值的词内重组**；
5. **错误恢复**：解析错误后从下一个 `;`/换行继续，而不是整行丢弃；
6. **here-doc 内展开**（`<<EOF` 未加引号时主体可展开变量）、CRLF 处理、`\r` 剥离。

---

## 4. 其他文件的既有问题（未修复，建议处理）

### 4.1 mnsh.c

| # | 位置 | 问题 |
|---|------|------|
| 1 | `cmd_str_to_num()` | 与 `parse_is_fd` 同款 bug：`r.num=str[i]-'0'` 覆盖累加，`"123"` 解析为 3；影响 `$12` 位置参数、`%12` 作业号、`>&12` 等 |
| 2 | `set_env()`/`unset_env()` | 用“尾元素交换”删除，`variable.arr[i].env` 保存的下标会失效：`export A; export B; unset A; export C` 后环境表错乱 |
| 3 | `set_tmp_env()`/`recovery_tmp_env()` | `tmp_env.arr[i].var` 指向命令行的 `argv` 字符串，而 `parse_buffer()` 在执行后即 `free(argv[j])`，恢复时 `unset_var()` 对悬垂指针做 `strcmp`（use-after-free） |
| 4 | `add_job(now_name,...)` | `now_name` 随后被 `parse_buffer()` `free()`，`job.arr[i].name` 悬垂；`sh_jobs`/`deal_jobmsg` 打印已释放内存 |
| 5 | `execute_command_parent()` | `exit(WIFEXITED(status)?127:WEXITSTATUS(status))` 条件倒置：正常退出返回 127，被信号杀死的进程却取无意义的 `WEXITSTATUS` |
| 6 | `file_is_exist()` | `PATH` 内容逐字符写入固定 4096 字节的 `pathbuf`，无边界检查；超长 `PATH` 栈溢出（`pathbuf` 是全局） |
| 7 | `sh_cd()` 错误路径 | `chdir` 失败时 `strlen(argv[1])`，而 `argv[1]` 可能为 NULL（如 `cd` 且 HOME 未设置）→ 段错误 |
| 8 | `deal_jobmsg()` | `find_job_pid()` 返回 `(size_t)-1` 时直接 `job.arr[k]` 越界读；`jobmsgsiz` 在信号处理器中非原子递增 |
| 9 | `sh_bg()` | 循环内 `get_job_num(argv[1])` 应为 `argv[k]` |
| 10 | `get_job_num()` | `%+` 用 `job.size-1` 当下标而非最新作业号，作业删除后 `%+`/`%-` 解析失败 |
| 11 | `sh_history()` | 非选项参数报错信息误写为 `"cd: too many arguments"` |
| 12 | `parse_is_fd` 同款累加 | `restore_cmd_redir()` 中 `int fd=(size_t)r->_1` 的转换（有 `_1>=0` 保护，低危） |

### 4.2 main.c

| # | 位置 | 问题 |
|---|------|------|
| 1 | `parse_is_continue()` | `buf->arr[buf->size-1]`：空行时 `size==0` → `arr[(size_t)-1]` 越界读 |
| 2 | `execute_shebang()` | shebang 行无结尾换行时内层 `while(c!=' '&&c!='\0'&&c!='\n')` 在 EOF 后永不退出（`c` 不再变化）→ 死循环 |
| 3 | `execute_command_parent()` | 内置命令（除 echo）不 fork 直接执行，但文件重定向在子进程分支才打开 → `cd > file` 等重定向失效 |
| 4 | `parse_buffer()` | 与 mnsh.c#3/#4 同源的 `now_name` 生命周期问题 |

### 4.3 CDS/

- `input.h`：`static da_history history;`、`static da_str input_buffer;` 定义在头文件内，
  每个包含它的翻译单元各持一份（`main.c` 的 `history` 与 `mnsh.c` 的 `history` 是不同对象），
  历史功能实际不工作；`last_history()` 用 `strlen` 处理可能含嵌入 `\0` 的缓冲，长度计算错误。
- `darray.h`：`da_init` 用 `sizeof(darray_t(void))` 清零（行为正确但写法危险）；`da_resize` 缩小后
  保留旧数据，调用方需自行维护 `size`。

### 4.4 构建

- `make` 当前失败：22 个错误，全部源于 `mnsh.c` 使用已被 `mnsh.h` 删除的旧类型
  （`command_t`、`var_arr_t`、`var_int_t`、`var_func_t`、`VAR_ARR_SIZE`）与
  `input()` 签名不匹配（`sh_read` 调用 `input(echobuf)`）。仓库处于重构中途：
  新式 `CDS/` + `parser.c`（独立可测）与旧式 `main.c`/`mnsh.c` 执行器并存。

---

## 5. 验证

### 5.1 测试驱动

- `ptest.c`：60+ 用例（每例在子进程内运行并带 3 秒超时，捕获死循环），覆盖
  基础命令/重定向（含 fd、dup、close、here-doc、here-string）/管道/后台/逻辑运算/
  关键字结构/case/函数/子 shell/数学/引号/注释/负向用例。
- `leaktest.c`：代表脚本 2000 轮 × free / nofree 两模式。
- 编译：`cc -std=gnu99 -O0 -g -fsanitize=address,undefined`。

### 5.2 结果

| 项 | 修复前 | 修复后 |
|----|--------|--------|
| 死循环用例 | 5 个挂死（`2>`、`'`、`` ` `` 相关） | 0 |
| ASan/UBSan 报错 | —（挂死前未见，泄漏存在） | 0 |
| 泄漏（2000 轮 free 模式） | 每轮 ≥3 处 | 0 |
| `test.sh` 解析 | 命令缺失、重定向错乱 | 结构正确（`if→EXE→BREAK→then→REDIR→EXE→fi`…） |

`test.sh` 片段示例（修复后）：
```
0 14            IF
1 1             EXECUTE [ls]
2 2             BREAK
3 15            THEN
4 8             REDIR_OUT (fd=-1 file=test)
5 1             EXECUTE [ls]
6 17            FI
```

### 5.3 复现方式

```sh
cc -std=gnu99 -O0 -g -fsanitize=address,undefined -o ptest ptest.c && ./ptest
cc -std=gnu99 -O0 -g -fsanitize=address      -o leaktest leaktest.c && ./leaktest free
```

---

## 6. 文件变更

| 文件 | 变更 |
|------|------|
| `parser.c` | 整体重写：修复 2.1–2.4 全部问题，实现第 3 节功能；新增 `parse_commands_free()`；`type_redir` 增加 `body` 字段（here-doc 主体） |
| `test.c` | 解析后调用 `parse_commands_free(cmd); free(cmd); free(buf);`（原样泄漏） |
| `ptest.c`（新增） | 解析器功能测试驱动（60+ 用例，子进程超时防挂死） |
| `leaktest.c`（新增） | 内存所有权泄漏测试（free/nofree 双模式） |
| `REPORT.md`（本文件） | 审查报告 |

> 注：`parser.c` 当前通过 `test.c`/`ptest.c` 直接 `#include "CDS/darray.c"` 方式编译，
> 尚未加入 `Makefile`；接入主程序与实现操作码流执行器是下一阶段的首要任务。
