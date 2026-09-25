“在现有群聊系统上增加 Server 层”**的实现方案，而不是重新设计整个聊天系统。

整体思路

核心关系：

User
 │
 ├─────────────── 私聊
 │
 └── Server
       │
       ├── ServerMember
       │
       └── Channel（复用现有 Group Chat）
              │
              └── Message

也就是说：

Server 是社区容器，Channel 本质上还是你现在已经实现的群聊。

不需要重新实现一套聊天系统。

1. Server：社区
Server
├── id
├── name
├── icon
├── owner_id
├── description
└── created_at

对应数据库：

server

作用：

创建社区
删除社区
修改社区信息
设置服务器头像
指定服务器拥有者

创建服务器时：

User
 ↓
CreateServer
 ↓
Server
 ↓
创建者自动成为 ServerMember

服务器头像 icon 不单独搞图片系统，直接复用现有文件系统：

上传图片
 ↓
File
 ↓
file_id
 ↓
Server.icon_file_id
2. ServerMember：服务器成员
ServerMember
├── server_id
├── user_id
├── nickname
├── joined_at
└── role

数据库：

server_member

它解决：

谁加入了这个服务器？
这个人在服务器里的昵称是什么？
他的身份是什么？
什么时候加入？

关系：

User 1 ───── N ServerMember N ───── 1 Server

例如：

Yiuk
 ├── C++ Community
 │     └── nickname = Yiuk
 │
 └── Minecraft Community
       └── nickname = 玩家

所以服务器昵称可以和全局用户名分开。

3. Channel：服务器里的聊天频道
Channel
├── id
├── server_id
├── name
├── type
├── position
└── created_at

但你这里可以直接利用现有的 Group。

实际上可以设计成：

Server
 │
 ├── Group 101
 ├── Group 102
 └── Group 103

数据库可以增加一个关联：

server_channel
├── server_id
├── channel_id
└── position

或者如果你觉得没必要增加关联表，直接给现有 Group 增加：

group.server_id

就行。

这样：

Group
├── id
├── server_id
├── name
├── ...

当：

server_id = NULL

可以代表普通独立群聊；

当：

server_id = 123

就代表这个群聊属于某个 Server。

这会是你当前项目改动最小的方案。

4. Channel 本身不负责重新实现聊天

这是整个设计里最重要的一点。

你现在已经有：

Group
 ↓
GroupMember
 ↓
Message
 ↓
Session
 ↓
WebSocket
 ↓
Protobuf

不要动。

变成：

Server
 │
 └── Channel / Group
          │
          ├── GroupMember
          └── Message

所以发送消息依然是：

Client
 ↓
WebSocket
 ↓
Protobuf
 ↓
Server
 ↓
找到 Channel / Group
 ↓
找到成员
 ↓
广播 Message

只是多了一层 Server 的组织关系。

5. 前端表现

最终 UI：

┌────┬──────────────┬─────────────────────────┐
│    │ Server A     │ # cpp                   │
│ S1 │              │                         │
│    │ 📢 公告      │ UserA                   │
│ S2 │ 💬 闲聊      │ Hello                   │
│    │ 💻 C++       │                         │
│ S3 │ 🎮 游戏      │ UserB                   │
│    │              │ 👍                      │
│ +  │ Server B     │                         │
│    │              │                         │
└────┴──────────────┴─────────────────────────┘

逻辑：

点击 Server
    ↓
加载 Server 信息
    ↓
加载 ServerMember
    ↓
加载 Server 下的 Channel/Group
    ↓
默认进入某个 Channel
    ↓
继续使用现有群聊逻辑
6. 图片、头像、Emoji

全部尽量复用现有文件传输。

用户头像
User.avatar_file_id
Server 图标
Server.icon_file_id
图片消息
Message
└── attachment / file_id
自定义 Emoji
CustomEmoji
├── id
├── server_id
├── name
└── file_id

所以不会出现：

头像系统
图片系统
Emoji 系统
文件系统

四套东西重复造轮子。

而是：

                 File
                  │
       ┌──────────┼──────────┐
       ↓          ↓          ↓
     Avatar      Image      Emoji
       │
       └──── Server Icon
7. 回复消息

现有 Message 增加：

reply_to_message_id

即可。

Message
├── id
├── channel_id
├── sender_id
├── content
├── reply_to_message_id
└── created_at

前端根据 reply_to_message_id 显示引用消息。

8. Emoji

普通 Emoji：

😂 👍 ❤️ 🤣

直接作为 UTF-8 字符串。

不需要后端特殊处理。

自定义 Emoji：

[emoji:123]

或者以后在 Protobuf 中单独设计消息元素。

第一版甚至没必要搞复杂。

9. 聊天记录搜索

第一版可以非常简单：

当前已经加载的 messages[]
              ↓
        前端字符串搜索

例如：

messages.filter(
    message => message.content.includes(keyword)
)

暂时不做：

Elasticsearch
MySQL FULLTEXT
Redis Search

以后有需求再升级。

10. Role 暂时简单处理

你现在：

ServerMember
└── role

第一版可以直接：

role = owner / admin / member

甚至名字颜色：

ServerMember
└── name_color

直接由前端：

<span :style="{ color: member.nameColor }">

以后真的需要完整权限系统，再升级成：

Server
 │
 ├── Role
 │    ├── name
 │    ├── color
 │    └── permissions
 │
 └── ServerMember
       └── role_id

不用现在就做。

11. 最终架构

所以整个项目最终可以理解成：

                         User
                          │
             ┌────────────┴────────────┐
             │                         │
           DM                      Server
                                       │
                         ┌─────────────┼─────────────┐
                         │             │             │
                    ServerMember    Channel       Role
                                       │
                                  （现有 Group）
                                       │
                                  GroupMember
                                       │
                                    Message
                                       │
                              ┌────────┴────────┐
                              │                 │
                            Text             File
                              │                 │
                            Emoji        Image / Avatar