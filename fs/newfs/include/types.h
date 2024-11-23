#ifndef _TYPES_H_
#define _TYPES_H_

/******************************************************************************
* SECTION: Type def
*******************************************************************************/
typedef int          boolean;
typedef uint16_t     flag16;

typedef enum nfs_file_type {
    NFS_REG_FILE,       // 普通文件
    NFS_DIR,            // 目录文件
    // NFS_SYM_LINK     // 软链接--暂时不实现
} NFS_FILE_TYPE;
/******************************************************************************
* SECTION: Macro
*******************************************************************************/
#define TRUE                    1
#define FALSE                   0
#define UINT32_BITS             32
#define UINT8_BITS              8

#define NFS_MAGIC_NUM           0x52415453  
#define NFS_SUPER_OFS           0
#define NFS_ROOT_INO            0



#define NFS_ERROR_NONE          0
#define NFS_ERROR_ACCESS        EACCES
#define NFS_ERROR_SEEK          ESPIPE     
#define NFS_ERROR_ISDIR         EISDIR
#define NFS_ERROR_NOSPACE       ENOSPC
#define NFS_ERROR_EXISTS        EEXIST
#define NFS_ERROR_NOTFOUND      ENOENT
#define NFS_ERROR_UNSUPPORTED   ENXIO
#define NFS_ERROR_IO            EIO     /* Error Input/Output */
#define NFS_ERROR_INVAL         EINVAL  /* Invalid Args */

#define NFS_MAX_FILE_NAME       128
#define NFS_INODE_PER_FILE      16
#define NFS_DATA_PER_FILE       6       // 采用直接索引方式，且固定分配6个数据块
#define NFS_DEFAULT_PERM        0777

#define NFS_IOC_MAGIC           'S'
#define NFS_IOC_SEEK            _IO(NFS_IOC_MAGIC, 0)

#define NFS_FLAG_BUF_DIRTY      0x1
#define NFS_FLAG_BUF_OCCUPY     0x2

/* 磁盘布局设计 */
#define NFS_SUPER_BLKS          1       // 超级块占1个逻辑块
#define NFS_MAP_INODE_BLKS      1       // 索引节点位图占1个逻辑块
#define NFS_MAP_DATA_BLKS       1       // 数据块位图占1个逻辑块
#define NFS_INODE_BLKS          585     
#define NFS_DATA_BLKS           3508  

/******************************************************************************
* SECTION: Macro Function
*******************************************************************************/
// 获取大小信息
#define NFS_IO_SZ()                     (nfs_super.sz_io)
#define NFS_DISK_SZ()                   (nfs_super.sz_disk)
#define NFS_BLK_SZ()                    (nfs_super.sz_blks)
#define NFS_DRIVER()                    (nfs_super.driver_fd)
#define NFS_BLKS_SZ(blks)               ((blks) * NFS_BLK_SZ())
#define NFS_DENTRY_PER_DATABLK()        (NFS_BLK_SZ() / sizeof(struct nfs_dentry_d))  //计算一个磁盘块可以储存多少dentry_d

// 向上取整及向下取整
#define NFS_ROUND_DOWN(value, round)    ((value) % (round) == 0 ? (value) : ((value) / (round)) * (round))
#define NFS_ROUND_UP(value, round)      ((value) % (round) == 0 ? (value) : ((value) / (round) + 1) * (round))

// 设置文件名称
#define NFS_ASSIGN_FNAME(pnfs_dentry, _fname)  memcpy(pnfs_dentry->fname, _fname, strlen(_fname))

// 计算偏移
#define NFS_INO_OFS(ino)                (nfs_super.inode_offset + NFS_BLKS_SZ(ino))
#define NFS_DATA_OFS(dno)               (nfs_super.data_offset + NFS_BLKS_SZ(dno))

// 判断inode类型
#define NFS_IS_DIR(pinode)              (pinode->dentry->ftype == NFS_DIR)
#define NFS_IS_REG(pinode)              (pinode->dentry->ftype == NFS_REG_FILE)
// #define NFS_IS_SYM_LINK(pinode)         (pinode->dentry->ftype == NFS_SYM_LINK)      // 软链接--暂时不实现
/******************************************************************************
* SECTION: FS Specific Structure - In memory structure
*******************************************************************************/
struct custom_options {
	const char*        device;
	boolean            show_help;
};

struct nfs_inode        // 2-索引节点
{ 
    u_int32_t           ino;                             // 索引编号
    int                 size;                            // 文件已占用空间
    int                 link;                            // 链接数，默认为1
    NFS_FILE_TYPE       ftype;                           // 文件类型（目录类型、普通文件类型）
    int                 block_pointer[NFS_DATA_PER_FILE];// 数据块块号（可固定分配）
    struct nfs_dentry*  dentry;                          // 指向该inode的dentry
    struct nfs_dentry*  dentrys;                         // 所有目录项
    uint8_t*            data[NFS_DATA_PER_FILE];         // 指向数据块的指针
    int                 dir_cnt;                         // 如果是目录类型，记录下面有几个目录项           
};   

struct nfs_dentry       // 3-目录项
{ 
    char                fname[NFS_MAX_FILE_NAME];       // 文件名
    struct nfs_dentry*  parent;                         // 父亲Inode的dentry
    struct nfs_dentry*  brother;                        // 兄弟的dentry
    u_int32_t           ino;                            // 指向的inode号
    struct nfs_inode*   inode;                          // 指向的inode
    NFS_FILE_TYPE       ftype;                          // 文件类型
}; 

struct nfs_super        // 1-超级块
{
    uint32_t            magic_num;          // 幻数
    int                 driver_fd;          // 设备描述符

    int                 sz_io;              // 驱动的IO大小：512B
    int                 sz_blks;            // EXT2的磁盘块大小：1024B
    int                 sz_disk;            // 虚拟磁盘容量：4MB
    int                 sz_usage;

    int                max_ino;             // 索引节点最大数目
    uint8_t*           map_inode;           // inode位图
    int                map_inode_blks;      // inode位图所占的数据块
    int                map_inode_offset;    // inode位图的起始地址
    int                inode_offset; 
    
    int                max_data;            // 数据块最大数目
    uint8_t*           map_data;            // data位图
    int                map_data_offset;     // data位图的起始地址
    int                map_data_blks;       // data位图所占的块数
    int                data_offset;         // 数据块的起始地址

    boolean            is_mounted;          // 是否挂载

    struct nfs_dentry* root_dentry;         // 根目录
};

/* 用于创建新的目录项 */
static inline struct nfs_dentry* new_dentry(char * fname, NFS_FILE_TYPE ftype) {
    struct nfs_dentry * dentry = (struct nfs_dentry *)malloc(sizeof(struct nfs_dentry));
    memset(dentry, 0, sizeof(struct nfs_dentry));
    NFS_ASSIGN_FNAME(dentry, fname);
    dentry->ftype   = ftype;
    dentry->ino     = -1;
    dentry->inode   = NULL;
    dentry->parent  = NULL;
    dentry->brother = NULL;  
    return dentry;                                          
}
/******************************************************************************
* SECTION: FS Specific Structure - Disk structure
*******************************************************************************/
struct nfs_super_d
{
    uint32_t            magic_num;                      // 幻数
    int                 sz_usage;   
    
    int                 map_inode_blks;                 // inode位图所占的块数
    int                 map_inode_offset;               // inode位图的起始地址
        
    int                 map_data_blks;                  // data位图所占的块数
    int                 map_data_offset;                // data位图的起始地址
    
    int                 data_offset;                    // 数据块的起始地址                    
    int                 inode_offset;                   // 索引节点的起始地址
};

struct nfs_inode_d
{
    u_int32_t           ino;                             // 索引编号
    int                 size;                            // 文件已占用空间
    int                 link;                            // 链接数，默认为1
    NFS_FILE_TYPE       ftype;                           // 文件类型（目录类型、普通文件类型）
    int                 block_pointer[NFS_DATA_PER_FILE];// 数据块块号（可固定分配）
    int                 dir_cnt;                         // 如果是目录类型，记录下面有几个目录项 
};  

struct nfs_dentry_d
{
    char                fname[NFS_MAX_FILE_NAME];        // 文件名          
    NFS_FILE_TYPE       ftype;                           // 文件类型
    int                 ino;                             // 指向的ino号
};  


#endif /* _TYPES_H_ */