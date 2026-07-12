# 09. Object 服务：从 `dkey/akey` 到对象类与数据保护

## 为什么 Object 这一篇承上启下

写到这里，DAOS 的阅读顺序其实已经进入一个很自然的阶段：

- 我们知道 pool 是资源边界。
- 我们知道 container 是事务与版本视图边界。
- 我们知道 gRPC、dRPC、CART 分别服务哪条通信边界。
- 我们也知道数据最终会落到 VOS 这种本地版本化对象存储里。

接下来，最应该进入的就是 object。

因为对大多数用户来说，真正和 DAOS 数据模型发生接触的第一层，不是 pool，也不是 container，而是 object：

- 我怎么组织 key？
- 数据怎么分布？
- 冗余怎么做？
- 什么时候复制，什么时候 EC？
- checksum 又是在哪一层和复制/纠删码配合起来的？

所以这一篇的意义很特别。

它既是“用户视角的数据模型”解释篇，也是“源码实现如何把这套模型落地”的承上启下篇。

这一篇我会回答三个问题：

1. `dkey/akey` 这种二级 key 模型到底解决了什么问题？
2. object class 如何决定分布、复制和纠删码？
3. checksum、replication、EC、placement、VOS、DTX 在 object I/O 栈里怎么协同？

## 先给结论：DAOS object 不是文件，也不是表，而是可分布的最小数据模型单元

`src/object/README.md` 一开头就把 object 的定位说得很直接：

- DAOS object 存储用户数据
- 它由 object ID 标识
- object ID 在所属 container 内唯一
- object 可以分布到 pool 的任意 targets 上，以获得性能和弹性

这里最重要的不是“对象”这个词，而是它背后的三个特征：

- **有稳定身份**：由 OID 标识
- **可跨 target 分布**：不是天然绑定单点
- **受 object class 支配**：分布和保护方式不是后补的，而是对象身份的一部分

更关键的是，README 还强调了一点：

- DAOS object 被刻意设计得非常简单
- 系统默认不维护访问时间、大小、owner、permission，也不跟踪 opener

这说明 object 层的设计目标非常克制：

- 不把高层文件语义都压进来
- 把 object 保持成一个高性能、低开销、可分布的数据模型单元

所以最适合记住的一句话是：

**在 DAOS 里，object 是数据分布、数据保护和 I/O 执行栈真正围绕的核心单元。**

## 第一层：为什么 `dkey/akey` 不是“多此一举”

第一次接触 DAOS object 时，最容易觉得奇怪的就是二级 key：

- `dkey`
- `akey`

为什么不直接一个 key 到 value？

### README 已经点出核心语义

`src/object/README.md` 对这两个 key 的描述很关键：

- `dkey` 是 distribution key
- `akey` 是 attribute key
- 同一个 `dkey` 下的所有 entries 保证 colocated 在同一个 target 上

这句话其实已经把设计动机说透了。

它说明 DAOS 并不是为了“让 API 看起来复杂一点”才拆成 `dkey/akey`，而是为了把两件不同的事情显式分开：

- `dkey` 决定放置与局部性
- `akey` 决定局部命名空间里的数据组织

### `dkey` 解决的是“放哪儿”的问题

如果一组数据天然应该被放在同一个 target 上一起处理，那么最自然的做法就是让它们共享一个决定放置的 key。

DAOS 把这个角色交给了 `dkey`。

也就是说，`dkey` 的本质不是“第一层 key”，而是：

- 一个显式暴露给用户的数据局部性控制点

这很重要，因为对象系统一旦进入分布式环境，光有一个单层 key 往往不够表达：

- 哪些数据应该同放
- 哪些数据应该分散

而 `dkey` 恰好把这件事拿到了模型层。

### `akey` 解决的是“在这个局部性单元里怎么组织”的问题

当一个 `dkey` 把一批数据放到了同一个 target 之后，`akey` 再负责描述：

- 这个局部集合里有哪些属性项
- 每个项是 single value 还是 array value

也就是说：

- `dkey` 更接近分布边界
- `akey` 更接近局部结构边界

### 这种拆法的实际好处

把单层 key 拆成 `dkey/akey`，至少带来三个直接收益。

#### 1. 显式建模局部性

同一个 `dkey` 的数据会被 colocate。

这让应用可以自己控制哪些数据应该一起落在同一 target 上，避免完全把局部性赌博给底层散列函数。

#### 2. 把“分布”和“属性组织”分层

如果只有一层 key，那么：

- 分布语义
- 属性语义

会耦在一起。

而现在：

- `dkey` 管分布
- `akey` 管属性

分层更清晰。

#### 3. 更适合 array / KV 混合模型

README 还说：

- value 可以是 atomic single value
- 也可以是 array value

有了 `akey` 这一层后，一个 `dkey` 下就可以自然组织多个 value 项，且每个项还能有不同 value 形态。

所以 `dkey/akey` 的真实价值，不在于“多了一层索引”，而在于：

**它把数据局部性和局部组织同时纳入了 object 模型。**

## 第二层：Object Type 解决的不是保护问题，而是数据形态问题

`src/object/README.md` 接着讲了 object type。

这一层很容易和 object class 混淆，但它们其实完全不是一回事。

### object type 决定 key/value 语义

README 里说：

- object type 定义 key type，有时也定义 value type
- 引擎据此优化底层存储和 key 枚举顺序

例如：

- 默认 `DAOS_OT_MULTI_HASHED` 不约束 akey/dkey 类型
- KV object 可以绕过 akey，只允许 single value
- array object 会在 integer dkey 下存 array chunks

这说明 object type 关注的是：

- key 是 lexical 还是 integer
- value 是 single 还是 array
- 引擎应如何看待这个 object 的逻辑形态

所以 object type 更像“数据模型类型信息”。

### object class 则是另一回事

如果 object type 解决“这个 object 长什么样”，那么 object class 解决的就是：

- 这个 object 怎么分布
- 这个 object 怎么保护

这两层一定要分清。

## 第三层：Object Class 为什么是 object 层最关键的概念

### object class 同时编码分布与保护

`src/object/README.md` 对 object class 的定义非常直接：

- object class 描述对象的 distribution 和 protection method
- 它由 class ID 和 group 数共同决定

也就是说，对 DAOS 来说，一个 object 不是“先有数据，再决定复制方式”，而是：

- 从 OID 生成那一刻起，它的分布和保护方法就被确定下来

这和很多高层存储系统的思路很不一样。

### 命名规则其实很值得掌握

README 对 object class naming conventions 的解释很实用。

几个关键记忆点：

- `OC`：Object Class
- `RP`：Replication
- `EC`：Erasure Code
- `G`：Redundancy Group
- `S{n}`：无保护时的 shard 数
- `X`：尽可能铺满更多 targets / engines

例如：

- `OC_SX`：无保护，尽量在更多 targets 上分布
- `OC_RP_2GX`：2 副本复制，尽量多 redundancy groups
- `OC_EC_4P2G1`：4+2 EC，一个 redundancy group

这个命名体系之所以重要，不只是因为类多，而是因为它把几个核心维度全编码进名字里了：

- 有没有保护
- 用什么保护
- 一个冗余组里有多少 shard
- 一共有多少组

所以 object class 名字本身就是一张“布局说明书”。

## `daos_obj_generate_oid()` 为什么这么关键

### OID 不是随便生成的 ID

`src/object/README.md` 明确写到：

- object class ID 和 number of groups 会被嵌入 OID
- DAOS 使用这些编码信息生成 object layout

这点特别关键。

它意味着 OID 在 DAOS 里不只是“唯一标识符”，还是：

- 对象布局决策的输入
- 对象保护方式的编码载体

也就是说，OID 不是纯身份号，而是“身份 + 布局约束”的组合。

### `cli_obj.c` 里的实现验证了这一点

在 `src/object/cli_obj.c` 中，`daos_obj_generate_oid()` 的逻辑很清楚：

1. 先通过 container handle 找到 pool handle。
2. 读取 container properties。
3. 结合 pool placement 信息 `pl_map_query(...)` 获取 domain/target 能力。
4. 如果 class 未指定为 `OC_UNKNOWN`，就根据显式 class 去 fit。
5. 否则调用 `dc_set_oclass(...)` 根据：
   - RF
   - domain 数
   - target 数
   - object type
   - hints
   自动选择合适的 `ord` 和 `nr_grp`
6. 最后调用 `daos_obj_set_oid(...)` 把 type、redun、groups 等编码进 OID。

这个流程把 README 里那段“自动 class 选择”真正落到了代码上。

### 这段设计说明了什么

这段逻辑至少说明了三件事。

#### 1. OID 生成依赖 container / pool 上下文

OID 不是纯本地随机生成，因为 class 选择要依赖：

- container 的冗余属性
- pool 的 placement 能力

这说明 object 从出生起就和上层资源与容器属性绑定。

#### 2. class 可以显式选，也可以自动选

高级用户可以直接指定 class。

而普通用户把 class 设成 `OC_UNKNOWN`，DAOS 会根据：

- object type
- RF
- domain 数量
- hints

自动选出一个更合适的 class。

#### 3. 分布与保护是对象身份的一部分

因为最终这些信息会被编码进 OID。

所以 object 的布局不是后期动态猜出来的，而是对象身份的一部分。

## Object Class 和 Placement 是怎么接上的

### placement 解决的是“具体放到哪些 fault domain / targets”

`src/placement/README.md` 对 placement 的描述很清楚：

- DAOS 用 pool map 派生 placement maps
- 再根据 object ID、object schema 和 placement map 算法性生成 object layout

也就是说：

- object class 说明“要什么样的布局和保护”
- placement 负责“在当前 pool map 里具体挑哪些位置”

### 为什么需要 placement map

placement map 本质上是 pool map 的抽象和排列视图。

它的目标是：

- 既利用 pool 的真实拓扑
- 又避免每次布局都直接重扫完整 pool map 细节

因此，object layout 的真实形成过程可以粗略理解成：

1. object type / class 决定逻辑要求。
2. OID 编码携带这些要求。
3. placement map 基于 pool map 和 OID 算出具体 shards 落点。

### 这一层回答了 `dkey` 的“放置含义”

前面我们说 `dkey` 决定 colocate 边界。

而 object class + placement 则决定：

- 这个 object 总共有多少 shards / redundancy groups
- 每个 group 最终能落到哪些 fault domains

所以 object 层的放置逻辑，其实是两层叠加的：

- `dkey` 决定一个局部键组落到 object layout 的哪部分
- object class + placement 决定 object layout 本身长什么样、分布在哪里

## replication、EC、checksum 为什么都在 object 层会合

### object 层是数据保护语义真正开始发生的地方

`src/object/README.md` 在 Data Protection Method 一节里直接说：

- DAOS 支持 replication 和 EC
- checksum 可以和两者配合，提供 end-to-end integrity

这意味着 object 层不是简单转发 I/O 到 VOS，而是：

- 负责把用户的对象访问请求转成具有保护语义的执行流程

也就是说，真正“把保护方案变成 I/O 行为”的地方，就是 object 层。

## replication：为什么 README 强调 leader shard

README 对 replication 的服务端过程讲得很直白：

- client 选 leader shard 发送请求
- leader 转发到其他 shards
- leader 本地执行
- 等待其他 shard 完成，再回复客户端

这说明 replication 在 object 层的关键不是“多写几份”，而是：

- 有明确 leader
- 由 leader 组织复制执行
- 冲突写在 leader 处被检测和串行化

所以 replication 不是客户端盲目 fan-out，而是 object 层带 leadership 的 server-side replication。

### `obj_rpc.h` 也能看出这条栈的存在

在 `src/object/obj_rpc.h` 里可以看到：

- `DAOS_OBJ_RPC_UPDATE`
- `DAOS_OBJ_RPC_TGT_UPDATE`

这两个 opcode 并存，本身就说明 object 更新既有面向客户端的入口，也有 target/shard 间的内部传播路径。

这和 README 里的 leader-forward 模型是对得上的。

## EC：为什么 object 层必须理解条带和 parity group

### EC 不是 VOS 自己能决定的

README 里对 EC 的介绍虽然简短，但它已经说明：

- EC 是 object class 决定的数据保护方式
- parity/data shard 的组织方式是对象布局的一部分

这意味着 EC 的关键逻辑天然在 object 层，而不是只在 VOS 层。

因为只有 object 层同时知道：

- object class
- shard 角色
- parity/data 关系
- placement 落点

### `obj_mod_init()` 里也能看到 EC 是 object 模块原生能力

在 `src/object/srv_mod.c` 里，`obj_mod_init()` 会初始化：

- `obj_class_init()`
- `obj_ec_codec_init()`

这说明：

- class 和 EC codec 都是 object 模块原生要准备的基础能力

object 模块不是被动接受“外部给了我个 EC 请求”，而是自身就把 EC 作为对象 I/O 栈的一部分来初始化。

## checksum：为什么不是独立功能，而是 object I/O 栈的一部分

### README 已经把 checksum 流程讲透了

`src/object/README.md` 对 checksum 的描述非常好，因为它明确把 client、server、VOS 三层都串起来了。

#### update 时

- client 先计算 checksums
- 随 IOD 一起发给 server
- server 再把 checksum 和数据一起写入 VOS

#### fetch 时

- server 从 VOS 取回 checksum
- 如果 array extent 与请求不完全对齐，可能要重新计算新 checksum
- client 再对返回数据做 checksum verify

也就是说，checksum 不是一个“旁路插件”，而是 object update/fetch 流程本身的一部分。

### `obj_rpc.h` 也验证了 checksum 是 RPC 协议的一部分

在 `DAOS_ISEQ_OBJ_RW` 里就能看到：

- `orw_dkey_csum`
- `orw_iod_array`

在返回结果里还能看到：

- `orw_iod_csums`

这说明 checksum 信息并不是局部隐藏状态，而是明确进入 object RPC 输入输出的数据结构。

### `srv_obj.c` 进一步说明 server 端确实在 object 层处理 checksum

在 `src/object/srv_obj.c` 里可以看到很多直接相关逻辑，例如：

- `obj_fetch_csum_init(...)`
- `csum_add2iods(...)`
- `obj_verify_bio_csum(...)`
- `ds_obj_rw_handler(...)` 中对 checksum report 和 fetch/update checksum 路径的处理

这说明 server object 层不仅传递 checksum，还真正参与：

- checksum 初始化
- checksum 拷贝或重算
- checksum 校验

所以 checksum 与 object 层的关系可以概括成一句话：

**checksum 是 object I/O 协议和 object 执行路径的一部分，而不是 VOS 或 client 单边的附属功能。**

## `ds_obj_rw_handler()`：object I/O 栈真正开始转起来的地方

### 这是 update/fetch 的总入口

`src/object/obj_rpc.h` 已经说明：

- `DAOS_OBJ_RPC_UPDATE` 和 `DAOS_OBJ_RPC_FETCH` 都由 `ds_obj_rw_handler` 处理

而 `src/object/srv_obj.c` 里的 `ds_obj_rw_handler()` 一开头就做了最关键的上下文建立：

- `obj_ioc_begin(...)`

这里会把：

- oid
- pool UUID
- container handle
- container UUID
- map version
- rpc flags

装配成 object I/O context。

这一步很重要，因为它说明 object 请求一进服务端，首先要把自己放回：

- 哪个 pool
- 哪个 container
- 哪个 layout / map version
- 哪个目标上下文

### fetch 和 update 在这里分叉

`ds_obj_rw_handler()` 中：

- fetch 路径会处理 fetch epoch、DTX begin/end、本地 `obj_local_rw(...)`
- update 路径则会进入更复杂的 leader / replica / DTX / resend / membership 逻辑

这说明 object I/O 栈的核心复杂度，不在“读写 API 名字不同”，而在于：

- update 需要真正推动分布式一致修改
- fetch 需要在版本视图、校验和布局约束下选择可读数据

### DTX 也在 object 层会合

从 `ds_obj_rw_handler()` 能直接看到：

- `dtx_begin(...)`
- `dtx_end(...)`
- `obj_gen_dtx_mbs(...)`
- resend / piggyback / conflict 相关逻辑

这说明 object 层并不只是“把 I/O 发到底层”，它还承担着：

- 分布式事务上下文的组织
- shard 间成员集合处理
- 重试和幂等执行语义

所以 object 层其实正是：

- 用户数据模型
- 布局/保护
- 分布式事务

三者真正相交的地方。

## object 和 VOS 的关系：谁负责什么

### VOS 负责本地版本化对象存储

从前一篇我们已经知道：

- VOS 是本地 shard 上的 versioning object store

它负责：

- 本地对象数据与索引的版本化存储
- epoch history
- 本地持久元数据与校验

### object 层负责把全局 object 语义翻译成本地 I/O

而 object 层负责的是：

- 根据 OID / class / placement 知道该访问哪些 shards
- 根据 replication / EC 选择执行模式
- 根据 DTX / epoch / checksum 组织请求
- 再把局部请求落到 VOS

所以两者关系可以这么记：

- VOS 管本地“怎么存”
- object 层管全局“怎么分、怎么保、怎么执行业务 I/O”

这也是为什么 object 层必须理解：

- class
- shard
- placement
- DTX
- csum

而不仅仅是调用几个 VOS 接口。

## 一个更实用的阅读方法：把 object 层拆成四层

以后你再读 `src/object` 时，最实用的方法不是直接陷进 `srv_obj.c` 巨大的 I/O 细节里，而是先判断自己现在在看哪一层。

### 第一层：逻辑模型层

看的是：

- object
- `dkey`
- `akey`
- single / array value
- object type

这一层回答“用户到底在操作什么模型”。

### 第二层：布局层

看的是：

- object class
- OID 编码
- placement map
- shard / redundancy group

这一层回答“这个 object 在系统里怎么摊开”。

### 第三层：保护层

看的是：

- replication
- EC
- checksum

这一层回答“这个 object 怎么被保护起来”。

### 第四层：执行层

看的是：

- `ds_obj_rw_handler()`
- DTX
- resend
- local rw
- target update / fetch

这一层回答“真正的 I/O 请求怎么跑起来”。

只要先把这四层分清，`src/object` 会好读很多。

## 小结

Object 层之所以是 DAOS 源码阅读里的关键节点，是因为它把前面几篇分散建立的概念真正拧在了一起：

- pool 提供资源边界
- container 提供事务与版本视图边界
- placement 提供布局算法
- VOS 提供本地版本化存储

而 object 层则把这些能力组织成了用户真正感知到的数据模型和 I/O 栈。

所以理解 object 最重要的不是记住所有 opcode，而是记住这条主线：

**`dkey/akey` 定义数据局部性与局部结构，object type 定义数据形态，object class 定义分布与保护，OID 编码把这些约束带进对象身份，object I/O 栈再结合 placement、DTX、checksum、replication/EC 把请求真正跑起来。**

如果把这篇压缩成一句话，那就是：

**在 DAOS 里，object 是“数据模型 + 布局策略 + 保护策略 + I/O 执行”的交汇点。**

## 下一篇看什么

现在正好可以回头把前面多次出现但还没专门展开的一条底层主线写透：

**RDB 与 RSVC：DAOS 的强领导复制元数据框架**

因为理解完 object 之后，再回头看 pool/container 为什么能把元数据做成高可用复制状态，会更容易把服务层与数据层区分清楚。
