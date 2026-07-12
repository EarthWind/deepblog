# Ceph Dashboard 架构与实现：管理界面、REST API 与前后端协作

## 这篇文章要解决什么问题

上一篇我们已经讲清楚了：

- `MGR` 为什么存在
- 它为什么要做管理平面
- 它为什么适合承载 Python 模块生态

那接下来最自然的问题就是：

**Ceph Dashboard 这个用户最容易感知到的管理界面，到底在代码里是怎么组织起来的？**

很多人第一次接触 Dashboard 时，直觉上会把它想成：

- 一个独立的 Web 管理系统
- 一个“连到 Ceph 上”的外部前端
- 一个纯前端页面，后面只是顺便调几个接口

这些理解都不够准确。

Ceph 仓库文档对它的定位其实说得很清楚：

- Dashboard 是一个 web-based Ceph management-and-monitoring tool
- 它实现为 `ceph-manager` 的一个模块
- 后端基于 CherryPy，自定义 REST API
- WebUI 基于 Angular/TypeScript

如果把这些信息压缩成一句话，那就是：

**Ceph Dashboard 不是一个独立外挂系统，而是一个运行在 `ceph-mgr` 里的 Web 应用：后端是 Python + CherryPy，前端是 Angular 单页应用，中间通过一套自定义 REST API 协作。**

这一篇就专门把这套结构讲清楚。

## 先建立第一条边界：Dashboard 不是“独立控制面”，而是 `MGR` 的一个模块

理解 Dashboard 的第一步，不是先看前端，也不是先看页面，而是先把它放回 Ceph 的总体架构里。

Dashboard 本质上属于：

- `src/pybind/mgr/dashboard`

也就是说，它首先是：

- 一个 `ceph-mgr` 模块

然后才是：

- 一个 Web 服务
- 一个 REST API 后端
- 一个 Angular 前端

这条边界非常重要，因为它直接决定了 Dashboard 的系统位置：

### 它不直接维护权威状态

权威状态仍然在 `MON` 和其他核心守护进程里。

### 它也不直接承担普通数据 IO

普通读写主路径仍然在客户端、消息层、OSD、PG 和 BlueStore。

### 它主要做的是“把集群变成可视化、可操作、可管理的系统”

所以 Dashboard 最核心的价值不是：

- 替代 Ceph 本体

而是：

- 给 Ceph 管理平面提供图形化和服务化出口

## 第二张图：Dashboard 的整体结构

先建立一张总览图：

```text
浏览器
   |
   v
Angular 前端
   |
   v
Dashboard REST API
   |
   v
CherryPy 控制器层
   |
   v
Dashboard services / mgr API / OrchClient / Prometheus / Grafana
   |
   v
ceph-mgr 模块运行时
```

这张图里最重要的，不是每一层的名字，而是它表达出来的分工：

### 前端

- 页面、路由、交互、组件

### 后端控制器层

- HTTP 路由、认证、请求参数解析、响应封装

### 服务层

- 把控制器里的动作转成对 Ceph 管理能力、Prometheus、Grafana、orchestrator 的实际调用

### `ceph-mgr` 模块运行时

- 提供 Dashboard 运行环境和集群状态上下文

你可以先记一句话：

**Dashboard 不是“前端直连集群”，而是“前端调用 Dashboard 后端，Dashboard 后端再站在 `MGR` 平台之上组织管理能力”。**

## 从官方文档定义开始：Dashboard 是 `ceph-mgr` 托管的 Web 应用

`doc/mgr/dashboard.rst` 对 Dashboard 的定位非常明确：

- 它是基于 Web 的 Ceph 管理与监控工具
- 它实现为 `ceph-mgr` 模块
- backend 使用 CherryPy
- WebUI 使用 Angular/TypeScript
- 最终由 `ceph-mgr` 托管成一个 Web server

这里最值得抓住的不是技术栈名字，而是那句：

- “hosted by `ceph-mgr`”

这意味着：

- Dashboard 不是额外再起一个完全独立的后端进程
- 它是挂在 `MGR` 运行时里的

也正因为如此，上一篇讲的 `MGR` 模块运行时、状态访问能力、active/standby 语义，在 Dashboard 这里全部会真正落地。

## 第 1 层：模块入口 `module.py` 才是 Dashboard 的真正起点

如果你要从源码第一眼进入 Dashboard，最值得看的文件不是前端入口，而是：

- `src/pybind/mgr/dashboard/module.py`

这是因为：

- 它是 Dashboard 作为 `ceph-mgr` 模块的入口

### 为什么这里比 `main.ts` 更重要

因为系统上真正先发生的事情不是：

- “浏览器打开 Angular”

而是：

- `ceph-mgr` 先把 Dashboard 模块启动起来
- Dashboard 后端先把 Web 服务和 API 路由准备好
- 然后浏览器才有东西可以访问

这也是为什么理解 Dashboard 时，先看模块入口比先看前端入口更稳。

## `Module.serve()`：Dashboard 后端真正开始运转的地方

在 `module.py` 里，最关键的入口之一就是：

- `Module.serve()`

如果把这段启动流程翻译成架构语言，大致是在做下面几件事：

### 1. 初始化认证相关状态

比如：

- `AuthManager`
- SSO 配置与数据库

### 2. 准备 CherryPy 配置

比如：

- SSL
- gzip
- 全局工具
- JSON 处理
- 错误包装

### 3. 生成和挂载路由

不是手工一个个 URL 写死，而是统一收集控制器并生成路由表。

### 4. 启动 Web 服务

也就是让 CherryPy 真正开始对外提供 HTTP 服务。

### 5. 检查并服务前端静态资源

也就是最后让浏览器能够拿到 Angular 构建产物。

所以 `Module.serve()` 本质上做的是：

**把一个 `MGR` 模块，装配成一个可工作的 Web 应用。**

## 第 2 层：CherryPy 为什么是 Dashboard 后端的中心骨架

很多人对 Dashboard 的后端会先有个误解：

- 觉得它只是一些零散 Python 文件加几个 API

但实际上，它是建立在一个明确的 Web 框架之上的：

- CherryPy

这点在文档和 `module.py` 里都很清楚。

### CherryPy 在这里承担什么

可以把它理解成 Dashboard 后端的 Web 容器，负责：

- HTTP 服务生命周期
- request/response 流转
- 工具链挂载
- 路由分发
- 中间处理逻辑

也就是说，Dashboard 后端不是“裸 Python HTTP 处理”，而是：

- 在 CherryPy 上装配的一层 Ceph 管理 API 应用

### 为什么这点重要

因为你后面看到的很多能力，比如：

- 认证工具
- JSON 输入输出
- 控制器 dispatch
- 静态文件服务

都不是散乱实现，而是围绕 CherryPy 这层骨架组织起来的。

## 第 3 层：Dashboard 的 URL 空间不是平铺的，而是分成三块

理解 Dashboard 路由时，最重要的不是记所有路径，而是先知道它按用途分成了三类：

### 1. `/api`

- 主要面向业务 REST API

### 2. `/ui-api`

- 主要面向前端 UI 辅助接口

### 3. `/`

- 静态页面和前端构建产物

这套结构来自几个非常关键的装饰器：

- `Router`
- `APIRouter`
- `UIRouter`

### 为什么这套拆分很合理

因为 Dashboard 并不只是一个“API 服务”，它同时还承担：

- 前端静态资源托管
- UI 辅助数据输出
- 业务 API 暴露

如果不把这些入口分开，整个 URL 空间会很快变得混乱。

所以可以先把它理解成：

- `APIRouter` 负责正式业务 REST 接口
- `UIRouter` 负责前端配套接口
- `Router` 负责更通用或非 API 的路径

## `Router / APIRouter / UIRouter`：把控制器类声明成 URL 空间

在 Dashboard 里，路由并不是一堆手工 if/else 或字符串表，而是建立在控制器类装饰器上的。

这套设计的好处是：

- 路由声明和控制器定义靠得很近
- 控制器天然带作用域和认证配置
- 不同类型的入口可以统一管理

### 一个非常典型的例子

比如：

- `@APIRouter('/auth', secure=False)`
- `@APIRouter('/grafana', Scope.GRAFANA)`
- `@UIRouter('/langs', secure=False)`
- `@Router('/', secure=False)`

这些声明本质上就是在说：

- 这个控制器属于哪条 URL 空间
- 默认是否需要认证
- 它归属哪个安全 scope

也就是说，Dashboard 的路由组织方式不是“路径先行”，而是：

- 控制器先行，路径由控制器元数据生成

## 第 4 层：控制器不是手工散装函数，而是统一基于 `BaseController`

如果你继续往下看，会发现 Dashboard 控制器并不是随便写几个函数就暴露成 API。

最基础的抽象是：

- `BaseController`

它负责的事情包括：

- 控制器元数据收集
- endpoint 定义
- 参数处理
- 响应封装
- 路由生成配合
- 版本相关处理

这一步很重要，因为它说明 Dashboard 后端不是“简单脚本型 API”，而是：

- 一个有明确控制器框架的 Web 应用

也正因为有了 `BaseController` 这层，后面才可以在它上面继续做：

- 通用 endpoint 抽象
- REST 风格控制器抽象

## `RESTController`：为什么很多 Dashboard API 能写得像“资源操作”

在 Dashboard 后端里，非常值得专门讲的一个抽象是：

- `RESTController`

如果你看它的实现，会发现它把很多资源操作语义统一映射成：

- `list`
- `create`
- `get`
- `set`
- `delete`
- `update`

以及对应的：

- `GET`
- `POST`
- `PUT`
- `PATCH`
- `DELETE`

### 这层抽象的价值是什么

它让很多控制器不必手工重复写大量样板代码，而可以更像是在声明：

- 我这个资源有哪些 CRUD 语义

比如用户控制器这样的资源型接口，就很适合走这条路径。

所以可以把 `RESTController` 理解成：

**Dashboard 把“Ceph 管理操作”组织成资源型 REST API 的关键抽象层。**

## 第 5 层：控制器自动发现，而不是人工集中注册

Dashboard 的另一个很漂亮的设计是：

- 控制器不是在某个总文件里一个个手工注册

而是：

- `BaseController.load_controllers()` 去扫描 `controllers/*.py`
- 然后自动导入非下划线模块
- 最后统一生成路由

### 为什么这点非常适合模块化系统

因为 Dashboard 的控制器非常多，覆盖：

- 用户与认证
- OSD、pool、RBD、CephFS
- Grafana、Prometheus
- orchestrator
- 配置与日志
- 监控与告警

如果这些东西都靠手工集中注册，扩展性和维护性都会变差。

自动发现的好处是：

- 新控制器更容易扩展
- 路由体系更一致
- 文档生成和权限控制也更容易共享元数据

## 第 6 层：认证不是散落在各个 API 里，而是挂在 CherryPy tool 上集中处理

Dashboard 既然是管理界面，就一定要有完整认证链路。

而它在架构上的一个很重要特点是：

- 认证不是每个控制器自己手工校验

而是通过 CherryPy tool 统一前置处理。

这里最关键的对象之一就是：

- `AuthManagerTool`

### 这意味着什么

意味着一个请求进入 Dashboard 时，通常不是先跑业务逻辑，而是先经过：

- token 提取
- 用户识别
- scope 判断
- permission 判断

再决定是否继续进入具体 handler。

这是一种非常典型、也非常合理的 Web 应用设计：

- 认证鉴权作为通用前置中间层
- 业务控制器只关注自身业务

### 这里最值得强调的一点

Dashboard 的控制器天然会带：

- security scope
- endpoint permission

所以认证工具并不是“只判断有没有登录”，而是能进一步判断：

- 这个用户是否有权访问这个 scope 下的这个操作

这让 Dashboard 真正具备了管理系统需要的权限边界。

## JWT、本地登录、SSO：Dashboard 的认证入口为什么不止一种

如果你继续看认证相关代码，会发现 Dashboard 不只是简单用户名密码登录。

它的认证体系同时支持：

- 本地用户登录
- JWT token
- OAuth2
- SAML2

### 本地登录入口

一个非常直接的入口是：

- `controllers/auth.py`

这里通过：

- `POST /api/auth`

发起登录，成功后签发 JWT，并把 token 写到 cookie。

### 为什么这条链路很重要

因为它说明 Dashboard 的认证不是“前端本地存个状态”那么简单，而是完整的：

- 用户认证
- token 生成
- cookie 写入
- 后续请求再由鉴权工具校验

### SSO 为什么也合理

因为 Dashboard 是面向运维和管理的 Web 系统，企业环境里常常需要：

- 外部身份提供方
- 单点登录

所以 OAuth2 和 SAML2 并不是“额外花活”，而是企业管理界面非常自然的需求。

## 第 7 层：首页控制器 `HomeController` 负责前端静态资源托管

很多人以为 Dashboard 的前端是一个完全独立部署的静态站点，但从源码看，更准确的情况是：

- Dashboard 后端本身也负责托管前端构建产物

这里最值得看的入口就是：

- `controllers/home.py`

### 它做了什么

它会：

- 从 `mgr.get_frontend_path()` 找前端构建目录
- 根据 `Accept-Language` 或 cookie 选择语言目录
- 默认回退到 `index.html`
- 通过 CherryPy 静态文件能力返回前端资源

这说明 Dashboard 前后端不是“两套彼此无关的部署物”，而是：

- 后端模块启动后，同时把前端静态资源一起服务出来

这也是为什么我们前面一直强调：

- Dashboard 是 `ceph-mgr` 托管的 Web 应用

## 第 8 层：前端入口不是“页面集合”，而是 Angular 单页应用

现在才轮到前端入口。

Angular 侧最直接的启动点是：

- `frontend/src/main.ts`

它会：

- `bootstrapModule(AppModule)`

也就是说，Dashboard 前端从结构上看是一套标准 Angular 单页应用。

### 为什么这一步值得单独讲

因为很多人对 Ceph Dashboard 的印象还停留在：

- “一个管理页面”

但从代码结构上看，它其实是一套比较完整的前端应用工程：

- Angular CLI 项目
- 根模块
- 路由模块
- 业务模块
- 共享组件和共享 API 封装

所以如果你从仓库结构去看，就会发现它不是“几个 HTML 页”，而是一整套现代前端工程。

## 第 9 层：Angular 前端按 `core / ceph / shared` 分层

如果你进入：

- `frontend/src/app`

会看到一个非常典型、也非常适合大型前端的分层方式：

### `core`

- 应用壳子、布局、登录、导航、错误页

### `ceph`

- Ceph 业务页面本体

比如：

- dashboard
- cluster
- cephfs
- nfs
- rgw
- block
- smb

### `shared`

- API service
- 通用组件
- 模型
- 表格
- 工具类

这套分层很值得在博客里讲出来，因为它体现了 Dashboard 前端不是“按页面随便堆组件”，而是：

- 有明确壳层、业务层、共享层

这使得它能够承载越来越多的 Ceph 管理功能，而不至于完全失控。

## `AppRoutingModule`：Dashboard 前端怎么把管理能力组织成导航空间

理解前端结构时，另一个非常关键的入口是：

- `frontend/src/app/app-routing.module.ts`

这里可以清楚看到 Dashboard 把前端页面组织成了一个很大的管理导航空间，比如：

- `overview`
- `hosts`
- `monitor`
- `services`
- `inventory`
- `osd`
- `configuration`
- `logs`
- `monitoring`

这说明 Dashboard 不是“若干零散功能页”的集合，而是：

- 一个统一的管理工作台

### 这里还有一个特别值得提的点

你会看到很多页面路由会挂：

- `AuthGuardService`
- `ModuleStatusGuardService`

这意味着前端本身也不是无条件开放所有页面，而会根据：

- 登录状态
- 模块/后端能力是否可用

来决定页面是否可进入。

所以 Dashboard 的前后端协作不是：

- 后端管 API，前端只展示

而是：

- 后端和前端都在共同维护“功能可见性”和“能力门禁”

## 第 10 层：前后端真正的协作，不只是“调接口”，而是“围绕控制器资源模型展开”

理解 Dashboard 的前后端协作，最容易停留在一句空话上：

- 前端调后端 API

这当然没错，但不够具体。

更准确的说法是：

- 前端通过 `shared/api` 里的服务封装访问后端控制器
- 后端控制器再通过 `services/*`、`mgr` API、orchestrator、Prometheus、Grafana 等实现能力落地

也就是说，它是三层协作：

1. 前端页面与组件
2. 前端 API service 与后端 controller
3. 后端 controller 与 Ceph/外部系统服务层

这比“前端请求后端”更能反映真实结构。

## Grafana 集成：Dashboard 为什么不自己重造所有图表后端

Grafana 集成是理解 Dashboard 非常好的一个例子。

从文档和代码都能看出来，Dashboard 支持：

- 嵌入 Grafana dashboard
- 校验 Grafana URL 和 dashboard UID
- 在某些情况下推送本地 dashboard 定义到 Grafana

### 这说明什么

说明 Dashboard 的策略不是：

- 自己重造一整套监控图表系统

而是：

- 站在 `MGR` 管理平面上，与更专业的监控展示系统协作

### 从代码结构看

这里至少有两层：

- 后端 `controllers/grafana.py` 与 `grafana.py`
- 前端 `shared/components/grafana/*`

也就是说：

- 后端负责 URL、校验、推送等管理动作
- 前端负责嵌入式展示与交互

这就是一个非常典型的“前后端共同完成外部系统集成”的例子。

## Prometheus 集成：Dashboard 为什么更像代理与管理层，而不是监控采集器

Prometheus 集成同样非常能体现 Dashboard 的边界感。

Dashboard 不负责自己去采集底层所有监控数据，它更像：

- 站在管理平面上，代理和组织 Prometheus 相关访问能力

### 从功能上看，它会处理什么

- 告警列表
- 规则
- silences
- 告警通知
- 查询相关数据

### 从架构上看，这意味着什么

这意味着 Dashboard 和 Prometheus 的关系不是“替代”，而是：

- 对接
- 聚合
- 暴露为更适合管理界面的功能入口

所以 Dashboard 在这里的角色更像：

- 面向用户的运维管理前台

而 Prometheus 仍然是：

- 监控数据与告警体系的重要后端组件

## orchestrator 集成：Dashboard 不只是“看板”，还是“集群管理入口”

如果说 Grafana 和 Prometheus 更多体现 Dashboard 的“看”，那么 orchestrator 集成体现的就是 Dashboard 的“管”。

这部分非常关键，因为它直接解释了为什么 Dashboard 不只是一个监控前端，而是：

- 真正的集群管理入口之一

### 这里的核心逻辑是什么

前端页面比如：

- `services`
- `inventory`
- `add-storage`
- `upgrade`

会先探测 orchestrator 是否可用。

后端则通过：

- `services/orchestrator.py`
- `controllers/orchestrator.py`
- `controllers/service.py`

等入口，把操作继续转向：

- `OrchClient`
- 底层 orchestrator backend

比如：

- `cephadm`
- `rook`

### 这说明什么

说明 Dashboard 不是“只读控制台”，而是能在合适权限和后端能力存在时，真正承担：

- 集群服务编排
- 主机与磁盘管理
- 升级入口

这一步让 Dashboard 从“可视化界面”真正升级成“管理工作台”。

## `ui-api` 为什么存在：不是所有接口都适合塞进正式业务 REST 资源

这是初学者很容易忽略，但很值得讲的一个点。

Dashboard 有：

- `/api`
- `/ui-api`

这本身就说明，前端需要的不一定都是严格意义上的业务资源 REST 接口。

比如：

- 语言列表
- 登录 banner
- 模块可用性探测
- 一些前端辅助元数据

这些东西如果全塞进正式业务资源接口，语义会比较别扭。

所以 `ui-api` 的存在，本质上是在告诉你：

- Dashboard 既是一个管理 API 系统
- 也是一个真实前端应用

而真实前端应用天然会需要一些：

- UI 辅助接口

这也是它架构成熟的体现。

## OpenAPI / 自描述文档：为什么 Dashboard 的接口体系是“可生成文档”的

Dashboard 还有一个很值得在文章里提到的点：

- 它不是一套只能靠源码猜的 API

控制器和 endpoint 元数据最终还能被：

- `controllers/docs.py`

整理成：

- `/docs/openapi.json`

这样的接口描述输出。

### 这说明了什么

说明 Dashboard 的控制器元数据设计不只是为了分发请求，也服务于：

- 文档生成
- 接口自描述
- 工具化能力

这也是为什么我们前面反复强调：

- 它不是零散 API，而是一套有框架意识的管理后端

## 把 Dashboard 压缩成一条“请求主线”

如果你想把这篇的全部内容压缩成一条最短协作链，可以记成下面这样：

```text
浏览器访问 Dashboard
  -> ceph-mgr 中的 dashboard 模块已通过 Module.serve() 启动
  -> CherryPy 托管静态前端资源与控制器路由
  -> Angular 前端加载并进入 AppModule / AppRoutingModule
  -> 前端组件通过 shared/api 请求 /api 或 /ui-api
  -> AuthManagerTool 先完成认证与鉴权
  -> 控制器方法执行业务逻辑
  -> 后端 service 再调用 mgr API、OrchClient、Prometheus、Grafana 等
  -> 返回 JSON 或页面数据给前端
```

这条链里最重要的不是函数名，而是分层感觉：

- `ceph-mgr` 模块宿主
- Web 服务框架
- 前端应用
- 控制器/认证
- 服务集成层

只要这五层顺序你没丢，后面回源码就不会迷路。

## 初学者最容易混淆的 8 个点

### 1. 认为 Dashboard 是独立部署在 Ceph 外部的管理系统

不准确。它首先是 `ceph-mgr` 里的一个模块。

### 2. 认为 Dashboard 是纯前端页面

不对。它是 `Python + CherryPy` 后端和 `Angular` 前端共同构成的 Web 应用。

### 3. 认为后端 API 都是手写零散路由

不对。它有 `Router / APIRouter / UIRouter`、`BaseController`、`RESTController` 这一整套抽象。

### 4. 认为认证是每个接口自己判断

不对。认证和鉴权主要通过 CherryPy tool 做统一前置处理。

### 5. 认为前端只是“请求后端 JSON”

太浅了。它还有路由守卫、功能门禁、模块状态探测和共享 API 层。

### 6. 认为 Dashboard 自己重造了所有监控和图表系统

不对。Grafana 和 Prometheus 集成体现的是协作，而不是替代。

### 7. 认为 Dashboard 只是展示界面，不承担管理动作

不对。它还通过 orchestrator 集成承载了很多实际管理入口。

### 8. 认为 `/ui-api` 和 `/api` 没区别

不对。前者更偏 UI 辅助接口，后者更偏正式业务资源接口。

## 这一篇最应该留下的 5 个直觉

### 直觉一：Dashboard 首先是 `MGR` 模块，其次才是 WebUI

这是理解它系统位置的关键。

### 直觉二：Dashboard 后端的中心骨架是 CherryPy

控制器、认证、静态资源托管和路由组织都围绕它展开。

### 直觉三：Dashboard 不是“一个前端 + 几个接口”，而是一套分层的前后端应用

前端、控制器、服务层、外部集成之间有明确边界。

### 直觉四：Grafana、Prometheus、orchestrator 集成体现了 Dashboard 的平台协作价值

它不是孤立系统，而是管理平面的统一入口。

### 直觉五：Dashboard 让 Ceph 的管理能力真正变成了可视化、可服务化、可扩展的工作台

这正是它在 `MGR` 生态中的意义。

## 下一篇看什么

既然这一篇已经把：

- Dashboard 如何作为 `MGR` 模块运行
- 后端控制器和认证如何组织
- Angular 前端如何协作
- Grafana、Prometheus、orchestrator 如何集成

这条主线讲清楚了，下一步最自然的事情，就是回到 Ceph 最核心的数据面执行单元本身：

**OSD 进程内部到底是如何组织请求处理、队列、Peering、复制和后台任务的？**

所以下一篇建议接：

**《OSD 进程详解：Ceph 最核心的数据面执行单元》**
