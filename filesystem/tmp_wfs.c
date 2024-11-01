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

void print_fs() {
  printf("|||||||||||||||||||||| FS |||||||||||||||||||||||\n");
  printf("D_BITMAP: ");
  for (int i = 0; i < 28; i++) {
    printf("%i ", d_bitmap[i]);
  }
  printf("\n");
  printf("I_BITMAP: ");
  for (int i = 0; i < 4; i++) {
    printf("%i ", i_bitmap[i]);
  }
  printf("\n|||||||||||||||||||||||||||||||||||||||||||||||||\n");
}

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
  printf("get_dentry: want %s from inode %i\n", name, inode->num);
  struct wfs_dentry *dentry = NULL;
  // scan through allocated blocks
  for (int i = 0; i < inode->size / BLOCK_SIZE; i++) {
    dentry = (struct wfs_dentry *) (disk + inode->blocks[i]);
    for (int j = 0; j < N_DENTRY; j++) {
      if (strcmp(name, "print") == 0) {
	printf("%s, ", dentry[j].name);
	continue;
      }
      if (strcmp(name, dentry[j].name) == 0) {
	return &dentry[j];
      }
    }
  }
  printf("\n");
  return NULL;
}

/*
 * returns inode of file/dir at given path
 */
struct wfs_inode *walk_path(char *path) {
  printf("walk_path: %s\n", path);
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

  printf("walk returning inode: %i, size: %li\n", curr->num, curr->size);
  free(path_cpy);
  return curr;
}

/*
 * returns index of free inode block
 */
int find_free_inode() {
  printf("find_free_inode: ");
  for (int i = 0; i < (sb->num_inodes / 8); i++) {
    uint8_t b = i_bitmap[i];
    for (int j = 7; j >= 0; j--) {
      if (((b>>j) & 1) == 0) {
	printf("found index %i\n", i * 8 + (7 - j));
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
  printf("allocate_inode\n");
  int index = find_free_inode();
  if (index == -1) {
    return -ENOSPC;
  }

  i_bitmap[index / 8] = i_bitmap[index / 8] | (1 << ((-index + 7) % 8));
  get_inode(index)->num = index;
  return index;
}

/*
 * returns index of free data block
 */
int find_free_db() {
  printf("find_free_db: ");
  for (int i = 0; i < (sb->num_data_blocks / 8); i++) {
    uint8_t b = d_bitmap[i];
    for (int j = 7; j >= 0; j--) {
      if (((b>>j) & 1) == 0) {
	printf("found index %i\n", i * 8 + (7-j));
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
  printf("allocate_db\n");
  int index = find_free_db();
  if (index == -1) {
    return -ENOSPC;
  }

  d_bitmap[index / 8] = d_bitmap[index / 8] | (1 << ((-index + 7) % 8));
  return index * BLOCK_SIZE + sb->d_blocks_ptr;
}

/*
 * allocates new dentry at given path and returns address of the newly allocated dentry
 */
off_t allocate_dentry(const char *path) {
  printf("allocate_dentry\n");
  char *path_cpy = strdup(path);
  char *file_name;
  for(file_name=path_cpy+strlen(path_cpy); file_name >= path_cpy && *file_name != '/'; file_name--)
    ;
  file_name++;
  path_cpy[strlen(path_cpy) - strlen(file_name) - 1] = '\0';

  // find parent dir
  struct wfs_inode *dir = walk_path(path_cpy);

  // try finding free dentry in dir
  struct wfs_dentry *free_entry = get_dentry(dir, "\0");
  if (free_entry == NULL) {
    if (dir->size / BLOCK_SIZE == 6) {
      return -ENOSPC;
    }
   
    // alloc block                                                                                                                                                                                         
    dir->blocks[dir->size / BLOCK_SIZE] = allocate_db();
    if (dir->blocks[dir->size / BLOCK_SIZE] < 0) {
      return dir->blocks[dir->size / BLOCK_SIZE];
    }
    print_fs();
    // init
    struct wfs_dentry *tmp = (struct wfs_dentry *) (disk + dir->blocks[dir->size / BLOCK_SIZE]);
    for (int i = 0; i < N_DENTRY; i++) {
      strcpy(tmp->name, "\0");
      tmp++;
    }
    
    dir->size += BLOCK_SIZE;
    // retry finding free dentry
    free_entry = get_dentry(dir, "\0");
    print_fs();
  }

  strcpy(free_entry->name, file_name);
  off_t new_inode_off = allocate_inode();
  if (new_inode_off < 0) {
    return new_inode_off;
  }
  struct wfs_inode *new_inode = get_inode(new_inode_off);
  free_entry->num = new_inode->num;

  dir->nlinks++;
  free(path_cpy);
  return (off_t) free_entry;
}

int wfs_mknod(const char* path, mode_t mode, dev_t rdev) {
  printf("wfs_mknod\n");
  char *tmp = strdup(path);
  if (walk_path(tmp) != NULL) {
    return -EEXIST;
  }
  free(tmp);

  off_t new_dentry_off = allocate_dentry(path);
  if (new_dentry_off < 0) {
    return new_dentry_off;
  }
  struct wfs_dentry *new_dentry = (struct wfs_dentry *) new_dentry_off;
  struct wfs_inode *new_inode = get_inode(new_dentry->num);
  
  new_inode->uid = getuid();
  new_inode->gid = getgid();
  new_inode->mode = S_IFREG | mode;
  new_inode->size = 0;
  new_inode->nlinks = 1;
  new_inode->atim = time(NULL);
  new_inode->mtim = time(NULL);
  new_inode->ctim = time(NULL);

  return 0;
}

int wfs_getattr(const char *path, struct stat *stbuf) {
  char *tmp = strdup(path);
  struct wfs_inode *node = walk_path(tmp);
  if (node == NULL) { return -ENOENT; }
  free(tmp);
  stbuf->st_uid = node->uid;
  stbuf->st_gid = node->gid;

  stbuf->st_atim.tv_sec = node->atim;
  stbuf->st_atim.tv_nsec = 0;

  stbuf->st_mtim.tv_sec = node->mtim;
  stbuf->st_mtim.tv_nsec = 0;

  stbuf->st_ctim.tv_sec = node->ctim;
  stbuf->st_ctim.tv_nsec = 0;

  stbuf->st_mode = node->mode;
  stbuf->st_size = node->size;
  stbuf->st_nlink = node->nlinks;

  stbuf->st_ino = node->num;

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
    int read_size = BLOCK_SIZE - block_offset > size ? size : BLOCK_SIZE - block_offset;
    if (num_block > D_BLOCK) {
      // indirect block
      int *indirect = (int *) (disk + inode->blocks[D_BLOCK]);
      num_block = indirect[num_block - D_BLOCK];
    } else {
      num_block = inode->blocks[num_block];
    }
    memcpy(buf + size_read, disk + num_block + block_offset, read_size);
    offset += read_size;
    size -= read_size;
    size_read += read_size;
  }
  
  inode->atim = time(NULL);

  return size_read;
}

/////////////////////////////////// New work /////////////////////////////////////////

int wfs_mkdir(const char* path, mode_t mode) {
  char *tmp = strdup(path);
  if (walk_path(tmp) != NULL) {
    return -EEXIST;
  }
  free(tmp);
  
  off_t newDirectory_off = allocate_dentry(path);

  if (newDirectory_off < 0) {
    return newDirectory_off;
  }

  struct wfs_dentry *newDirectory = (struct wfs_dentry *) newDirectory_off;
  struct wfs_inode *new_inode = get_inode(newDirectory->num);
  
  new_inode->uid = getuid();
  new_inode->gid = getgid();
  new_inode->mode = S_IFDIR | mode;
  new_inode->size = 0;
  new_inode->nlinks = 0;
  new_inode->atim = time(NULL);
  new_inode->mtim = time(NULL);
  new_inode->ctim = time(NULL);

  return 0;
}


int wfs_unlink(const char* path) {
  printf("wfs_unlink\n");
  char *path_cpy = strdup(path);
  char *file_name;
  for(file_name=path_cpy+strlen(path_cpy); file_name >= path_cpy && *file_name != '/'; file_name--)
    ;
  file_name++;
  path_cpy[strlen(path_cpy) - strlen(file_name) - 1] = '\0';

  struct wfs_inode *dir = walk_path(path_cpy);
  struct wfs_dentry *dentry = get_dentry(dir, file_name);
  if (dentry == NULL) {
    return -ENOENT;
  }

  struct wfs_inode *inode = get_inode(dentry->num);
  // free dentry
  strcpy(dentry->name, "\0");
  dentry->num = 0;

  // free data blocks

  for (int i = 0; i < N_BLOCKS; i++) {
    if (inode->blocks[i] != -1) {
      int index = (inode->blocks[i] - sb->d_blocks_ptr) / BLOCK_SIZE / 8;
      int bit = 7 - ((inode->blocks[i] - sb->d_blocks_ptr) / BLOCK_SIZE % 8);
      printf("freeing data block at index %i bit %i\n", index, bit);
      d_bitmap[index] = d_bitmap[index] & ~(1 << bit);
    }
  }

  // free inode
  int index = dentry->num / 8;
  int bit = 7 - (dentry->num % 8);
  i_bitmap[index] = i_bitmap[index] & ~(1 << bit);

  // update directory
  dir->nlinks--;
  free(path_cpy);
  return 0;
}

int wfs_write(const char* path, const char *buf, size_t size, off_t offset, struct fuse_file_info* fi) {
  printf("write: %li\n", size);
  char *tmp = strdup(path);
  struct wfs_inode *inode = walk_path(tmp);
  if (inode == NULL) {
    return -ENOENT;
  }
  free(tmp);

  int size_written = 0;

  while (size > 0) {
    print_fs();
    printf("%li, %li, inode: %li\n", size, offset, inode->size);
    int num_block = offset / BLOCK_SIZE;
    int block_offset = offset % BLOCK_SIZE;
    int write_size = BLOCK_SIZE - block_offset > size ? size : BLOCK_SIZE - block_offset;
    
    // allocate new block if needed 
    if (inode->size / BLOCK_SIZE  <= num_block) {
      if (num_block < N_BLOCKS) {
        inode->blocks[num_block] = allocate_db();
        if (inode->blocks[num_block] < 0) {
          return inode->blocks[num_block];
        }
        inode->size += BLOCK_SIZE;
      } else {
        int *indirect = (int *) (disk + inode->blocks[IND_BLOCK]);
        indirect[num_block - IND_BLOCK] = allocate_db();
        if (indirect[num_block - IND_BLOCK] < 0) {
          return indirect[num_block - IND_BLOCK];
        }
        inode->size += BLOCK_SIZE;
      }
    }

    if (num_block > D_BLOCK) { // indirect block
      int *indirect = (int *) (disk + inode->blocks[IND_BLOCK]);
      num_block = indirect[num_block - IND_BLOCK];
    } else { // direct block  
      num_block = inode->blocks[num_block];
    }

    memcpy(disk + num_block + block_offset, buf + size_written, write_size);
    offset += write_size;
    size -= write_size;
    size_written += write_size;
  }

  inode->atim = time(NULL);
  inode->mtim = time(NULL);

  return size_written;
}
///////////////////////////////////////////////////////////////////////////

static struct fuse_operations ops = {
  .getattr = wfs_getattr, ///from previous
  .mknod   = wfs_mknod, ///from previous
  .mkdir   = wfs_mkdir,
  .unlink  = wfs_unlink,
  //.rmdir   = wfs_rmdir,
  .read    = wfs_read, ///from previous
  .write   = wfs_write,
  //.readdir = wfs_readdir,*/
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
