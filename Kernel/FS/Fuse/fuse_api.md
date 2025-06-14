## fuse lowlevel api
> `void(* fuse_lowlevel_ops::init)(void *userdata, struct fuse_conn_info *conn)`

初始化文件系统。在liufuse和kernel模块建立连接的时候会被调用；

> `void(* fuse_lowlevel_ops::destroy)(void *userdata)`

清理文件系统。文件系统退出时调用改方法，注意，此时libfuse和kernel的连接可能已经关闭，所以像fuse_lowlevel_notify_*系列的调用将会失败；

> `void(* lookup)(fuse_req_t req, fuse_ino_t parent, const char *name)`

获取prent目录下名为name的文件属性，可返回`fuse_reply_entry`或`fuse_reply_err`

> `void(* forget)(fuse_req_t req, fuse_ino_t ino, uint64_t nlookup)`

当内核从自己内部的缓存中删除inode时会调用文件系统的该方法，inode的lookup计数会在每次调用`fuse_reply_entry`和`fuse_reply_create`时加1，该方法中的nlookup参数表明了需要减掉多少lookup计数；返回`fuse_reply_none`

> `void(* fuse_lowlevel_ops::getattr)(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)`

获取文件属性，返回`fuse_reply_attr`和`fuse_reply_err`

> `void(* fuse_lowlevel_ops::setattr)(fuse_req_t req, fuse_ino_t ino, struct stat *attr, int to_set, struct fuse_file_info *fi)`

设置文件系统属性。返回`fuse_reply_attr`和`fuse_reply_err`

