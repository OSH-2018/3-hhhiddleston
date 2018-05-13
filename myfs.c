#define FUSE_USE_VERSION 26
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/mman.h>

#define BLOCK_SIZE (4*1024)
#define BLOCK_NUM (1024*1024)
#define SUPER_BLOCK_NUM 1
#define DIRTY_BLOCK_NUM (8*BLOCK_NUM / BLOCK_SIZE)
#define FILENODE_NUM 1024
#define FIRST_FILENODE_IDX (SUPER_BLOCK_NUM+DIRTY_BLOCK_NUM)
#define FIRST_DATABLOCK_IDX (SUPER_BLOCK_NUM+DIRTY_BLOCK_NUM+FILENODE_NUM)
#define CONTENT_SIZE (BLOCK_SIZE-sizeof(int)-sizeof(ull))
#define NAMELENGTH 255

#define min(a,b) ({__typeof__(a) _a=(a); __typeof__(b) _b=(b); _a<_b?_a:_b;})

void* mem[BLOCK_NUM];
char dirty[BLOCK_NUM];

typedef unsigned long long ull;

typedef struct {
  ull total_block;// = BLOCK_NUM;
  ull used_block;// = SUPER_BLOCK_NUM + DIRTY_BLOCK_NUM + FILENODE_NUM;
  ull file_first_clean; // = FIRST_FILENODE_IDX
  ull data_first_clean;// = FIRST_DATABLOCK_IDX;
}super_block;

typedef struct {
  ull nextnum;
  char content[CONTENT_SIZE];
}data_block;

typedef struct {
  char dirty[BLOCK_SIZE/8];
}dirty_block;

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

static ull get_filenode(const char* path){
  // if exits : return block_idx
  // else: return 0
  ull i;
  for(i=FIRST_FILENODE_IDX;i<FIRST_DATABLOCK_IDX;i++){
    if(dirty[i]==1)
      if(strcmp(((filenode*)mem[i])->filename, path+1) == 0)
        return i;
  }
  return 0;
}

static int my_mknod(const char* path, mode_t mode, dev_t dev){
  mystat st;
  // set stat
  st.st_mode = S_IFREG | 0644;
  st.st_uid = fuse_get_context()->uid;
  st.st_gid = fuse_get_context()->gid;
  st.st_nlink = 1;
  st.st_size = 0;
  time(&st.atime);
  time(&st.mtime);
  // create filenode
  ull idx = ((super_block*)mem[0])->file_first_clean;
  ////printf("%lld\n", idx);

  // no free filenode
  if(idx>=FIRST_DATABLOCK_IDX)
    return -errno;
  // create block
  mem[idx] = (filenode*)mmap(NULL,BLOCK_SIZE,PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  ////printf("FILENODE-CREATE-DONE\n");

  // assign filenode
  memcpy(((filenode*)mem[idx])->filename, path+1, strlen(path+1)+1);
  memcpy(&(((filenode*)mem[idx])->st), &st, sizeof(mystat));
  ull first_block = 0;
  memcpy(&(((filenode*)mem[idx])->first_block), &first_block, sizeof(ull));
  // dirty the filenode_block
  dirty[idx] = 1;
  // modify supernode
  // update first clean filenode block
  ull tmp;
  for(tmp=FIRST_FILENODE_IDX;tmp<FIRST_DATABLOCK_IDX;tmp++){
    if(dirty[tmp]==0){
      ((super_block*)((super_block*)mem[0]))->file_first_clean = tmp;
      break;
    }
  }

  ////printf("MKNOD-DONE\n");

  return 0;
}

static void* my_init(struct fuse_conn_info *conn){
  // super_block
  mem[0] = (super_block*)mmap(NULL,BLOCK_SIZE,PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  // init
  ((super_block*)((super_block*)mem[0]))->total_block = BLOCK_NUM;
  ((super_block*)((super_block*)mem[0]))->used_block = SUPER_BLOCK_NUM + DIRTY_BLOCK_NUM + FILENODE_NUM;
  ((super_block*)((super_block*)mem[0]))->file_first_clean = FIRST_FILENODE_IDX;
  ((super_block*)mem[0])->data_first_clean = FIRST_DATABLOCK_IDX;
  // dirty blocks
  ull i;
  for(i=1;i<FIRST_FILENODE_IDX;i++){
    mem[i] = (dirty_block*)mmap(NULL,BLOCK_SIZE,PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  }
  // init
  memset(dirty,0,BLOCK_NUM);
  ////printf("INIT-DONE.\n");
  return NULL;
}

static int my_getattr(const char* path, struct stat * stbuf){
  ////printf("GETATTR-%s\n----------\n",path);
  ull idx = get_filenode(path);
  ////printf("%lld\n", idx);
  if(strcmp(path, "/") == 0) {
    memset(stbuf, 0, sizeof(struct stat));
    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_uid = fuse_get_context()->uid;
    stbuf->st_gid = fuse_get_context()->gid;
    return 0;
  }
  else if(idx!=0){
    memset(stbuf, 0, sizeof(struct stat));
    stbuf->st_mode = S_IFREG | 0644;
    stbuf->st_uid = ((filenode*)mem[idx])->st.st_uid;
    stbuf->st_gid = ((filenode*)mem[idx])->st.st_gid;
    stbuf->st_size = ((filenode*)mem[idx])->st.st_size;
    stbuf->st_nlink = ((filenode*)mem[idx])->st.st_nlink;
    stbuf->st_atime = ((filenode*)mem[idx])->st.atime;
    stbuf->st_mtime = ((filenode*)mem[idx])->st.mtime;
    return 0;
  }
  ////printf("GETATTR-DONE.\n");
  return -ENOENT;
}

static int my_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
off_t offset, struct fuse_file_info *fi){
  ////printf("READDIR--%s\n----------\n", path);
  filler(buf, ".", NULL, 0);
  filler(buf, "..", NULL, 0);
  ull idx = FIRST_FILENODE_IDX;
  for(;idx<FIRST_DATABLOCK_IDX;idx++){
    if(dirty[idx]==1){
      struct stat tmp;
      tmp.st_mode = S_IFREG | 0644;
      tmp.st_uid = ((filenode*)mem[idx])->st.st_uid;
      tmp.st_gid = ((filenode*)mem[idx])->st.st_gid;
      tmp.st_size = ((filenode*)mem[idx])->st.st_size;
      tmp.st_nlink = ((filenode*)mem[idx])->st.st_nlink;
      tmp.st_atime = ((filenode*)mem[idx])->st.atime;
      tmp.st_mtime = ((filenode*)mem[idx])->st.mtime;
      filler(buf, ((filenode*)mem[idx])->filename, &tmp, 0);
    }
  }
  ////printf("READDIR-DONE.\n");
  return 0;
}

static int my_open(const char *path, struct fuse_file_info *fi){
  return 0;
}

static int my_write(const char *path, const char *buf, size_t size,
off_t offset, struct fuse_file_info *fi){
  ull idx = get_filenode(path);
  // extend file size

  //printf("IDX %lld ; OFFSET %ld ; SIZE %ld\n", idx, offset, size);

  size_t add_size = 0;
  //printf("FILESIZE %ld\n", ((filenode*)mem[idx])->st.st_size);
  //printf("CONTENT_SIZE %ld\n", CONTENT_SIZE);
  int fuck = 1;
  if(((filenode*)mem[idx])->st.st_size % CONTENT_SIZE  == 0){
    fuck = 0;
  }
  int damn = 1;
  if((offset+size)%CONTENT_SIZE == 0){
    damn = 0;
  }
  ull add_block_num = ((offset+size)/CONTENT_SIZE + damn)  - (((filenode*)mem[idx])->st.st_size / CONTENT_SIZE + fuck);

  if(offset+size > ((filenode*)mem[idx])->st.st_size){
    add_size = offset+size-((filenode*)mem[idx])->st.st_size;
    ((filenode*)mem[idx])->st.st_size = offset + size;
  }
  //printf("ADD_SIZE %ld \n", add_size);

  ull offset_block = offset/CONTENT_SIZE;
  off_t offset_in_block = offset%CONTENT_SIZE;

  ull cur_block = ((filenode*)mem[idx])->first_block;

  if(add_size){
    // no enough free blocks
    //printf("ADD_BLOCK_NUM %lld\n", add_block_num);
    if(add_block_num+((super_block*)mem[0])->used_block > ((super_block*)mem[0])->total_block){
      return -ENOSPC;
    }
    ull end_block_idx;
    ull tmp_idx = ((filenode*)mem[idx])->first_block;
    //printf("FIRST_BLOCK %lld\n",tmp_idx);


    // special case
    if(tmp_idx==0){
      // new file without content -- first_block==0
      ((filenode*)mem[idx])->first_block = ((super_block*)mem[0])->data_first_clean;
      ull prev_idx;
      int flag=0;
      while(add_block_num>0){
        // create one new block
        ull tmp = ((super_block*)mem[0])->data_first_clean;
        //printf("TARGET-DATABLOCK %lld\n", tmp);

        tmp_idx = tmp;
        // create one data block
        mem[tmp] = (data_block*)mmap(NULL,BLOCK_SIZE,PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ((data_block*)mem[tmp])->nextnum = tmp;

        if(flag){
          ((data_block*)mem[prev_idx])->nextnum = tmp;
        }
        flag = 1;
        prev_idx = tmp;

        // clear content
        memset(((data_block*)mem[tmp])->content, 0, CONTENT_SIZE);
        //printf("TARGET-CLEAR!\n");

        dirty[tmp] = 1;
        // update used block num
        ((super_block*)mem[0])->used_block ++;

        // update first clean datablock idx
        while(tmp<BLOCK_NUM){
          tmp++;
          if(dirty[tmp]==0){
            ((super_block*)mem[0])->data_first_clean = tmp;
            //printf("FUCK\n");
            break;
          }
        }
        add_block_num--;
      }
    }

    // find end block idx
    while(((data_block*)mem[tmp_idx])->nextnum != tmp_idx){
      tmp_idx = ((data_block*)mem[tmp_idx])->nextnum;
    }
    end_block_idx = tmp_idx;
    //printf("END_BLOCKIDX %lld\n", end_block_idx);

    // create free blocks
    // starts from the end block

    while(add_block_num>0){
      ull tmp = ((super_block*)mem[0])->data_first_clean;
      ////printf("TARGET-DATABLOCK %lld\n", tmp);

      // create one data block
      mem[tmp] = (data_block*)mmap(NULL,BLOCK_SIZE,PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      ((data_block*)mem[end_block_idx])->nextnum = tmp;
      ((data_block*)mem[tmp])->nextnum = tmp;  // tmp is the current end block
      // clear content
      memset(((data_block*)mem[tmp])->content, 0, CONTENT_SIZE);
      end_block_idx = tmp;
      dirty[tmp] = 1;
      // update used block num
      ((super_block*)mem[0])->used_block ++;
      ////printf("FUCK\n");
      // update first clean datablock idx
      while(tmp<BLOCK_NUM){
        tmp++;
        if(dirty[tmp]==0){
          ((super_block*)mem[0])->data_first_clean = tmp;
          break;
        }
      }
      add_block_num--;
    }
    ////printf("ALLOCATE_SUCCESS\n");
  }

  cur_block = ((filenode*)mem[idx])->first_block;
  // find the offset block idx
  while(offset_block>0){
    cur_block = ((data_block*)mem[cur_block])->nextnum;
    offset_block--;
  }
  size_t write_size = 0;
  while(1){
    memcpy(((data_block*)mem[cur_block])->content+offset_in_block, buf+write_size, min(CONTENT_SIZE-offset_in_block, size-write_size));
    write_size += min(CONTENT_SIZE-offset_in_block, size-write_size);
    if(write_size>=size)
      break;
    cur_block = ((data_block*)mem[cur_block])->nextnum;
    offset_in_block = 0;
  }
  return size;

}

static int my_truncate(const char *path, off_t size){
  ull idx = get_filenode(path);
  ////printf("FILENODE-NUM  %lld\n", idx);
  if(idx==0)
    return -ENOENT;
  // new file or too large size
  if(((filenode*)mem[idx])->st.st_size <= size)
    return 0;

  ((filenode*)mem[idx])->st.st_size = size;

  if(size == 0){
    // remove all data blocks
    ull cur_idx = ((filenode*)mem[idx])->first_block;
    ((filenode*)mem[idx])->first_block = 0; // FUCK
    ull next_idx;
    while(1){
      next_idx = ((data_block*)mem[cur_idx])->nextnum;
      munmap(mem[cur_idx], BLOCK_SIZE);
      dirty[cur_idx]=0;
      ((super_block*)mem[0])->used_block --;
      if(cur_idx<((super_block*)mem[0])->data_first_clean)
        ((super_block*)mem[0])->data_first_clean = cur_idx;
      ////printf("%lld ####### %lld\n", cur_idx, next_idx);
      if(cur_idx == next_idx)
        break;
      cur_idx = next_idx;
    }
    return 0;
  }


  ull remain_block_num = size / CONTENT_SIZE;
  off_t offset_in_block = size % CONTENT_SIZE;
  if(offset_in_block==0){
    remain_block_num --;
    offset_in_block = CONTENT_SIZE;
  }
  ull cur_block = ((filenode*)mem[idx])->first_block;
  while(remain_block_num>0){
    cur_block = ((data_block*)mem[cur_block])->nextnum;
    remain_block_num --;
  }
  // clear content within the block
  memset(((data_block*)mem[cur_block])->content + offset_in_block, 0, CONTENT_SIZE-offset_in_block);
  ull new_end_idx = cur_block;
  if(((data_block*)mem[cur_block])->nextnum == cur_block)
    return 0;
  cur_block = ((data_block*)mem[cur_block])->nextnum;
  while(1){
    ull next_block = ((data_block*)mem[cur_block])->nextnum;
    munmap(mem[cur_block], BLOCK_SIZE);
    // clean dirty flag
    dirty[cur_block]=0;
    // update supernode
    ((super_block*)mem[0])->used_block --;
    if(cur_block<((super_block*)mem[0])->data_first_clean)
      ((super_block*)mem[0])->data_first_clean = cur_block;
    if(cur_block == next_block)
      break;
    ////printf("%lld ####### %lld\n", cur_block, next_block);

    cur_block = next_block;
  }
  ((data_block*)mem[new_end_idx])->nextnum = new_end_idx;
  return 0;
}

static int my_read(const char *path, char *buf, size_t size,
off_t offset, struct fuse_file_info *fi){
  ull idx = get_filenode(path);
  int ret = size;
  // too large size -- read as much as we can
  if(offset + size > ((filenode*)mem[idx])->st.st_size)
    ret = ((filenode*)mem[idx])->st.st_size - offset;
  // read -- start from the offset
  ull offset_block = offset/CONTENT_SIZE;
  off_t offset_in_block = offset%CONTENT_SIZE;
  ull cur_block = ((filenode*)mem[idx])->first_block;
  // find the offset block idx
  while(offset_block>0){
    cur_block = ((data_block*)mem[cur_block])->nextnum;
    offset_block--;
  }
  size_t read_size = 0;
  while(1){
    memcpy(buf+read_size, ((data_block*)mem[cur_block])->content+offset_in_block, min(CONTENT_SIZE-offset_in_block, ret-read_size));
    read_size += min(CONTENT_SIZE-offset_in_block, ret-read_size);
    if(read_size>=ret)
      break;
    cur_block = ((data_block*)mem[cur_block])->nextnum;
    offset_in_block = 0;
  }
  return ret;
}

static int my_unlink(const char* path){
  ull idx = get_filenode(path);
  // no that file
  if(idx == 0)
    return -ENOENT;
  off_t st_size = ((filenode*)mem[idx])->st.st_size;
  // remove data blocks
  my_truncate(path,0);
  // remove filenode block
  munmap(mem[idx], BLOCK_SIZE);
  // dirty flag
  dirty[idx] = 0;
  // supernode
  // update first clean filenode block
  if(idx < ((super_block*)mem[0])->file_first_clean)
    ((super_block*)mem[0])->file_first_clean = idx;
  return 0;
}

static struct fuse_operations myop={
  .init = my_init,
  .getattr = my_getattr,
  .readdir = my_readdir,
  .mknod = my_mknod,
  .open = my_open,
  .write = my_write,
  .truncate = my_truncate,
  .read = my_read,
  .unlink = my_unlink
};

int main(int argc, char *argv[]){
  return fuse_main(argc, argv, &myop, NULL);
}
