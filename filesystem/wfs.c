#include "wfs.h"
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>

#define N_DENTRY (BLOCK_SIZE / sizeof(struct wfs_dentry))

char *disk;
struct wfs_sb *sb;
uint8_t *i_bitmap;
uint8_t *d_bitmap;

/*
 * get inode struct from inode num
 */
struct wfs_inode *get_inode(int num) {
  return (struct wfs_inode *)(disk + sb->i_blocks_ptr + (num * BLOCK_SIZE));
}

/*
 * find dentry with given name in given inode
 * (can be also used to find a free dentry by passing "\0" as name)
 */
struct wfs_dentry *get_dentry(struct wfs_inode *inode, char *name) {
  struct wfs_dentry *dentry = NULL;
  // scan through allocated blocks
  for (int i = 0; i < inode->size / BLOCK_SIZE; i++) {
    dentry = (struct wfs_dentry *) (disk + inode->blocks[i]);
    for (int j = 0; j < N_DENTRY; j++) {
      if (strcmp(name, dentry[j].name) == 0) {
	      return &dentry[j];
      }
    }
  }

  return NULL;
}

/*
 * returns inode of file/dir at given path
 */
struct wfs_inode *walk_path(char *path) {
  char *path_cpy = strdup(path);
  struct wfs_inode *curr = get_inode(0);
  char *token = strtok(path_cpy, "/");

  while (token != NULL) {
    struct wfs_dentry *entry = get_dentry(curr, token);
    if (entry == NULL) {
      return NULL;
    }

    curr = get_inode(entry->num);
    token = strtok(NULL, "/");
  }

  free(path_cpy);
  return curr;
}

/*
 * returns index of free inode block
 */
int find_free_inode() {
  for (int i = 0; i < (sb->num_inodes / 8); i++) {
    uint8_t b = i_bitmap[i];
    for (int j = 7; j >= 0; j--) {
      if (((b>>j) & 1) == 0) {
        return i * 8 + (7 - j);
      }
    }
  }

  return -1;
}

/*
 * allocates new inode and returns inode num
 */
off_t allocate_inode() {
  int index = find_free_inode();
  if (index == -1) {
    return -ENOSPC;
  }
  // mark bit as allocated
  i_bitmap[index / 8] = i_bitmap[index / 8] | (1 << (7 - (index % 8)));
  get_inode(index)->num = index;
  return index;
}

/*
 * returns index of free data block
 */
int find_free_db() {
  for (int i = 0; i < (sb->num_data_blocks / 8); i++) {
    uint8_t b = d_bitmap[i];
    for (int j = 7; j >= 0; j--) {
      if (((b>>j) & 1) == 0) {
	      return i * 8 + (7 - j);
      }
    }
  }

  return -1;
}

/*
 * allocates new data block and returns offset to data block
 */
off_t allocate_db() {
  int index = find_free_db();
  if (index == -1) {
    return -ENOSPC;
  }
  // mark bit as allocated
  d_bitmap[index / 8] = d_bitmap[index / 8] | (1 << (7-(index % 8)));
  return index * BLOCK_SIZE + sb->d_blocks_ptr;
}

/*
 * allocates new dentry at given path and returns address of the newly allocated dentry
 */
off_t allocate_dentry(const char *path) {
  char *parent_path = strdup(path);
  char *file_name;
  for(file_name=parent_path+strlen(parent_path); file_name >= parent_path && *file_name != '/'; file_name--)
    ;
  file_name++;
  parent_path[strlen(parent_path) - strlen(file_name) - 1] = '\0';

  // find parent dir
  struct wfs_inode *parent_dir = walk_path(parent_path);
  if (parent_dir == NULL) {
    return -ENOENT;
  }
  // try finding free dentry in parent_dir
  struct wfs_dentry *free_entry = get_dentry(parent_dir, "\0");
  if (free_entry == NULL) {
    if (parent_dir->size / BLOCK_SIZE == IND_BLOCK) {
      return -ENOSPC;
    }
    // alloc block
    parent_dir->blocks[parent_dir->size / BLOCK_SIZE] = allocate_db();
    if (parent_dir->blocks[parent_dir->size / BLOCK_SIZE] < 0) {
      return parent_dir->blocks[parent_dir->size / BLOCK_SIZE];
    }
    // initialize data blocks (dentries)
    struct wfs_dentry *tmp = (struct wfs_dentry *) (disk + parent_dir->blocks[parent_dir->size / BLOCK_SIZE]);
    for (int i = 0; i < N_DENTRY; i++) {
      strcpy(tmp->name, "\0");
      tmp++;
    }
    
    parent_dir->size += BLOCK_SIZE;
    // retry finding free dentry
    free_entry = get_dentry(parent_dir, "\0");
  }

  // fill dentry
  strcpy(free_entry->name, file_name);
  off_t new_inode_off = allocate_inode();
  if (new_inode_off < 0) { // no free inode
    return new_inode_off;
  }
  struct wfs_inode *new_inode = get_inode(new_inode_off);
  free_entry->num = new_inode->num;

  parent_dir->nlinks++;
  free(parent_path);
  return (off_t) free_entry;
}

int wfs_mknod(const char* path, mode_t mode, dev_t rdev) {
  // allocate new dentry in parent dir
  off_t new_dentry_off = allocate_dentry(path);
  if (new_dentry_off < 0) {
    return new_dentry_off;
  }
  struct wfs_dentry *new_dentry = (struct wfs_dentry *) new_dentry_off;
  struct wfs_inode *new_inode = get_inode(new_dentry->num);
  // initialize inode
  new_inode->mode = S_IFREG | mode;
  new_inode->size = 0;
  new_inode->nlinks = 1;
  new_inode->uid = getuid();
  new_inode->gid = getgid();
  new_inode->atim = time(NULL);
  new_inode->mtim = time(NULL);
  new_inode->ctim = time(NULL);
  return 0;
}

int wfs_getattr(const char *path, struct stat *stbuf) {
  char *tmp = strdup(path);
  struct wfs_inode *inode = walk_path(tmp);
  if (inode == NULL) { 
    return -ENOENT; 
  }
  free(tmp);
  stbuf->st_ino = inode->num;
  stbuf->st_mode = inode->mode;
  stbuf->st_gid = inode->gid;
  stbuf->st_uid = inode->uid;
  stbuf->st_nlink = inode->nlinks;
  stbuf->st_size = inode->size;
  stbuf->st_blocks = inode->size / 512;
  stbuf->st_atim.tv_sec = inode->atim;
  stbuf->st_ctim.tv_sec = inode->ctim;
  stbuf->st_mtim.tv_sec = inode->mtim;
  stbuf->st_atim.tv_nsec = 0;
  stbuf->st_ctim.tv_nsec = 0;
  stbuf->st_mtim.tv_nsec = 0;
  return 0;
}

int wfs_read(const char* path, char *buf, size_t size, off_t offset, struct fuse_file_info* fi) {
  char *tmp = strdup(path);
  struct wfs_inode *inode = walk_path(tmp);
  if (inode == NULL) {
    return -ENOENT;
  }
  free(tmp);

  if (offset >= inode->size) {
    return 0;
  }
  
  int size_read = 0;

  while (size > 0) {
    int num_block = offset / BLOCK_SIZE;
    int block_offset = offset % BLOCK_SIZE;
    // set read size to the smaller of the two
    int read_size = BLOCK_SIZE - block_offset > size ? size : BLOCK_SIZE - block_offset;

    off_t addr;
    if (num_block > D_BLOCK) { // indirect block
      off_t *indirect = (off_t *) (disk + inode->blocks[IND_BLOCK]);
      addr = indirect[num_block - IND_BLOCK];
    } else { // direct block  
      addr = inode->blocks[num_block];
    }
    // read data
    memcpy(buf + size_read, disk + addr + block_offset, read_size);
    // update offset and size
    offset += read_size;
    size -= read_size;
    size_read += read_size;
  }
  
  inode->atim = time(NULL);
  return size_read;
}

int wfs_mkdir(const char* path, mode_t mode) {
  // allocate new dentry in parent dir
  off_t new_dentry_off = allocate_dentry(path);
  if (new_dentry_off < 0) {
    return new_dentry_off;
  }

  struct wfs_dentry *new_dentry = (struct wfs_dentry *) new_dentry_off;
  struct wfs_inode *new_inode = get_inode(new_dentry->num);
  // initialize inode
  new_inode->mode = S_IFDIR | mode;
  new_inode->size = 0;
  new_inode->nlinks = 0;
  new_inode->uid = getuid();
  new_inode->gid = getgid();
  new_inode->atim = time(NULL);
  new_inode->mtim = time(NULL);
  new_inode->ctim = time(NULL);
  return 0;
}

int wfs_unlink(const char* path) {
  char *parent_path = strdup(path);
  char *file_name;
  for(file_name=parent_path+strlen(parent_path); file_name >= parent_path && *file_name != '/'; file_name--)
    ;
  file_name++;
  parent_path[strlen(parent_path) - strlen(file_name) - 1] = '\0';
  // find parent dir
  struct wfs_inode *parent = walk_path(parent_path);
  if (parent == NULL) {
    return -ENOENT;
  }
  // find dentry and inode of file to unlink
  struct wfs_dentry *dentry = get_dentry(parent, file_name);
  if (dentry == NULL) {
    return -ENOENT;
  }
  struct wfs_inode *inode = get_inode(dentry->num);
  // free dentry
  strcpy(dentry->name, "\0");
  dentry->num = -1;
  // free data blocks
  for (int i = 0; i < inode->size / BLOCK_SIZE; i++) {
    int index = (inode->blocks[i] - sb->d_blocks_ptr) / BLOCK_SIZE / 8;
    int bit = 7 - ((inode->blocks[i] - sb->d_blocks_ptr) / BLOCK_SIZE % 8);
    // set bit to unallocated
    d_bitmap[index] = d_bitmap[index] & ~(1 << bit);
  }
  // free inode
  int index = inode->num / 8;
  int bit = 7 - (inode->num % 8);
  // set bit to unallocated
  i_bitmap[index] = i_bitmap[index] & ~(1 << bit);

  // update parent directory
  parent->nlinks--;
  free(parent_path);
  return 0;
}

int wfs_write(const char* path, const char *buf, size_t size, off_t offset, struct fuse_file_info* fi) {
  char *tmp = strdup(path);
  struct wfs_inode *inode = walk_path(tmp);
  if (inode == NULL) {
    return -ENOENT;
  }
  free(tmp);

  int size_written = 0;

  while (size > 0) {
    int num_block = offset / BLOCK_SIZE;
    int block_offset = offset % BLOCK_SIZE;
    int write_size = BLOCK_SIZE - block_offset > size ? size : BLOCK_SIZE - block_offset;
    // allocate new block if needed 
    if (inode->size / BLOCK_SIZE  <= num_block) {
      if (num_block < IND_BLOCK) { // case of direct block
        inode->blocks[num_block] = allocate_db();
        if (inode->blocks[num_block] < 0) {
          return inode->blocks[num_block];
        }
        inode->size += BLOCK_SIZE;
      } else { // case of indirect block
	      if (num_block == IND_BLOCK) {
          inode->blocks[IND_BLOCK] = allocate_db();
          if (inode->blocks[IND_BLOCK] < 0) {
            return inode->blocks[IND_BLOCK];
          }
        }
        off_t *indirect = (off_t *) (disk + inode->blocks[IND_BLOCK]);
        indirect[num_block - IND_BLOCK] = allocate_db();
        if (indirect[num_block - IND_BLOCK] < 0) {
          return indirect[num_block - IND_BLOCK];
        }
        inode->size += BLOCK_SIZE;
      }
    }

    if (num_block > D_BLOCK) { // indirect block
      off_t *indirect = (off_t *) (disk + inode->blocks[IND_BLOCK]);
      num_block = indirect[num_block - IND_BLOCK];
    } else { // direct block  
      num_block = inode->blocks[num_block];
    }
    // write data
    memcpy(disk + num_block + block_offset, buf + size_written, write_size);
    // update offset and size
    offset += write_size;
    size -= write_size;
    size_written += write_size;
  }

  // update inode (access and modification time)
  inode->atim = time(NULL);
  inode->mtim = time(NULL);
  return size_written;
}

int wfs_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi) {
  char *tmp = strdup(path);
  struct wfs_inode *dir = walk_path(tmp);
  if (dir == NULL) {
    return -ENOENT;
  }
  free(tmp);
  // scan through dentries
  struct wfs_dentry *dentry = (struct wfs_dentry *) (disk + dir->blocks[offset / N_DENTRY]);
  for (int i = offset; i < dir->nlinks; i++) {
    if (i % N_DENTRY == 0) { // move to next block
      dentry = (struct wfs_dentry *) (disk + dir->blocks[i / N_DENTRY]);
    }
    // if valid entry, add to buffer
    if (strcmp(dentry[i % 16].name, "\0") != 0) {
      filler(buf, dentry[i % 16].name, NULL, 0);
    }
  }

  return 0;
}

int wfs_rmdir(const char* path) {
  char *parent_path = strdup(path);
  char *dir_name;
  for(dir_name=parent_path+strlen(parent_path); dir_name >= parent_path && *dir_name != '/'; dir_name--)
    ;
  dir_name++;
  parent_path[strlen(parent_path) - strlen(dir_name) - 1] = '\0';

  struct wfs_inode *parent = walk_path(parent_path);
  if (parent == NULL) {
    return -ENOENT;
  }
  struct wfs_dentry *dentry = get_dentry(parent, dir_name);
  if (dentry == NULL) {
    return -ENOENT;
  }

  struct wfs_inode *inode = get_inode(dentry->num);
  // free dentry
  strcpy(dentry->name, "\0");
  dentry->num = -1;

  // free data blocks
  for (int i = 0; i < inode->size / BLOCK_SIZE; i++) {
    int index = (inode->blocks[i] - sb->d_blocks_ptr) / BLOCK_SIZE / 8;
    int bit = 7 - ((inode->blocks[i] - sb->d_blocks_ptr) / BLOCK_SIZE % 8);
    // set bit to unallocated
    d_bitmap[index] = d_bitmap[index] & ~(1 << bit);
  }

  // free inode
  int index = inode->num / 8;
  int bit = 7 - (inode->num % 8);
  // set bit to unallocated
  i_bitmap[index] = i_bitmap[index] & ~(1 << bit);

  // update directory
  parent->nlinks--;
  free(parent_path);
  return 0;
}

static struct fuse_operations ops = {
  .getattr = wfs_getattr,
  .mknod   = wfs_mknod,
  .mkdir   = wfs_mkdir,
  .unlink  = wfs_unlink,
  .rmdir   = wfs_rmdir,
  .read    = wfs_read,
  .write   = wfs_write,
  .readdir = wfs_readdir,
};

int main(int argc, char *argv[]) {
    int fd = open(argv[1], O_RDWR);
    struct stat img_info;
    if (fstat(fd, &img_info) == -1) {
        close(fd);
        return 1;
    }

    disk = mmap(NULL, img_info.st_size, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
    sb = (struct wfs_sb *) disk;

    i_bitmap = (uint8_t *) (disk + sb->i_bitmap_ptr);
    d_bitmap = (uint8_t *) (disk + sb->d_bitmap_ptr);

    argv[1] = argv[0];
    close(fd);
    return fuse_main(argc  - 1, argv + 1, &ops, NULL);
}
