## MYFS

##### Ziwei Wu



#### 基本属性：

- 总存储空间为4GB(实际为3.997G)
-  blocksize大小为4KB
- 文件名称限长255字节
- 最多能存放的文件数目：1024



### 数据结构

- super_block：存放文件系统元数据

```c
typedef struct {
  ull total_block;// = BLOCK_NUM;
  ull used_block;// = SUPER_BLOCK_NUM + DIRTY_BLOCK_NUM + FILENODE_NUM;
  ull file_first_clean; // = FIRST_FILENODE_IDX
  ull data_first_clean;// = FIRST_DATABLOCK_IDX;
}super_block;
```

- filenode: 存放文件名、文件mode\uid\gid\size\nlink\atime\mtime元数据和对应的第一个data_block编号。

```c
typedef struct {
  mode_t st_mode;
  uid_t st_uid;
  gid_t st_gid;
  off_t st_size;
  nlink_t st_nlink;
  time_t atime;
  time_t mtime;
}mystat;

typedef struct {
  char filename[NAMELENGTH];
  mystat st;
  ull first_block;
} filenode;
```

- data_block：存放数据的block，存放内容和下一个data_block的编号，若是最后一个则为它自己。

```c
typedef struct {
  ull nextnum;
  char content[CONTENT_SIZE];
}data_block;
```

- dirty_block: 存放各个block被占用的情况

```c
typedef struct {
  char dirty[BLOCK_SIZE];
}dirty_block;
```



### 内存分配算法

我在文件的super_block中保存了**file_first_clean**（第一块可用的filenode编号）, **data_first_clean**（第一块可用的data_block编号）。这两个设计使得后续查找时**非常便捷**：

- filenode

  - 新建filenode时，通过判断 file_first_clean 是否小于 FIRST_DATABLOCK_IDX 来决定是否能够创建filenode.
  - 能够创建的话，该filenode的编号就是file_first_clean，创建完后通过检查dirty位是否为空更新file_first_clean。
  - 若要删除filenode，则将当前filenode的编号与super_block中的file_first_clean比较，将较小者更新为file_first_clean。

- data_block

  - 新建文件内容时，通过判断剩余可用data_block数是否足够来决定是否能够写入该文件。
  - 若能写入，将data_first_clean块进行mmap，通过检查dirty位来更新data_first_clean。
  - 若要删除data_block，则将当前data_block块的编号与data_first_clean比较，将较小者更新为data_first_clean。

  这样做免去了很多遍历操作，很方便。



### 实现功能与评价

- 基本功能：创建、读、写、删除、截断
- 修改文件元数据、文件系统元数据
- 虽然没有实现目录功能，但是只要在filenode里稍作修改即可实现。本实验的拓展性很好，只要对数据结构作适当修改就能拓展功能。
- 我的实验思路简单易懂，在代码中加了很多注释，变量命名也很直观，对一些特殊情况的处理说明也很详细。



### 性能分析：

在处理小文件时，本系统的存储速率约为200MB/s, 读取速率约为700MB/s。

随着文件的增大，性能降低。文件大小每增加一个数量级，速率约降为原来的1/5倍。

本实验主要操作是数组上的操作，因为一开始设计实验时考虑到性能，没有选择链表操作。虽然通过一些设计减少了遍历，但是在处理大文件时性能还是一般。
