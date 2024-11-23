#include "../include/newfs.h"

extern struct nfs_super      nfs_super; 
extern struct custom_options nfs_options;

/**
 * @brief 获取文件名
 * 
 * @param path 文件路径
 * @return char* 返回文件名
 */
char* nfs_get_fname(const char* path) {
    char ch = '/';  // 定义目录分隔符 '/'
    
    // 使用 strrchr 查找路径中最后一个 '/' 字符，并返回它后面的部分（即文件名）
    char *q = strrchr(path, ch) + 1;  // strrchr 返回指向最后一个 '/' 的指针，+1 使 q 指向文件名的开始位置
    
    return q;  // 返回文件名
}

/**
 * @brief 计算路径的层级
 * exm: /av/c/d/f
 * -> lvl = 4
 * @param path 输入的文件路径
 * @return int 返回路径的层级数
 */
int nfs_calc_lvl(const char * path) {
    char* str = path;  // 定义指针 str 指向路径字符串的开头
    int   lvl = 0;     // 初始化路径层级为 0
    
    if (strcmp(path, "/") == 0) {  // 如果路径是根目录 '/'
        return lvl;  // 根目录的层级为 0
    }
    
    while (*str != '\0') {  // 当字符串还没有结束
        if (*str == '/') {   // 如果当前字符是 '/'
            lvl++;  // 层级数加 1
        }
        str++;  // 指向下一个字符
    }
    return lvl;  // 返回路径的层级数
}

/**
 * @brief 驱动读
 * 
 * @param offset       读取的起始偏移
 * @param out_content  读取内容的输出缓冲区
 * @param size         读取的字节大小
 * @return int         返回状态码，NFS_ERROR_NONE 表示成功
 */
int nfs_driver_read(int offset, uint8_t *out_content, int size) {
    // 计算对齐的偏移和大小
    int      offset_aligned = NFS_ROUND_DOWN(offset, NFS_BLK_SZ());       // 向下对齐到逻辑块大小
    int      bias           = offset - offset_aligned;                   // 偏移量到逻辑块的偏差
    int      size_aligned   = NFS_ROUND_UP((size + bias), NFS_BLK_SZ()); // 向上对齐到逻辑块大小
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);            // 分配临时缓冲区
    uint8_t* cur            = temp_content;                             // 临时缓冲区当前指针

    // 将磁盘指针移动到对齐的偏移位置
    ddriver_seek(NFS_DRIVER(), offset_aligned, SEEK_SET);

    // 按磁盘块大小(512B)循环读取数据
    while (size_aligned != 0) {
        ddriver_read(NFS_DRIVER(), cur, NFS_IO_SZ());                    // 每次读取一个磁盘块
        cur          += NFS_IO_SZ();                                    // 更新临时缓冲区指针
        size_aligned -= NFS_IO_SZ();                                    // 减少剩余读取大小
    }

    // 将读取的有效数据拷贝到输出缓冲区
    memcpy(out_content, temp_content + bias, size);

    // 释放临时缓冲区
    free(temp_content);
    
    return NFS_ERROR_NONE;                                               // 返回成功状态码
}

/**
 * @brief 驱动写
 * 
 * @param offset       写入的起始偏移
 * @param in_content   写入内容的输入缓冲区
 * @param size         写入的字节大小
 * @return int         返回状态码，NFS_ERROR_NONE 表示成功
 */
int nfs_driver_write(int offset, uint8_t *in_content, int size) {
    // 计算对齐的偏移和大小
    int      offset_aligned = NFS_ROUND_DOWN(offset, NFS_BLK_SZ());       // 向下对齐到逻辑块大小
    int      bias           = offset - offset_aligned;                   // 偏移量到逻辑块的偏差
    int      size_aligned   = NFS_ROUND_UP((size + bias), NFS_BLK_SZ()); // 向上对齐到逻辑块大小
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);            // 分配临时缓冲区
    uint8_t* cur            = temp_content;                             // 临时缓冲区当前指针

    // 读出需要的磁盘块到内存
    nfs_driver_read(offset_aligned, temp_content, size_aligned);

    // 在内存中覆盖指定内容
    memcpy(temp_content + bias, in_content, size);

    // 将磁盘指针移动到对齐的偏移位置
    ddriver_seek(NFS_DRIVER(), offset_aligned, SEEK_SET);

    // 将修改后的内容依次写回磁盘
    while (size_aligned != 0) {
        ddriver_write(NFS_DRIVER(), cur, NFS_IO_SZ());                   // 每次写入一个磁盘块
        cur          += NFS_IO_SZ();                                    // 更新临时缓冲区指针
        size_aligned -= NFS_IO_SZ();                                    // 减少剩余写入大小
    }

    // 释放临时缓冲区
    free(temp_content);
    
    return NFS_ERROR_NONE;                                               // 返回成功状态码
}


/**
 * @brief 为一个inode分配dentry，采用头插法，并根据情况分配新的数据块存储dentry
 * 
 * @param inode 目标inode
 * @param dentry 待分配的dentry
 * @param judge 判断是否需要分配新的数据块
 * @return int 返回inode的目录项数量（dir_cnt），失败时返回错误码
 */
int nfs_alloc_dentry(struct nfs_inode* inode, struct nfs_dentry* dentry, int judge) {
    // 头插法，将dentry插入到inode的dentry链表头部
    if (inode->dentrys == NULL) {
        inode->dentrys = dentry;
    } else {
        dentry->brother = inode->dentrys;
        inode->dentrys = dentry;
    }

    inode->dir_cnt++;  // 增加目录项计数

    // 判断是否需要为inode分配新的数据块来存储dentry
    int cur_blk = inode->dir_cnt / NFS_DENTRY_PER_DATABLK();  // 当前应分配的块号
    if (judge == 1 && inode->dir_cnt % NFS_DENTRY_PER_DATABLK() == 1) {
        // 查找空闲数据块
        int byte_cursor = 0;
        int bit_cursor  = 0;
        int dno_cursor  = 500;  // 数据逻辑块号 -- 对应物理块号0
        boolean is_find_free_data_blk = FALSE;

        // 在数据块位图中查找空闲数据块
        for (byte_cursor = 0; byte_cursor < NFS_BLKS_SZ(nfs_super.map_data_blks); byte_cursor++) {
            for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
                if ((nfs_super.map_data[byte_cursor] & (0x1 << bit_cursor)) == 0) {
                    nfs_super.map_data[byte_cursor] |= (0x1 << bit_cursor);  // 标记为已使用
                    is_find_free_data_blk = TRUE;
                    break;
                }
                dno_cursor++;
            }
            if (is_find_free_data_blk) break;
        }

        // 如果未找到空闲数据块，返回错误
        if (!is_find_free_data_blk || dno_cursor == nfs_super.max_data) {
            return -NFS_ERROR_NOSPACE;
        }

        // 将空闲数据块号分配给当前块
        inode->block_pointer[cur_blk] = dno_cursor;
    }

    return inode->dir_cnt;  // 返回更新后的目录项数量
}

/**
 * @brief 分配一个inode，占用位图
 * 
 * @param dentry 该dentry指向分配的inode
 * @return nfs_inode
 */
struct nfs_inode* nfs_alloc_inode(struct nfs_dentry * dentry) {
    struct nfs_inode* inode;
    int byte_cursor = 0; 
    int bit_cursor  = 0; 
    int ino_cursor  = 0;
    boolean is_find_free_entry = FALSE;

    // 查找空闲的inode，在inode位图中遍历
    for (byte_cursor = 0; byte_cursor < NFS_BLKS_SZ(nfs_super.map_inode_blks); byte_cursor++)
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            if((nfs_super.map_inode[byte_cursor] & (0x1 << bit_cursor)) == 0) {    
                // 找到空闲的inode位置
                nfs_super.map_inode[byte_cursor] |= (0x1 << bit_cursor);  // 标记inode为已使用
                is_find_free_entry = TRUE;           
                break;
            }
            ino_cursor++;
        }
        if (is_find_free_entry) {
            break;
        }
    }

    // 如果没有找到空闲inode或者inode数量达到上限，返回错误
    if (!is_find_free_entry || ino_cursor == nfs_super.max_ino)
        return -NFS_ERROR_NOSPACE;

    // 分配新的inode内存
    inode = (struct nfs_inode*)malloc(sizeof(struct nfs_inode));
    inode->ino  = ino_cursor;  // 分配的inode号
    inode->size = 0;           // 初始化文件大小为0
    
    // 将dentry与inode关联
    dentry->inode = inode;     
    dentry->ino   = inode->ino;
    
    // 将inode与dentry关联
    inode->dentry = dentry;
    
    inode->dir_cnt = 0;  // 初始化目录计数器为0
    inode->dentrys = NULL;  // 初始化dentry链表为空
    
    // 如果是常规文件，则为文件分配数据块
    if (NFS_IS_REG(inode)) {
        for(int i = 0; i < NFS_DATA_PER_FILE; i++) {
            inode->data[i] = (uint8_t *)malloc(NFS_BLK_SZ());  // 分配文件数据块
        }
    }

    return inode;
}

/**
 * @brief 将内存 inode 及其下方结构全部刷回磁盘
 * 
 * @param inode       指向需要刷回的内存 inode 的指针
 * @return int        操作状态码，NFS_ERROR_NONE 表示成功
 */
int nfs_sync_inode(struct nfs_inode * inode) {
    struct nfs_inode_d  inode_d;               // 用于存储 inode 的磁盘结构
    struct nfs_dentry*  dentry_cursor;         // 当前目录项指针
    struct nfs_dentry_d dentry_d;              // 用于存储目录项的磁盘结构
    int offset;                                // 当前写入磁盘的偏移量
    int ino = inode->ino;                      // inode 编号

    /* 将内存中的 inode 刷回磁盘的 inode_d */
    inode_d.ino     = ino;                     // 设置 inode 编号
    inode_d.size    = inode->size;             // 设置 inode 文件大小
    inode_d.ftype   = inode->dentry->ftype;    // 设置文件类型
    inode_d.dir_cnt = inode->dir_cnt;          // 设置目录项计数
    for (int i = 0; i < NFS_DATA_PER_FILE; i++) {
        inode_d.block_pointer[i] = inode->block_pointer[i]; // 拷贝数据块指针
    }
    
    // 写入 inode_d 到磁盘
    if (nfs_driver_write(NFS_INO_OFS(ino), (uint8_t *)&inode_d, sizeof(struct nfs_inode_d)) != NFS_ERROR_NONE) {
        NFS_DBG("[%s] io error\n", __func__);
        return -NFS_ERROR_IO;                 // 写入失败，返回错误码
    }

    /* 
     * Cycle 1: 写入 INODE 
     * Cycle 2: 写入数据
     */

    /* 如果是目录类型，则写回目录项，并递归刷写每个目录项对应的 inode 节点 */
    if (NFS_IS_DIR(inode)) {    
        int blk_number = 0;                   // 当前数据块编号
        dentry_cursor = inode->dentrys;       // 获取目录项链表头指针

        // 将目录项写入最多 6 个磁盘块
        while (dentry_cursor != NULL && blk_number < NFS_DATA_PER_FILE) {
            offset = NFS_DATA_OFS(inode->block_pointer[blk_number] - 500); // 当前数据块的起始偏移
            while ((dentry_cursor != NULL) && 
                   (offset + sizeof(struct nfs_dentry_d) < NFS_DATA_OFS(inode->block_pointer[blk_number] - 500) + NFS_BLK_SZ())) {
                
                // 将内存中 dentry_cursor 指向的目录项写入磁盘
                memcpy(dentry_d.fname, dentry_cursor->fname, NFS_MAX_FILE_NAME); // 拷贝文件名
                dentry_d.ftype = dentry_cursor->ftype;                          // 设置文件类型
                dentry_d.ino   = dentry_cursor->ino;                            // 设置 inode 编号
                if (nfs_driver_write(offset, (uint8_t *)&dentry_d, sizeof(struct nfs_dentry_d)) != NFS_ERROR_NONE) {
                    NFS_DBG("[%s] io error\n", __func__);
                    return -NFS_ERROR_IO;      // 写入失败，返回错误码
                }
                
                // 递归同步目录项对应的 inode
                if (dentry_cursor->inode != NULL) {
                    nfs_sync_inode(dentry_cursor->inode);
                }

                dentry_cursor = dentry_cursor->brother; // 移动到下一个兄弟目录项
                offset += sizeof(struct nfs_dentry_d);  // 偏移量递增
            }
            blk_number++;                      // 切换到下一个数据块
        }
    }
    /* 如果是文件类型，则将 inode 指向的数据直接写入磁盘 */
    else if (NFS_IS_REG(inode)) {
        for (int i = 0; i < NFS_DATA_PER_FILE; i++) {
            if (nfs_driver_write(NFS_DATA_OFS(inode->block_pointer[i] - 500), 
                                 inode->data[i], NFS_BLK_SZ()) != NFS_ERROR_NONE) {
                NFS_DBG("[%s] io error\n", __func__);
                return -NFS_ERROR_IO;          // 写入失败，返回错误码
            }
        }
    }
    return NFS_ERROR_NONE;                    // 返回成功状态码
}

/**
 * @brief 从磁盘读取 inode 并加载到内存中
 * 
 * @param dentry 指向 inode 的目录项
 * @param ino    inode 的唯一编号
 * @return struct nfs_inode* 返回加载到内存的 inode 结构，失败返回 NULL
 */
struct nfs_inode* nfs_read_inode(struct nfs_dentry * dentry, int ino) {
    struct nfs_inode* inode = (struct nfs_inode*)malloc(sizeof(struct nfs_inode)); // 分配内存给 inode
    struct nfs_inode_d inode_d;             // 用于读取磁盘中的 inode 结构
    struct nfs_dentry* sub_dentry;          // 子目录项指针
    struct nfs_dentry_d dentry_d;           // 用于读取磁盘中的目录项
    int dir_cnt = 0;                        // 目录项计数

    // 从磁盘读取 inode 数据
    if (nfs_driver_read(NFS_INO_OFS(ino), (uint8_t *)&inode_d, sizeof(struct nfs_inode_d)) != NFS_ERROR_NONE) {
        NFS_DBG("[%s] io error\n", __func__);
        return NULL; // 读取失败，返回 NULL
    }

    /* 根据磁盘中的 inode_d 初始化内存中的 inode */
    inode->ino       = inode_d.ino;         // 设置 inode 编号
    inode->size      = inode_d.size;        // 设置文件大小
    inode->dentry    = dentry;              // 设置对应的目录项
    inode->dentrys   = NULL;                // 初始化目录项链表为空
    inode->dir_cnt   = 0;                   // 初始化目录项计数为 0
    for (int i = 0; i < NFS_DATA_PER_FILE; i++) {
        inode->block_pointer[i] = inode_d.block_pointer[i]; // 复制数据块指针
    }

    /* 判断 inode 的文件类型 */
    if (NFS_IS_DIR(inode)) { // 如果是目录类型
        dir_cnt = inode_d.dir_cnt;          // 获取目录项计数
        int blk_number = 0;                 // 当前处理的数据块编号
        int offset;                         // 当前磁盘偏移量

        /* 遍历目录项，将其加载到内存 */
        while (dir_cnt > 0 && blk_number < NFS_DATA_PER_FILE) {
            offset = NFS_DATA_OFS(inode->block_pointer[blk_number] - 500); // 计算当前块的起始偏移

            // 遍历磁盘中当前数据块的目录项
            while ((dir_cnt > 0) && 
                   (offset + sizeof(struct nfs_dentry_d) < NFS_DATA_OFS(inode->block_pointer[blk_number] - 500) + NFS_BLK_SZ())) {
                // 从磁盘读取一个目录项
                if (nfs_driver_read(offset, (uint8_t *)&dentry_d, sizeof(struct nfs_dentry_d)) != NFS_ERROR_NONE) {
                    NFS_DBG("[%s] io error\n", __func__);
                    return NULL; // 读取失败，返回 NULL
                }

                // 根据磁盘中的目录项创建一个内存中的子目录项
                sub_dentry = new_dentry(dentry_d.fname, dentry_d.ftype); // 创建目录项
                sub_dentry->parent = inode->dentry;                     // 设置父目录项
                sub_dentry->ino    = dentry_d.ino;                      // 设置 inode 编号
                nfs_alloc_dentry(inode, sub_dentry, 0);                 // 将目录项添加到 inode 的目录链表中

                offset += sizeof(struct nfs_dentry_d); // 偏移量移动到下一个目录项
                dir_cnt--;                            // 剩余目录项计数减 1
            }
            blk_number++; // 处理下一个数据块
        }
    } 
    else if (NFS_IS_REG(inode)) { // 如果是文件类型
        /* 直接读取文件数据到内存 */
        for (int i = 0; i < NFS_DATA_PER_FILE; i++) {
            inode->data[i] = (uint8_t *)malloc(NFS_BLK_SZ()); // 分配内存用于存储数据块
            if (nfs_driver_read(NFS_DATA_OFS(inode->block_pointer[i] - 500), 
                                (uint8_t *)inode->data[i], NFS_BLK_SZ()) != NFS_ERROR_NONE) {
                NFS_DBG("[%s] io error\n", __func__);
                return NULL; // 读取失败，返回 NULL
            }
        }
    }

    return inode; // 返回加载完成的 inode
}

/**
 * @brief 
 * 
 * @param inode 
 * @param dir [0...]
 * @return struct nfs_dentry* 
 */
struct nfs_dentry* nfs_get_dentry(struct nfs_inode * inode, int dir) {
    struct nfs_dentry* dentry_cursor = inode->dentrys;
    int    cnt = 0;
    while (dentry_cursor)
    {
        if (dir == cnt) {
            return dentry_cursor;
        }
        cnt++;
        dentry_cursor = dentry_cursor->brother;
    }
    return NULL;
}

/**
 * @brief 查找路径对应的目录项（dentry），若未找到则返回上一级目录项
 * 
 * 示例解析：
 * - 路径: /qwe/ad, total_lvl = 2
 *   1) 查找 / 的 inode             -> lvl = 1
 *   2) 查找 qwe 的 dentry         
 *   3) 查找 qwe 的 inode           -> lvl = 2
 *   4) 查找 ad 的 dentry
 * 
 * - 路径: /qwe, total_lvl = 1
 *   1) 查找 / 的 inode             -> lvl = 1
 *   2) 查找 qwe 的 dentry
 * 
 * @param path 路径字符串
 * @param is_find 指针，用于标识是否找到目标目录项
 * @param is_root 指针，用于标识路径是否为根目录
 * @return struct nfs_dentry* 返回找到的目录项，或其上一级目录项
 */
struct nfs_dentry* nfs_lookup(const char * path, boolean* is_find, boolean* is_root) {
    struct nfs_dentry* dentry_cursor = nfs_super.root_dentry;// 当前处理的目录项，从根目录开始
    struct nfs_dentry* dentry_ret = NULL;                    // 最终返回的目录项
    struct nfs_inode*  inode;                                // 当前处理的 inode
    int total_lvl = nfs_calc_lvl(path);                      // 计算路径的层级数
    int lvl = 0;                                             // 当前处理的层级数
    boolean is_hit;                                          // 是否命中目录项
    char* fname = NULL;                                      // 当前层级的文件或文件夹名称
    char* path_cpy = (char*)malloc(sizeof(path));            // 路径副本，避免修改原路径
    *is_root = FALSE;                                        // 初始化为非根目录
    strcpy(path_cpy, path);                                  // 复制路径字符串

    // 如果路径层级为 0，表示路径为根目录，直接返回根目录项
    if (total_lvl == 0) {                            
        *is_find = TRUE;
        *is_root = TRUE;
        dentry_ret = nfs_super.root_dentry;
    }

    // 获取路径的第一个文件夹名称
    fname = strtok(path_cpy, "/");       
    while (fname) {   
        lvl++; // 当前层级数增加

        // 若当前目录项的 inode 为空，从磁盘读取 inode
        if (dentry_cursor->inode == NULL) {           
            nfs_read_inode(dentry_cursor, dentry_cursor->ino);
        }

        inode = dentry_cursor->inode; // 获取当前目录项对应的 inode

        // 如果 inode 是文件类型且未查找到目标层级，路径错误，返回上一级
        if (NFS_IS_REG(inode) && lvl < total_lvl) {
            NFS_DBG("[%s] not a dir\n", __func__);
            dentry_ret = inode->dentry;
            break;
        }

        // 如果 inode 是目录类型
        if (NFS_IS_DIR(inode)) {
            dentry_cursor = inode->dentrys; // 进入该目录的子目录项链表
            is_hit = FALSE;                // 初始化命中标志

            // 遍历目录项链表，查找匹配的名称
            while (dentry_cursor) {
                if (memcmp(dentry_cursor->fname, fname, strlen(fname)) == 0) { // 名称匹配
                    is_hit = TRUE;
                    break;
                }
                dentry_cursor = dentry_cursor->brother; // 查找下一个目录项
            }
            
            // 如果未命中，路径错误，返回上一级目录项
            if (!is_hit) {
                *is_find = FALSE;
                NFS_DBG("[%s] not found %s\n", __func__, fname);
                dentry_ret = inode->dentry;
                break;
            }

            // 如果命中且已达到目标层级，返回该目录项
            if (is_hit && lvl == total_lvl) {
                *is_find = TRUE;
                dentry_ret = dentry_cursor;
                break;
            }
        }

        // 获取路径的下一层文件夹名称
        fname = strtok(NULL, "/"); 
    }

    // 若返回的目录项的 inode 未加载，则从磁盘读取
    if (dentry_ret && dentry_ret->inode == NULL) {
        dentry_ret->inode = nfs_read_inode(dentry_ret, dentry_ret->ino);
    }
    
    return dentry_ret; // 返回查找到的目录项或上一级目录项
}

/**
 * @brief 挂载NFS文件系统并初始化必要的结构。
 * 
 * 该函数执行以下任务：
 * 1. 打开设备并获取必要的信息。
 * 2. 检查文件系统是否为首次挂载。
 * 3. 初始化超级块和位图（包括inode和数据块）。
 * 4. 创建根目录并分配根inode。
 * 5. 设置必要的结构并挂载文件系统。
 * 
 * 注意：16个Inode占用一个块（Blk）。
 * 
 * @param options 配置挂载操作的选项。
 * @return int 成功时返回0，失败时返回负错误代码。
 */
int nfs_mount(struct custom_options options) {
    int ret = NFS_ERROR_NONE;           // 函数返回值，标记成功或失败
    int driver_fd;                      // 设备驱动文件描述符
    struct nfs_super_d nfs_super_d;     // 内存中的超级块结构
    struct nfs_dentry* root_dentry;     // 根目录项指针
    struct nfs_inode* root_inode;       // 根inode指针

    int inode_num;                      // inode数量
    int map_inode_blks;                 // inode位图块数量

    int data_num;                       // 数据块数量
    int map_data_blks;                  // 数据块位图块数量

    int super_blks;                     // 超级块数量
    boolean is_init = FALSE;            // 是否为首次挂载标记

    // 标记文件系统未挂载
    nfs_super.is_mounted = FALSE;

    // 打开设备驱动并获取驱动文件描述符
    driver_fd = ddriver_open(options.device);
    if (driver_fd < 0) {
        return driver_fd;  // 如果设备打开失败，返回错误代码
    }

    // 向超级块中写入设备信息
    nfs_super.driver_fd = driver_fd;
    ddriver_ioctl(NFS_DRIVER(), IOC_REQ_DEVICE_SIZE,  &nfs_super.sz_disk);
    ddriver_ioctl(NFS_DRIVER(), IOC_REQ_DEVICE_IO_SZ, &nfs_super.sz_io);
    nfs_super.sz_blks = 2 * nfs_super.sz_io;  // 计算块大小
    
    // 创建根目录项
    root_dentry = new_dentry("/", NFS_DIR);

    // 读取磁盘超级块nfs_super_d到内存中
    if (nfs_driver_read(NFS_SUPER_OFS, (uint8_t *)(&nfs_super_d), sizeof(struct nfs_super_d)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;  // 读取超级块失败
    }

    // 检查超级块中的幻数，判断是否为首次挂载
    if (nfs_super_d.magic_num != NFS_MAGIC_NUM) {  // 幻数不匹配，表示首次挂载
        // 估算各部分大小
        super_blks = NFS_SUPER_BLKS;
        inode_num  = NFS_INODE_BLKS;
        data_num = NFS_DATA_BLKS;
        map_inode_blks = NFS_MAP_INODE_BLKS;
        map_data_blks = NFS_MAP_DATA_BLKS;

        // 设置超级块布局
        nfs_super.max_ino = inode_num;
        nfs_super.max_data = data_num;

        nfs_super_d.map_inode_blks = map_inode_blks; 
        nfs_super_d.map_data_blks = map_data_blks; 

        nfs_super_d.map_inode_offset = NFS_SUPER_OFS + NFS_BLKS_SZ(super_blks);
        nfs_super_d.map_data_offset = nfs_super_d.map_inode_offset + NFS_BLKS_SZ(map_inode_blks);

        nfs_super_d.inode_offset = nfs_super_d.map_data_offset + NFS_BLKS_SZ(map_data_blks);
        nfs_super_d.data_offset = nfs_super_d.inode_offset + NFS_BLKS_SZ(inode_num);

        nfs_super_d.sz_usage = 0;
        nfs_super_d.magic_num = NFS_MAGIC_NUM;

        is_init = TRUE;  // 标记为首次初始化
    }

    /* 创建内存中的结构 */
    // 初始化超级块信息
    nfs_super.sz_usage = nfs_super_d.sz_usage;

    // 创建inode位图
    nfs_super.map_inode = (uint8_t *)malloc(NFS_BLKS_SZ(nfs_super_d.map_inode_blks));
    nfs_super.map_inode_blks = nfs_super_d.map_inode_blks;
    nfs_super.map_inode_offset = nfs_super_d.map_inode_offset;
    nfs_super.inode_offset = nfs_super_d.inode_offset;

    // 创建数据块位图
    nfs_super.map_data = (uint8_t *)malloc(NFS_BLKS_SZ(nfs_super_d.map_data_blks));
    nfs_super.map_data_blks = nfs_super_d.map_data_blks;
    nfs_super.map_data_offset = nfs_super_d.map_data_offset;
    nfs_super.data_offset = nfs_super_d.data_offset;

    // 读取inode位图到内存
    if (nfs_driver_read(nfs_super_d.map_inode_offset, (uint8_t *)(nfs_super.map_inode), 
                        NFS_BLKS_SZ(nfs_super_d.map_inode_blks)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;  // 读取inode位图失败
    }

    // 读取数据块位图到内存
    if (nfs_driver_read(nfs_super_d.map_data_offset, (uint8_t *)(nfs_super.map_data), 
                        NFS_BLKS_SZ(nfs_super_d.map_data_blks)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;  // 读取数据块位图失败
    }

    // 如果是首次挂载，则分配根节点
    if (is_init) {
        root_inode = nfs_alloc_inode(root_dentry);  // 分配根inode
        nfs_sync_inode(root_inode);  // 同步根inode
    }

    // 读取根inode
    root_inode = nfs_read_inode(root_dentry, NFS_ROOT_INO);
    root_dentry->inode = root_inode;  // 将根inode与根目录项关联
    nfs_super.root_dentry = root_dentry;  // 将根目录项与超级块关联
    nfs_super.is_mounted = TRUE;  // 设置文件系统为已挂载

    return ret;  // 返回挂载操作结果
}

/**
 * @brief 卸载NFS文件系统并释放资源
 * 
 * 该函数执行以下任务：
 * 1. 确保文件系统已挂载，如果未挂载，则直接返回。
 * 2. 刷写根目录的inode信息。
 * 3. 将内存中的超级块nfs_super更新并写回磁盘。
 * 4. 刷写inode位图和数据位图到磁盘。
 * 5. 释放内存中的位图结构。
 * 6. 关闭设备驱动。
 * 
 * @return int 如果操作成功，返回NFS_ERROR_NONE；否则返回错误码。
 */
int nfs_umount() {
    struct nfs_super_d nfs_super_d;  // 用于存储即将写回磁盘的超级块

    // 如果文件系统未挂载，直接返回
    if (!nfs_super.is_mounted) {
        return NFS_ERROR_NONE;
    }

    // 刷写根目录inode，确保数据同步到磁盘
    nfs_sync_inode(nfs_super.root_dentry->inode);

    // 用内存中的超级块信息更新nfs_super_d
    nfs_super_d.magic_num          = NFS_MAGIC_NUM;                // 超级块的魔术数
    nfs_super_d.sz_usage           = nfs_super.sz_usage;           // 文件系统使用情况
    nfs_super_d.map_inode_blks     = nfs_super.map_inode_blks;     // inode位图的块数
    nfs_super_d.map_inode_offset   = nfs_super.map_inode_offset;   // inode位图的偏移量
    nfs_super_d.inode_offset       = nfs_super.inode_offset;       // inode表的偏移量
    nfs_super_d.map_data_blks      = nfs_super.map_data_blks;      // 数据位图的块数
    nfs_super_d.map_data_offset    = nfs_super.map_data_offset;    // 数据位图的偏移量
    nfs_super_d.data_offset        = nfs_super.data_offset;        // 数据块的偏移量

    // 将更新后的超级块写回磁盘
    if (nfs_driver_write(NFS_SUPER_OFS, (uint8_t *)&nfs_super_d, sizeof(struct nfs_super_d)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;  // 如果写入失败，返回IO错误
    }

    // 将inode位图写回磁盘
    if (nfs_driver_write(nfs_super_d.map_inode_offset, (uint8_t *)(nfs_super.map_inode), NFS_BLKS_SZ(nfs_super_d.map_inode_blks)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;  // 如果写入失败，返回IO错误
    }

    // 将数据位图写回磁盘
    if (nfs_driver_write(nfs_super_d.map_data_offset, (uint8_t *)(nfs_super.map_data), NFS_BLKS_SZ(nfs_super_d.map_data_blks)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;  // 如果写入失败，返回IO错误
    }

    // 释放内存中的inode和数据位图
    free(nfs_super.map_inode);
    free(nfs_super.map_data);

    // 关闭NFS驱动
    ddriver_close(NFS_DRIVER());

    return NFS_ERROR_NONE;  // 返回成功标志
}

