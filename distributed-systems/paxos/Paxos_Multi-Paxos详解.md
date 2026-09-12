 # Paxos、Multi-Paxos详解
> There is only one consensus protocol, and that's Paxos – all other approaches are just broken versions of Paxos.

## 1.简介
本文是分布式系统原理和实践的第一篇，笔者在学习的过程参考了原论文以及很多网络文章，感谢那些作者的分享和记录。主要会从Paxos引入，到Paxos的流程分解，再到对Paxos进行总结，最后分析了Multi-Paxos。并不会涉及系统实现相关的部分，如果读者对这个部分有了解，可以自行跳过；
在一个系统中，为了避免单节点故障导致数据丢失和不可用，通常会使用把数据复制多份进行保存，并将其存储在不同的数据节点上，以此使系统具有可用性和可靠性，降低数据丢失的可能。但这种方式同时也引入了一个新的问题，就是如何保证处于不同节点上的副本数据是一致的，数据副本之间没有任何差异。分布式共识算法就是为解决这个问题而生，而最让人熟知的当属Paxos协议，Google Chubby的作者就曾说，世界上就只有一种一致性协议，那就是Paxos，其他都是Paxos的残缺版本；

## 2.Paxos的引入
#### 2.1 简介
如果要理解Paxos，那必须要知道Paxos解决了什么问题，当然在是解决一致性问题，这里想说的是Paxos如何去解决一致性问题，笔者查阅了论文和网上很多相关的文章，认为最易于记忆的总结是：

> 确定一个不可变变量的取值

Paxos通过确定一个不可变变量的取值来使分布式系统达成一致，对于这个总结可能会存在不少疑惑，确定一个不可变变量有什么用，系统中的数据都是变化的，随时可能发生增删改；但如果换一个思路，在一份数据的多个副本中，假设它们的初始状态一致，如果共同对它们进行操作并且操作的顺序一致，那副本在每个操作结束后的结果也就是一致的，这里我们把各个操作按照顺序记录成[op.1,op.2,...op.i,op.i+1,...]，在一个分布式系统中，必然会存在多个进程同时对数据进行操作，那Paxos要做的就是在这些操作中进行选择，并将选择的结果赋值给op.i，这个过程称为确定，即确定op.i的值，一旦op.i确定后就不可再更改。

#### 2.2 基本角色
- Proposer: 提出需要被确定的值
- Acceptor: 接受需要被确定的值
- Learner: 将确定的值保存和应用

在Paxos中，包括了三类角色，分别是Proposer、Accepter和Learner，一个系统中他们都存在一个或则多个。需要被确定的值由Proposer提出，值被提出后发送给接收者Acceptor进行确定，确定后由学习者对值进行保存和应用。

#### 2.3 确定一个值(单个Accptor)

![2ebc0de90c7b4d78ea75a775d7f742f2.png](evernotecid://F69FFB73-5A4B-441D-90A3-E7FF421602A3/appyinxiangcom/31284421/ENResource/p24)

在只有单个Acceptor的系统中，对于多个Propser提出的多个值，Acceptor只要选择它第一个收到的就能将值确定下来。相应的问题是单点Acceptor失效的情况下会使得系统不可用，所以需要引入多个Acceptor。

#### 2.4 确定一个值(多个Accptor)

![3db593fa1aa51b86248a343c8eec4476.png](evernotecid://F69FFB73-5A4B-441D-90A3-E7FF421602A3/appyinxiangcom/31284421/ENResource/p22)

在具有多个Acceptor的系统中，Proposer可以将提出的值发送给多个Acceptor，只要被大多数（一半以上）Acceptor接受了，那么值就被确定了。为什么是大多数？任何两个由大多数Acceptor组成的集合必然存在交集，即公共的Acceptor，**只要保证Acceptor只能接受(accept)一个值**，那任何两个大多数集合确定的值就是一样的，如果不一样，则集合交集中至少存在一个Acceptor接受了两个值，和条件违背，所以只要**大多数**Acceptor接受了一个值，那值就确定了，并且系统能供容忍少于大多数的Acceptor出现故障。综上，得出的Paxos实现一致性的原始条件是：

> Acceptor只能接受一个值

![4b7488135c719fba4e5490c73220fd76.png](evernotecid://F69FFB73-5A4B-441D-90A3-E7FF421602A3/appyinxiangcom/31284421/ENResource/p32)

如果一个Acceptor能够接受多个值，那如上图所示，提出的两个值都被大多数的Acceptor接受了，那就确定出了两个值而不是一个值，与条件违背；

![93bf83fd8189f92a0412aa721a67b098.png](evernotecid://F69FFB73-5A4B-441D-90A3-E7FF421602A3/appyinxiangcom/31284421/ENResource/p33)

如果一个Acceptor只能接受一个值，那就能保证在一个值被大多数Acceptor接受后就不会出现另一个值再被接受。

> P1.Acceptor必须接受收到的第一个值

![3f3ad4ef57a5bad64a436b0a29a8a65a.png](evernotecid://F69FFB73-5A4B-441D-90A3-E7FF421602A3/appyinxiangcom/31284421/ENResource/p35)

如果一个Acceptor不接受它收到的第一个值，那值的确定就无从谈起。这个条件的引入产生了一个问题，如上图，一个系统中可能会存在多个Properer提出值，Acceptor接受第一个值后无法形成大多数，造成值无法确定，所以Acceptor除了接受第一个值之外，还需要能够接受后续的值，直到值被大多数Acceptor接受并确定下来为止。
为了对Acceptor对值的多次接受进行标识，引入一个编号，编号和值合并称为提议，将编号称为提议号，需要被确定的值称为提议值，至此，提议就包含了提议号和提议值。如果一个提议被确定，那该提议包含的值就被确定了。提议号的生成由系统自行实现，但需要不同的提议具有不同的提议号，并按提出的先后进行递增，即提议号小的提议先被提出，提议号大的后被提出。由于Acceptor能够接受多个提议并且不受数量的限制，一旦一个提议被确定后，为了保证只有一个值被确定，需要对之后的提议进行约束。

> P2.如果一个值为v的提议被确定了，那之后确定的提议（提议号更高）的值（提议值）也是v

由于提议编号是有序递增的，该条件保证了一旦有提议被确定了，那之后被再确定的提议值是相同的，确保了只有一个值被确定。

> P2a.如果一个值为v的提议被确定了，那之后Accpetor接受的提议（提议号更高）的值（提议值）也是v

一个提议在被确定之前，需要被Acceptor接受，所以只要满足该条件就能满足P2。

> P2b.如果一个值为v的提议被确定了，那之后Proposer提出的提议（提议号更高）的值（提议值）也是v

一个提议在被Acceptor之前，需要被Proposer提出，只要满足P2b就能满足P2a。但Proposer如何如何确定提议的提议值v是什么？只要做到有确定的值就用确定值，没有则用自己的值，因此引入P2c。

> P2c.如果一个提议号为n，提议值为v的提议被一个Proposer提出，那必然存在一个有大多数Acceptor组成的集合S：
> - (a).集合S中的任何一个Acceptor都没有接受过提议号小于n的提议；
> - (b).v是集合S中的Acceptor接受的最高提议编号m对应的提议值,其中m<n;

根据以上条件，需要Proposer在提出提议之前先学习到Acceptor接受过的具有最高提议号的提议的提议值，并且为了避免小于n的提议被接受，要求Acceptor不再接受小于n的提议；基于以上的条件，提出提议的流程为：
> 1.Proposer选择提议号n发送给Acceptor,并要求Acceptor对自己做如下的要求，这个过程称为prepare：
> - (a).不再接受提议号小于n的提议
> - (b).如果Acceptor已经接受了提议，则将提议返回给Proposer

> 2.如果Proposer接收到了大多数Acceptor的请求返回，那Proposer就能提出提议号为n,提议值为v的提议，其中v有如下两种可能：
> - (a).如果请求返回中存在提议，则v是所有返回中具有最高提议号的提议值
> - (b).如果不存在返回，则使用Proposer自己的提议值
> 
> 这个过程称为accept。

以上是Proposer的提议流程，针对Acceptor，会收到来自Proposer的两类请求：prepare和accept，从上面的条件可以看出，Acceptor一旦做出承诺，则不会再接受提议号小于n的提议，所以我们对P1进行修正：
> P1a.如果Acceptor没有收到过大于n的prepare请求，则可以接受提议号为n的提议。

以上是针对accept阶段的约束，可以做进一步的优化；如果acceptor收到了一个提议编号为n的prepare请求，此时发现已经回复过了一个提议号比n更大的提议，通过以上的约束条件，知道即使acceptor收到编号为n的accept请求，也不会被接受，所以可以忽略该prepare请求不作回复，或则回复通知proposer不必再发起accept请求。
综上，需要acceptor记录两个东西：
- 接受过的具有最高提议
- 承诺过的最高提议编号

#### 2.5 Paxos总结

![4c39a8c96f4d84a44a17502a668b9ac8.png](evernotecid://F69FFB73-5A4B-441D-90A3-E7FF421602A3/appyinxiangcom/31284421/ENResource/p38)

如上图所示，Paxos主要分为两个阶段：
- 阶段一：
(a)Properser选择一个提议号n，并给大多数的Acceptor发送prepare请求；
(b)Acceptor接收到prepare的请求后，如果提议号n大于之前回复过的最高提议号highest_promised_number，则重置最高提议号，并将接收过的提议返回给Proposer，承诺不再接受小于等于highest_promised_number的prepare请求；如果提议号n小于hightest_promised_number，则不响应请求或告知Proposer决绝接受的消息；

- 阶段二：
(a)如果Proposer收到了大部分Acceptor的prepare接受响应，则查看响应中是否存在接受的提议，如果存在，则将提议值设置为最高提议编号对应的值，如果不存在，则使用Propose自身的值，确定提议值后给大多数的Acceptor发送accept请求；
(b)如果Acceptor接收到了提议号为n的提议，如果没有响应过更高提议的prepare请求，即n>=highest_promised_number，就接受该提议，并重置accepted_proposal。否则不响应或返回拒绝信息给Proposer；

#### 2.6 活锁
在上一小节的总结中，可以看到Paxos主要分成两个阶段来完成，Acceptor通过提议编号的大小确定是否接受提议，可能会存在一种场景，如下图所示：

![e802a9c4cb8fcead41ae1cf1d8efc883.png](evernotecid://F69FFB73-5A4B-441D-90A3-E7FF421602A3/appyinxiangcom/31284421/ENResource/p39)

1. Proposer.1收到客户端的请求后发送编号为1的提议，preapare被1，2，3三个Acceptor接收；
2. 同时Proposer.3收到客户端的请求后发送编号为2的提议，也被1，2，3三个Acceptor接收；
3. Propser.1收到提议1的大多数prepare请求响应后发出accept请求，因为acceptor已经接收了编号为2的prepare请求，所以会拒绝编号为1的accept请求；
4. 由于系统没有确定接受的值，所以Proposer.1会提高自己的提议编号为3，并发送preapre请求，并导致了编号为2的accept请求被拒绝；
5. 不断的重复以上的步骤，会导致系统中不能确定被接受的值；

上诉的这种现象称为“活锁”；解决的办法可以在Proposer中选出一个leader，由leader进行所有提议的提交，如下图所示:

![8aa43ce75e3b5931e0a4d36bc42f9a27.png](evernotecid://F69FFB73-5A4B-441D-90A3-E7FF421602A3/appyinxiangcom/31284421/ENResource/p41)

## 3.Multi-Paxos
在一个系统中，我们可以通过不断的运行Paxos来确定多个值，运行一次Paxos称为一个实例，一个实例能够确定一个值，如下图：

![762e94afa476e53f3321e7e34d716f40.png](evernotecid://F69FFB73-5A4B-441D-90A3-E7FF421602A3/appyinxiangcom/31284421/ENResource/p46)

每个实例都需要运行Paxos的两个阶段，如下图是一个节点允许多个节点同时提交的运行情况：

![5771ace945845267a4c1fba688816ef4.png](evernotecid://F69FFB73-5A4B-441D-90A3-E7FF421602A3/appyinxiangcom/31284421/ENResource/p44)

> n.m : 其中n代表节点，m代表提议号，例如1.1位节点1提出的编号为1的提议；

- 实例1中，节点1提出了编号为1的提议，顺利的被accept
- 实例2中，节点2提出了编号为1的提议，由于节点1已经接收了来自节点2编号为3的提议，所以不会accept编号为1的提议；实例4同理，接收了来自节点3的更高的提议号而导致1.2被拒绝；

如果只有一个节点提出提议，则流程如下：

![36be3001632230da086d31295d87be29.png](evernotecid://F69FFB73-5A4B-441D-90A3-E7FF421602A3/appyinxiangcom/31284421/ENResource/p45)

由于只有一个节点提出提议，确认的流程就不会被打断，每次提议的编号都一致，只要对一个提议做出承诺后，之后的提议都不需要再承诺，从而省略Paxos的第一阶段，即将对提议编号的承诺扩展到多个实例：

![48aa0d0f5f92ac7078f512574816dd1c.png](evernotecid://F69FFB73-5A4B-441D-90A3-E7FF421602A3/appyinxiangcom/31284421/ENResource/p47)

由此可以看见，Multi-Paxos是系统的某种特殊场景下表现出来的并加之优化的特性，无需使用leader，在多个节点进行提交的情况下回衰减为朴素的Paxos，为了能够提供更好的性能，可以加强这一特性。

## 参考列表
- [分布式系统一致性及Paxos详解](https://blog.yangx.site/2018/08/05/paxos/)
- [Paxos算法详解](https://zhuanlan.zhihu.com/p/31780743)
- [Paxos共识算法详解](https://juejin.im/post/6844903817297788942)
- [分布式一致性与共识算法](https://draveness.me/consensus/)
- [深入理解Paxos算法协议](https://sevenvoid.github.io/2017-06-27-paxos/)
- [架构师需要了解的Paxos原理、历程及实战](https://mp.weixin.qq.com/s?__biz=MzAwMDU1MTE1OQ==&mid=403582309&idx=1&sn=80c006f4e84a8af35dc8e9654f018ace&scene=1&srcid=0225bkM8A6s0yGx5pYO000iz#rd)
- [【原创】一步一步理解Paxos算法](https://mp.weixin.qq.com/s?__biz=MjM5MDg2NjIyMA==&mid=203607654&idx=1&sn=bfe71374fbca7ec5adf31bd3500ab95a&key=8ea74966bf01cfb6684dc066454e04bb5194d780db67f87b55480b52800238c2dfae323218ee8645f0c094e607ea7e6f&ascene=1&uin=MjA1MDk3Njk1&devicetype=webwx&version=70000001&pass_ticket=2ivcW%2FcENyzkz%2FGjIaPDdMzzf%2Bberd36%2FR3FYecikmo%3D)
- [Paxos算法原理和过程解析](https://www.mdeditor.tw/pl/p3Dm)
- [深入理解分布式共识算法 Paxos](https://www.sohu.com/a/404156602_355142)
- [Paxos、Multi-Paxos详解](https://liu-jianhao.github.io/2019/05/paxosmulti-paxos%E8%AF%A6%E8%A7%A3/)
- [微信PaxosStore：深入浅出Paxos算法协议](https://www.infoq.cn/article/wechat-paxosstore-paxos-algorithm-protocol)
- [微信自研生产级paxos类库PhxPaxos实现原理介绍](https://mp.weixin.qq.com/s?__biz=MzI4NDMyNTU2Mw==&mid=2247483695&idx=1&sn=91ea422913fc62579e020e941d1d059e#rd)
- [微信开源：生产级paxos类库PhxPaxos实现原理介绍](https://www.infoq.cn/article/weinxin-open-source-paxos-phxpaxos?utm_source=related_read_bottom&utm_medium=article)
- [微信PaxosStore内存云揭秘：十亿Paxos/分钟的挑战](https://mp.weixin.qq.com/s?__biz=MjM5MDE0Mjc4MA==&mid=2650994526&idx=1&sn=255dd87bd8601919bda3d597c65439f3&chksm=bdbf0f0d8ac8861bad452606b302ca6655cf84ed161584a1246a8cb9fd1361ec1ac1386ffd92&scene=21)
- [PaxosStore在微信支付业务的实践](https://www.infoq.cn/article/U897DqTxKttpAxwFPRik)
- [分布式-Paxos算法(一)Basic-Paxos](https://honorjoey.top/2020/07/03/%E5%88%86%E5%B8%83%E5%BC%8F-Paxos%E7%AE%97%E6%B3%95(%E4%B8%80)-Basic-Paxos/)
- [paxos算法证明过程](https://my.oschina.net/pingpangkuangmo/blog/788484)
- [谈谈paxos, multi-paxos, raft](https://baotiao.github.io/2016/05/05/paxos-raft/)
- [[Paxos三部曲之二] 使用Multi-Paxos协议的日志同步与恢复](http://oceanbase.org.cn/?p=111)
- [Multi Paxos：Basic Paxos的进化](https://my.oschina.net/liangtee/blog/304779)
- [Paxos理论介绍(1): 朴素Paxos算法理论推导与证明](https://zhuanlan.zhihu.com/p/21438357)
- [Paxos理论介绍(2): Multi-Paxos与Leader](https://zhuanlan.zhihu.com/p/21466932)
- [Paxos理论介绍(3): Master选举](https://zhuanlan.zhihu.com/p/21540239)
- [Paxos理论介绍(4): 动态成员变更](https://zhuanlan.zhihu.com/p/22148265)
- [The Paxos Algorithmor How to Win a Turing Award](https://lamport.azurewebsites.net/tla/paxos-algorithm.html?back-link=more-stuff.html)
- [Lamport Paxos」学习Paxos分布式一致性协议必读的两篇论文译文](https://juejin.im/post/6844903683788898312#heading-1)
- [《Paxos Made Simple》翻译](https://www.cnblogs.com/YaoDD/p/6150498.html)
- [Paxos Made Easy](https://github.com/turingcell/paxos-made-easy)
- [Raft user study](https://ongardie.net/static/raft/userstudy/)
- [Paxos (computer science)](https://en.wikipedia.org/wiki/Paxos_(computer_science))


