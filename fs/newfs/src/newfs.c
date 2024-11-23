#define _XOPEN_SOURCE 700

#include "newfs.h"
#include "types.h"

/******************************************************************************
* SECTION: 宏定义
*******************************************************************************/
#define OPTION(t, p)        { t, offsetof(struct custom_options, p), 1 }

/******************************************************************************
* SECTION: 全局变量
*******************************************************************************/
static const struct fuse_opt option_spec[] = {		/* 用于FUSE文件系统解析参数 */
	OPTION("--device=%s", device),
	FUSE_OPT_END
};

struct custom_options nfs_options;			 /* 全局选项 */
struct nfs_super nfs_super; 
/******************************************************************************
* SECTION: FUSE操作定义
*******************************************************************************/
static struct fuse_operations operations = {
    .init      = newfs_init,      /* mount文件系统 */
    .destroy   = newfs_destroy,   /* umount文件系统 */
    .mkdir     = newfs_mkdir,     /* 建目录，mkdir */
    .getattr   = newfs_getattr,   /* 获取文件属性，类似stat，必须完成 */
    .readdir   = newfs_readdir,   /* 填充dentrys */
    .mknod     = newfs_mknod,     /* 创建文件，touch相关 */
    .write     = NULL,                 /* 写入文件 */
    .read      = NULL,                 /* 读文件 */
    .utimens   = newfs_utimens,   /* 修改时间，忽略，避免touch报错 */
    .truncate  = NULL,                 /* 改变文件大小 */
    .unlink    = NULL,                 /* 删除文件 */
    .rmdir     = NULL,                 /* 删除目录， rm -r */
    .rename    = NULL,                 /* 重命名，mv */
    
    .open      = NULL, 
    .opendir   = NULL,
    .access    = NULL
};

/******************************************************************************
* SECTION: 必做函数实现
*******************************************************************************/
/**
 * @brief 挂载（mount）文件系统
 * 
 * @param conn_info 可忽略，一些建立连接相关的信息 
 * @return void*
 */
void* newfs_init(struct fuse_conn_info * conn_info) {
	/* TODO: 在这里进行挂载 */
	if(nfs_mount(nfs_options) != NFS_ERROR_NONE) {
		NFS_DBG("[%s] mount error\n", __func__);
		fuse_exit(fuse_get_context()->fuse);
		return NULL;
	}
	return NULL;
}

/**
 * @brief 卸载（umount）文件系统
 * 
 * @param p 可忽略
 * @return void
 */
void newfs_destroy(void* p) {
	/* TODO: 在这里进行卸载 */
	if (nfs_umount() != NFS_ERROR_NONE) {
		NFS_DBG("[%s] unmount error\n", __func__);
		fuse_exit(fuse_get_context()->fuse);
		return;
	}
	return;
}

/**
 * @brief 创建目录
 * 
 * @param path 相对于挂载点的路径
 * @param mode 创建模式（只读？只写？），可忽略
 * @return int 0成功，否则返回对应错误号
 */
int newfs_mkdir(const char* path, mode_t mode) {
    /* TODO: 解析路径，创建目录 */
    (void)mode;  // 忽略 mode 参数

    boolean is_find, is_root;
    char* fname;
    // 查找路径对应的目录项，返回最后一个目录项及其是否存在、是否是根目录
    struct nfs_dentry* last_dentry = nfs_lookup(path, &is_find, &is_root);
    struct nfs_dentry* dentry;
    struct nfs_inode*  inode;

    if (is_find) {	    // 如果目录已存在，返回错误
        return -NFS_ERROR_EXISTS;
    }
    if (NFS_IS_REG(last_dentry->inode)) {	    // 如果路径最后是普通文件，则不支持创建目录
        return -NFS_ERROR_UNSUPPORTED;
    }
    fname  = nfs_get_fname(path);			// 获取路径中的目录名
    dentry = new_dentry(fname, NFS_DIR); 	// 创建新的目录项
    dentry->parent = last_dentry; 			// 设置父目录
    inode  = nfs_alloc_inode(dentry);		// 为新目录项分配inode
    nfs_alloc_dentry(last_dentry->inode, dentry, 1);	// 将新目录项添加到父目录的inode中
    // 创建成功，返回成功标志
    return NFS_ERROR_NONE;
}


/**
 * @brief 获取文件或目录的属性，该函数非常重要
 * 
 * @param path 相对于挂载点的路径
 * @param newfs_stat 返回状态
 * @return int 0成功，否则返回对应错误号
 */
int newfs_getattr(const char* path, struct stat * nfs_stat) {
    /* TODO: 解析路径，获取Inode，填充newfs_stat，可参考/fs/simplefs/sfs.c的sfs_getattr()函数实现 */
    boolean is_find, is_root;
    
    // 查找路径对应的目录项，返回是否找到以及是否为根目录
    struct nfs_dentry* dentry = nfs_lookup(path, &is_find, &is_root);
    
    // 如果没有找到路径，返回找不到错误
    if (is_find == FALSE) {
        return -NFS_ERROR_NOTFOUND;
    }

    // 如果是目录，设置目录相关的属性
    if (NFS_IS_DIR(dentry->inode)) {
        nfs_stat->st_mode  = S_IFDIR | NFS_DEFAULT_PERM;   // 设置文件类型为目录
        nfs_stat->st_size  = dentry->inode->dir_cnt * sizeof(struct nfs_dentry_d);  // 设置目录大小
    }
    // 如果是常规文件，设置文件相关的属性
    else if (NFS_IS_REG(dentry->inode)) {
        nfs_stat->st_mode  = S_IFREG | NFS_DEFAULT_PERM;    // 设置文件类型为常规文件
        nfs_stat->st_size  = dentry->inode->size;           // 设置文件大小
    }

    // 设置文件的其他属性
    nfs_stat->st_nlink = 1;                  // 默认链接数为1
    nfs_stat->st_uid   = getuid();           // 获取当前用户的UID
    nfs_stat->st_gid   = getgid();           // 获取当前用户的GID
    nfs_stat->st_atime = time(NULL);         // 获取当前时间作为最后访问时间
    nfs_stat->st_mtime = time(NULL);         // 获取当前时间作为最后修改时间
    nfs_stat->st_blksize = NFS_BLK_SZ();     // 设置块大小

    // 如果是根目录，特殊处理根目录的属性
    if (is_root) {
        nfs_stat->st_size    = nfs_super.sz_usage;          	// 根目录大小设置为文件系统总大小
        nfs_stat->st_blocks  = NFS_DISK_SZ() / NFS_BLK_SZ(); // 根目录块数设置为磁盘大小除以块大小
        nfs_stat->st_nlink   = 2;                            /* !特殊，根目录link数为2 */
    }

    return NFS_ERROR_NONE;  // 成功返回
}


/**
 * @brief 遍历目录项，填充至buf，并交给FUSE输出
 * 
 * @param path 相对于挂载点的路径
 * @param buf 输出buffer
 * @param filler 参数讲解:
 * 
 * typedef int (*fuse_fill_dir_t) (void *buf, const char *name,
 *				const struct stat *stbuf, off_t off)
 * buf: name会被复制到buf中
 * name: dentry名字
 * stbuf: 文件状态，可忽略
 * off: 下一次offset从哪里开始，这里可以理解为第几个dentry
 * 
 * @param offset 第几个目录项？
 * @param fi 可忽略
 * @return int 0成功，否则返回对应错误号
 */
int newfs_readdir(const char * path, void * buf, fuse_fill_dir_t filler, off_t offset,
			    		 struct fuse_file_info * fi) {
    /* TODO: 解析路径，获取目录的Inode，并读取目录项，利用filler填充到buf，可参考/fs/simplefs/sfs.c的sfs_readdir()函数实现 */
    boolean	is_find, is_root;
	int		cur_dir = offset;

	// 调用 nfs_lookup 查找路径，返回目录项（dentry），并标记是否找到以及是否为根目录
	struct nfs_dentry* dentry = nfs_lookup(path, &is_find, &is_root);
    struct nfs_dentry* sub_dentry;    // 子目录项
    struct nfs_inode* inode;          // 目录的 inode
	if (is_find) {		// 如果找到了目录项
		inode = dentry->inode;		// 获取该目录的 inode
		sub_dentry = nfs_get_dentry(inode, cur_dir);	// 获取目录下的子目录项
		// 如果找到了子目录项，使用filler填充到buf中
		if (sub_dentry) {
			filler(buf, sub_dentry->fname, NULL, ++offset);	// 将子目录项的名称填充到buf，并增加offset
		}
		return NFS_ERROR_NONE;
	}
	// 如果未找到目录项，返回目录未找到错误
	return -NFS_ERROR_NOTFOUND;
}

/**
 * @brief 创建文件
 * 
 * @param path 相对于挂载点的路径
 * @param mode 创建文件的模式，可忽略
 * @param dev 设备类型，可忽略
 * @return int 0成功，否则返回对应错误号
 */
int newfs_mknod(const char* path, mode_t mode, dev_t dev) {
	/* TODO: 解析路径，并创建相应的文件 */
	boolean	is_find, is_root;
	
	struct nfs_dentry* last_dentry = nfs_lookup(path, &is_find, &is_root);
	struct nfs_dentry* dentry;
	struct nfs_inode* inode;
	char* fname;
	
	if (is_find == TRUE) {
		return -NFS_ERROR_EXISTS;
	}

	fname = nfs_get_fname(path);
	
	if (S_ISREG(mode)) {
		dentry = new_dentry(fname, NFS_REG_FILE);
	}
	else if (S_ISDIR(mode)) {
		dentry = new_dentry(fname, NFS_DIR);
	}
	else {
		dentry = new_dentry(fname, NFS_REG_FILE);
	}
	dentry->parent = last_dentry;
	inode = nfs_alloc_inode(dentry);
	nfs_alloc_dentry(last_dentry->inode, dentry, 1);

	return NFS_ERROR_NONE;
}

/**
 * @brief 修改时间，为了不让touch报错 
 * 
 * @param path 相对于挂载点的路径
 * @param tv 实践
 * @return int 0成功，否则返回对应错误号
 */
int newfs_utimens(const char* path, const struct timespec tv[2]) {
	(void)path;
	return 0;
}
/******************************************************************************
* SECTION: 选做函数实现
*******************************************************************************/
/**
 * @brief 写入文件
 * 
 * @param path 相对于挂载点的路径
 * @param buf 写入的内容
 * @param size 写入的字节数
 * @param offset 相对文件的偏移
 * @param fi 可忽略
 * @return int 写入大小
 */
int newfs_write(const char* path, const char* buf, size_t size, off_t offset,
		        struct fuse_file_info* fi) {
	/* 选做 */
	return size;
}

/**
 * @brief 读取文件
 * 
 * @param path 相对于挂载点的路径
 * @param buf 读取的内容
 * @param size 读取的字节数
 * @param offset 相对文件的偏移
 * @param fi 可忽略
 * @return int 读取大小
 */
int newfs_read(const char* path, char* buf, size_t size, off_t offset,
		       struct fuse_file_info* fi) {
	/* 选做 */
	return size;			   
}

/**
 * @brief 删除文件
 * 
 * @param path 相对于挂载点的路径
 * @return int 0成功，否则返回对应错误号
 */
int newfs_unlink(const char* path) {
	/* 选做 */
	return 0;
}

/**
 * @brief 删除目录
 * 
 * 一个可能的删除目录操作如下：
 * rm ./tests/mnt/j/ -r
 *  1) Step 1. rm ./tests/mnt/j/j
 *  2) Step 2. rm ./tests/mnt/j
 * 即，先删除最深层的文件，再删除目录文件本身
 * 
 * @param path 相对于挂载点的路径
 * @return int 0成功，否则返回对应错误号
 */
int newfs_rmdir(const char* path) {
	/* 选做 */
	return 0;
}

/**
 * @brief 重命名文件 
 * 
 * @param from 源文件路径
 * @param to 目标文件路径
 * @return int 0成功，否则返回对应错误号
 */
int newfs_rename(const char* from, const char* to) {
	/* 选做 */
	return 0;
}

/**
 * @brief 打开文件，可以在这里维护fi的信息，例如，fi->fh可以理解为一个64位指针，可以把自己想保存的数据结构
 * 保存在fh中
 * 
 * @param path 相对于挂载点的路径
 * @param fi 文件信息
 * @return int 0成功，否则返回对应错误号
 */
int newfs_open(const char* path, struct fuse_file_info* fi) {
	/* 选做 */
	return 0;
}

/**
 * @brief 打开目录文件
 * 
 * @param path 相对于挂载点的路径
 * @param fi 文件信息
 * @return int 0成功，否则返回对应错误号
 */
int newfs_opendir(const char* path, struct fuse_file_info* fi) {
	/* 选做 */
	return 0;
}

/**
 * @brief 改变文件大小
 * 
 * @param path 相对于挂载点的路径
 * @param offset 改变后文件大小
 * @return int 0成功，否则返回对应错误号
 */
int newfs_truncate(const char* path, off_t offset) {
	/* 选做 */
	return 0;
}


/**
 * @brief 访问文件，因为读写文件时需要查看权限
 * 
 * @param path 相对于挂载点的路径
 * @param type 访问类别
 * R_OK: Test for read permission. 
 * W_OK: Test for write permission.
 * X_OK: Test for execute permission.
 * F_OK: Test for existence. 
 * 
 * @return int 0成功，否则返回对应错误号
 */
int newfs_access(const char* path, int type) {
	/* 选做: 解析路径，判断是否存在 */
	return 0;
}	
/******************************************************************************
* SECTION: FUSE入口
*******************************************************************************/
int main(int argc, char **argv)
{
    int ret;
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

	nfs_options.device = strdup("/home/students/220110309/ddriver");

	if (fuse_opt_parse(&args, &nfs_options, option_spec, NULL) == -1)
		return -1;
	
	ret = fuse_main(args.argc, args.argv, &operations, NULL);
	fuse_opt_free_args(&args);
	return ret;
}