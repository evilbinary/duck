/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/

#include "vfs.h"

#include "fd.h"

vnode_t *root_node = NULL;
voperator_t default_operator = {.write = vfs_write,
                                .read = vfs_read,
                                .close = vfs_close,
                                .open = vfs_open,
                                .find = vfs_find,
                                .mount = vfs_mount,
                                .readdir = vfs_readdir};

size_t vioctl(vnode_t *node, u32 cmd, void *args) {
  if (node->op->ioctl != NULL) {
    u32 ret = 0;
    // va_list args;
    // va_start(args, cmd);
    ret = node->op->ioctl(node, cmd, args);
    // va_end(args);
    return ret;
  } else {
    // log_warn("node %s ioctl is null\n", node->name);
    return 0;
  }
}

u32 vread(vnode_t *node, u32 offset, u32 size, u8 *buffer) {
  if (node->op->read != NULL) {
    return node->op->read(node, offset, size, buffer);
  } else {
    log_error("node %s read is null\n", node->name);
    return 0;
  }
}

u32 vwrite(vnode_t *node, u32 offset, u32 size, u8 *buffer) {
  if (node->op->write != NULL) {
    return node->op->write(node, offset, size, buffer);
  } else {
    log_error("node %s write is null\n", node->name);
    return 0;
  }
}

u32 vopen(vnode_t *node, u32 mode) {
  if (node->op->open != NULL) {
    return node->op->open(node, mode);
  } else {
    log_error("node %s open is null \n", node->name);
    return -1;
  }
}
u32 vclose(vnode_t *node) {
  if (node->op->close != NULL) {
    return node->op->close(node);
  } else {
    log_error("node %s close is null\n", node->name);
    return -1;
  }
}
u32 vreaddir(vnode_t *node, vdirent_t *dirent, u32 *offset, u32 count) {
  if (node->op->readdir != NULL) {
    if ((node->flags & V_DIRECTORY) == V_DIRECTORY) {
      return node->op->readdir(node, dirent, offset, count);
    } else {
      log_error("node readdir is not dir\n");
    }
  } else {
    log_error("node readdir is null\n");
    return 0;
  }
}
vnode_t *vfinddir(vnode_t *node, char *name) {
  if (node->op->finddir != NULL != NULL) {
    if ((node->flags & V_DIRECTORY) == V_DIRECTORY) {
      return node->op->finddir(node, name);
    } else {
      log_error("node finddir is not dir\n");
    }
  } else {
    log_error("node finddir is null\n");
    return 0;
  }
}

vnode_t *vfind(vnode_t *node, char *name) {
  if (node == NULL) {
    node = root_node;
  }
  if (node->op->find != NULL) {
    if (node->op->find == &vfs_find) {
      return NULL;
    }
    return node->op->find(node, name);
  } else {
    log_error("node find is null\n");
    return 0;
  }
}

void vmount(vnode_t *root, u8 *path, vnode_t *node) {
  if (root->op->mount != NULL) {
    return node->op->mount(root, path, node);
  } else {
    log_error("node mount is null\n");
    return;
  }
}

void vfs_exten_child(vnode_t *node) {
  u32 size = 4;
  if (node->child_number != 0) {
    size = node->child_number * 2;
  }
  vnode_t **child = kmalloc(size * sizeof(vnode_t *), KERNEL_TYPE);
  vnode_t **temp = node->child;
  if (node->child != NULL) {
    kmemmove(child, node->child, node->child_number * sizeof(vnode_t *));
    kfree(temp);
  }
  node->child = child;
  node->child_size = size;
}

void vfs_add_child(vnode_t *parent, vnode_t *child) {
  if ((parent->child_number + 1) > parent->child_size) {
    vfs_exten_child(parent);
  }
  if (parent->child == NULL) {
    log_error("child alloc error\n");
    return;
  }
  child->parent = parent;
  parent->child[parent->child_number++] = child;
}

void vfs_remove_child(vnode_t *parent, vnode_t *child) {
  vnode_t *find_one = NULL;
  for (int i = 0; i < parent->child_number; i++) {
    vnode_t *n = parent->child[i];
    if (n == NULL) continue;
    if (n == child) {
      find_one = n;
      for (int j = i; (j + 1) < parent->child_number; j++) {
        parent->child[j] = parent->child[j + 1];
      }
      break;
    }
  }
}

vnode_t *vfs_find_child(vnode_t *parent, char *name) {
  vnode_t *find_one = NULL;
  for (int i = 0; i < parent->child_number; i++) {
    vnode_t *n = parent->child[i];
    if (n == NULL) continue;
    if (kstrcmp(name, n->name) == 0) {
      find_one = n;
      break;
    }
  }
  return find_one;
}

vnode_t *vfs_find(vnode_t *root, u8 *path) {
  char *token;
  const char *split = "/";
  char buf[MAX_PATH_BUFFER];
  char *start;
  char *s = buf;

  if (root == NULL) {
    root = root_node;
  }
  u32 path_len = kstrlen(path);
  // 处理根路径 "/" 或空路径
  if (path_len == 0 || (path_len == 1 && path[0] == '/')) {
    return root;
  }
  if (path_len == 1 && kstrcmp(root->name, path) == 0) {
    return root;
  }
  if (path_len >= MAX_PATH_BUFFER) {
    s = kmalloc(path_len, KERNEL_TYPE);
    start = s;
  }
  kstrcpy(s, path);
  token = kstrtok(s, split);

  vnode_t *parent = root;
  vnode_t *node = parent;  // 默认返回当前目录
  if (token == NULL) {
    // 空路径，返回 root
    if (path_len >= MAX_PATH_BUFFER) {
      kfree(start);
    }
    return root;
  }
  while (token != NULL) {
    // 处理 .. 返回上一级
    if (kstrcmp(token, "..") == 0) {
      if (parent->parent != NULL) {
        parent = parent->parent;
      } else {
        // 已经是根目录，保持不变
        parent = root;
      }
      token = kstrtok(NULL, split);
      continue;
    }
    // 处理 . 保持当前目录
    if (kstrcmp(token, ".") == 0) {
      token = kstrtok(NULL, split);
      continue;
    }
    
    vnode_t *find_one = vfs_find_child(parent, token);
    if (find_one != NULL) {
      parent = find_one;
      token = kstrtok(NULL, split);
    } else {
      vnode_t *op_node = parent->super != NULL ? parent->super : parent;
      // not found try found in file
      // not found vfs vnode,is super block then find on block
      find_one = vfind(op_node, token);
      if (find_one != NULL) {
        vfs_add_child(parent, find_one);
        parent = find_one;
        token = kstrtok(NULL, split);
      } else {
        node = find_one;
        break;
      }
    }
    node = find_one;
  }
  if (path_len >= MAX_PATH_BUFFER) {
    kfree(start);
  }

  // 如果所有 token 都处理完了，返回最后的 parent
  if (node == NULL && token == NULL) {
    node = parent;
  }

  if (node == NULL) {
    log_error("cannot found file %s\n", path);
  }
  return node;
}

void vfs_mount(vnode_t *root, u8 *path, vnode_t *node) {
  if (root == NULL) {
    root = root_node;
  }
  vnode_t *parent = vfs_find(root, path);
  if (parent != NULL) {
    vfs_add_child(parent, node);
  } else {
    log_error("mount on %s error\n", path);
  }
}

u32 vfs_readdir(vnode_t *node, vdirent_t *dirent, u32 *offset, u32 count) {
  // todo search int vfs
  if (node->super != NULL) {
    return node->super->op->readdir(node, dirent, offset, count);
  }
  return 0;
}

u32 vfs_write(vnode_t *node, u32 offset, u32 size, u8 *buffer) {
  return vwrite(node, offset, size, buffer);
}

u32 vfs_read(vnode_t *node, u32 offset, u32 size, u8 *buffer) {
  return vread(node, offset, size, buffer);
}

u32 vfs_open(vnode_t *node, u32 mode) {
  int ret = 0;
  if (node->super != NULL) {
    node->super->op->open(node, mode);
  } else if (node->op != NULL) {
    // fix not vfs sda mount
    if (node->op->open != &vfs_open) {
      node->op->open(node, mode);
    }
  }
  return ret;
}

vnode_t *vfs_create_node(u8 *name, u32 flags) {
  vnode_t *node = kmalloc(sizeof(vnode_t), KERNEL_TYPE);
  node->name = kmalloc(kstrlen(name), KERNEL_TYPE);
  kstrcpy(node->name, name);
  node->flags = flags;
  node->op = &default_operator;

  node->child = NULL;
  node->child_number = 0;
  node->child_size = 0;
  return node;
}

vnode_t *vfs_open_attr(vnode_t *root, u8 *name, u32 attr) {
  if (name == NULL) {
    return root;
  }
  if (root == NULL) {
    root = root_node;
  }
  vnode_t *file = NULL;
  if ((attr & O_CREAT) == O_CREAT) {
    char parent_path[MAX_PATH_BUFFER];
    int len = kstrlen(name);
    while (len > 0) {
      if (name[--len] == '/') {
        break;
      }
    }
    kstrncpy(parent_path, name, len + 1);
    parent_path[len + 1] = 0;
    vnode_t *node = vfs_find(root, parent_path);
    if (node == NULL) {
      kprintf("open parent %s %s failed \n", parent_path, name);
      return NULL;
    }
    char *last = kstrrstr(name, root->name);
    if (last != NULL) {
      if (last[0] == '/') last++;
    }
    file = vfs_create_node(last, V_FILE);
    file->device = node->device;
    file->data = node->data;
    if (node->super != NULL) {
      if (node->super->op != NULL) {
        file->op = node->super->op;
      }
    } else {
      file->op = node->op;
    }
    file->super = node->super;
    vfs_add_child(node, file);
  } else {
    vnode_t *node = vfs_find(root, name);
    if (node == NULL) {
      log_error("open file %s failed \n", name);
      return NULL;
    }
    file = node;
  }
  u32 ret = vfs_open(file, attr);
  if (ret < 0) {
    log_error("open third %s failed \n", name);
    return NULL;
  }
  return file;
}

int vfs_close(vnode_t *node) {
  if (node == NULL) {
    log_error("close node is nul\n");
    return -1;
  }
  if (node->super != NULL) {
    return vclose(node->super);
  }
  return 0;
}

int vfs_path_append(vnode_t *node, char *name, char *buf) {
  int len = 0;
  vnode_t *p = node;
  if (name != NULL) {
    int start = 1;
    len = kstrlen(name);
    if (name[0] == '/') {
      start = 0;
    }
    kstrncpy(buf + start, name, len);
    buf[0] = '/';
    len++;
  }

  while (p != NULL) {
    int l = kstrlen(p->name);
    if (l == 1 && p->name[0] == '/') {
      break;
    }
    kmemmove(buf + l + 1, buf, len);
    kstrncpy(buf + 1, p->name, l);
    buf[0] = '/';
    p = p->parent;
    len += (1 + l);
  }
  buf[len] = 0;
  return len;
}

// 规范化路径，处理 . 和 ..
int vfs_normalize_path(char *result, const char *path) {
  if (path == NULL || result == NULL) {
    return -1;
  }

  int len = kstrlen(path);
  if (len == 0) {
    result[0] = '/';
    result[1] = '\0';
    return 0;
  }

  // 临时存储路径组件
  char *components[64];
  int comp_count = 0;

  char buf[512];
  kstrcpy(buf, path);

  char *token;
  const char *split = "/";
  token = kstrtok(buf, split);

  while (token != NULL) {
    if (kstrcmp(token, ".") == 0) {
      // 忽略当前目录
    } else if (kstrcmp(token, "..") == 0) {
      // 返回上一级目录
      if (comp_count > 0) {
        comp_count--;
      }
    } else {
      components[comp_count++] = token;
    }
    token = kstrtok(NULL, split);
  }

  // 构建结果路径
  int pos = 0;
  result[pos++] = '/';

  for (int i = 0; i < comp_count; i++) {
    int comp_len = kstrlen(components[i]);
    kstrcpy(result + pos, components[i]);
    pos += comp_len;
    if (i < comp_count - 1) {
      result[pos++] = '/';
    }
  }

  if (pos == 1) {
    result[1] = '\0';
  } else {
    result[pos] = '\0';
  }

  return 0;
}

// 从指定节点开始查找路径（支持相对路径和 ..）
vnode_t *vfs_find_relative(vnode_t *root, vnode_t *pwd, const char *path) {
  if (path == NULL) {
    return NULL;
  }

  // 绝对路径从根目录开始
  if (path[0] == '/') {
    return vfs_find(root, (u8 *)path);
  }

  // 相对路径从当前目录开始
  if (pwd == NULL) {
    pwd = root;
  }

  // 简化处理：直接从当前目录开始遍历
  char buf[512];
  kstrcpy(buf, path);
  
  vnode_t *current = pwd;
  char *token;
  const char *split = "/";
  token = kstrtok(buf, split);
  
  while (token != NULL) {
    log_debug("vfs_find_relative: token='%s' len=%d\n", token, kstrlen(token));
    if (kstrcmp(token, "..") == 0) {
      log_debug("vfs_find_relative: found .., current=%s parent=%x root=%x\n", current->name, current->parent, root);
      // 返回上一级
      if (current->parent != NULL) {
        current = current->parent;
        log_debug("vfs_find_relative: move to parent %s\n", current->name);
      } else {
        // 已经是根目录或 parent 为空，保持不变
        log_debug("vfs_find_relative: at root or no parent, keeping current\n");
      }
    } else if (kstrcmp(token, ".") == 0) {
      // 保持当前目录
      log_debug("vfs_find_relative: found ., keeping current\n");
    } else {
      log_debug("vfs_find_relative: searching for '%s' in %s\n", token, current->name);
      // 查找子节点
      vnode_t *found = vfs_find_child(current, token);
      if (found == NULL) {
        // 尝试从底层文件系统查找
        vnode_t *op_node = current->super != NULL ? current->super : current;
        found = vfind(op_node, token);
        if (found != NULL) {
          vfs_add_child(current, found);
        } else {
          log_error("vfs_find_relative: cannot find %s\n", token);
          return NULL;
        }
      }
      current = found;
    }
    token = kstrtok(NULL, split);
  }
  
  return current;
}

int vfs_init() {
  root_node = vfs_create_node("/", V_DIRECTORY);
  return 1;
}