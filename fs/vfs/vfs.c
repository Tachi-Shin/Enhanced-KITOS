#include <fs/vfs.h>
#include <kernel/error.h>
#include <kernel/sync.h>
#include <lib/string.h>
#include <block/block.h>

#define TYPE_COUNT 4U
static const struct filesystem_type *types[TYPE_COUNT];
static struct mount mounts[VFS_MAX_MOUNTS];
struct file {
    struct inode inode;
    struct mount *mount;
    uint64_t position;
    unsigned int flags;
    unsigned int references;
    bool used;
};
static struct file files[VFS_MAX_FILES];
static struct kernel_gate gate;
static bool same(const char *a, const char *b) { return strcmp(a, b) == 0; }
static bool under(const char *path, const char *root)
{
    size_t n = strlen(root);
    return same(root, "/") || (!strncmp(path, root, n) && (!path[n] || path[n] == '/'));
}
static struct mount *select_mount(const char *path)
{
    struct mount *best = NULL;
    for (unsigned int i = 0; i < VFS_MAX_MOUNTS; ++i)
        if (mounts[i].used && under(path, mounts[i].path) &&
            (!best || strlen(mounts[i].path) > strlen(best->path))) best = &mounts[i];
    return best;
}
static const char *relative(struct mount *mount, const char *path)
{
    if (same(mount->path, "/")) return path;
    path += strlen(mount->path);
    return *path ? path : "/";
}
static int lookup(const char *path, struct inode *node, struct mount **mount)
{
    *mount = select_mount(path);
    if (!*mount) return -K_ENODEV;
    return (*mount)->sb.ops->lookup(&(*mount)->sb, relative(*mount, path), node);
}
/* Validate each prefix before processing '..'. /file/.. and /absent/.. fail. */
static int resolve(const char *path, char *out, bool missing_leaf)
{
    if (!path || *path != '/') return -K_EINVAL;
    size_t used = 1;
    out[0] = '/'; out[1] = 0;
    while (*path) {
        while (*path == '/') ++path;
        if (!*path) break;
        const char *start = path;
        while (*path && *path != '/') ++path;
        size_t length = (size_t)(path - start);
        if (length >= VFS_NAME_MAX) return -K_ENAMETOOLONG;
        if (length == 1 && *start == '.') continue;
        if (length == 2 && start[0] == '.' && start[1] == '.') {
            while (used > 1 && out[--used] != '/') {}
            if (used == 0) used = 1;
            out[used] = 0;
            continue;
        }
        if (used + length + 2 > VFS_PATH_MAX) return -K_ENAMETOOLONG;
        if (used > 1) out[used++] = '/';
        memcpy(out + used, start, length); used += length; out[used] = 0;
        const char *tail = path;
        while (*tail == '/') ++tail;
        /* A trailing slash demands a directory, even for create. */
        bool require_dir = *tail || *path == '/';
        if (require_dir || !missing_leaf) {
            struct inode node; struct mount *mount;
            int rc = lookup(out, &node, &mount);
            if (rc) return rc;
            if (require_dir && node.kind != VFS_DIRECTORY) return -K_ENOTDIR;
        }
    }
    return 0;
}
static int refresh(struct file *file)
{ return file->inode.sb->ops->lookup(file->inode.sb, file->inode.path, &file->inode); }
static bool valid_file(struct file *file)
{
    for (unsigned int i = 0; i < VFS_MAX_FILES; ++i)
        if (file == &files[i]) return file->used;
    return false;
}
int vfs_register(const struct filesystem_type *type)
{
    if (!type || !type->name || !type->mount) return -K_EINVAL;
    if (!kernel_gate_try(&gate)) return -K_EBUSY;
    int rc = -K_ENOSPC;
    for (unsigned int i = 0; i < TYPE_COUNT; ++i) {
        if (types[i] && same(types[i]->name, type->name)) { rc = -K_EEXIST; break; }
        if (!types[i]) { types[i] = type; rc = 0; break; }
    }
    kernel_gate_release(&gate); return rc;
}
static int mount_volume(const char *type,const char *path,struct block_device *device,const void *options,bool virtual)
{
    if (!type || !path || !device) return -K_EINVAL;
    if (!kernel_gate_try(&gate)) return -K_EBUSY;
    char canonical[VFS_PATH_MAX];
    int rc = resolve(path, canonical, virtual);
    struct mount *slot = NULL;
    const struct filesystem_type *fs = NULL;
    if (rc) goto out;
    if (!same(canonical, "/")) {
        struct inode node; struct mount *parent;
        rc = lookup(canonical, &node, &parent);
        if(rc==-K_ENOENT&&virtual){rc=0;goto parent_ok;}
        if (rc) goto out;
        if (node.kind != VFS_DIRECTORY) { rc = -K_ENOTDIR; goto out; }
        if (!virtual&&parent->open_files) { rc = -K_EBUSY; goto out; }
    }
parent_ok:
    for (unsigned int i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (!mounts[i].used) slot = &mounts[i];
        else if (same(mounts[i].path, canonical) || mounts[i].sb.device == device) {
            rc = -K_EBUSY; goto out;
        }
    }
    for (unsigned int i = 0; i < TYPE_COUNT; ++i)
        if (types[i] && same(types[i]->name, type)) fs = types[i];
    if (!slot || !fs) { rc = !slot ? -K_ENOSPC : -K_ENODEV; goto out; }
    slot->sb = (struct super_block){.type = fs, .device = device};
    rc=block_hold(device);if(rc)goto out;
    rc = fs->mount(&slot->sb, device, options);
    if (!rc) { strcpy(slot->path, canonical); slot->used = true; slot->open_files = 0; }
    else block_put(device);
out:
    kernel_gate_release(&gate); return rc;
}
int vfs_mount(const char *t,const char *p,struct block_device *d,const void *o)
{return mount_volume(t,p,d,o,false);}
int vfs_mount_virtual(const char *t,const char *p,struct block_device *d)
{return mount_volume(t,p,d,NULL,true);}
int vfs_unmount(const char *path)
{
    if(!path)return -K_EINVAL;
    if (!kernel_gate_try(&gate)) return -K_EBUSY;
    char canonical[VFS_PATH_MAX];
    bool exact=false;
    for(unsigned int i=0;i<VFS_MAX_MOUNTS;++i)if(mounts[i].used&&!strcmp(mounts[i].path,path))exact=true;
    int rc=0;if(exact)strcpy(canonical,path);else rc=resolve(path,canonical,false);
    if (rc) goto out;
    struct mount *mount = select_mount(canonical);
    if (!mount || !same(mount->path, canonical)) { rc = -K_EINVAL; goto out; }
    if (mount->open_files) { rc = -K_EBUSY; goto out; }
    for (unsigned int i = 0; i < VFS_MAX_MOUNTS; ++i)
        if (mounts[i].used && &mounts[i] != mount && under(mounts[i].path, canonical)) {
            rc = -K_EBUSY; goto out;
        }
    rc=mount->sb.device->offline?0:block_sync(mount->sb.device);if(rc)goto out;
    mount->sb.ops->release(&mount->sb); mount->used = false;block_put(mount->sb.device);
out:
    kernel_gate_release(&gate); return rc;
}
int vfs_mount_path(struct block_device *device,char *out)
{
    if(!out)return -K_EINVAL;
    if(!kernel_gate_try(&gate))return -K_EBUSY;
    int rc=-K_ENOENT;
    for(unsigned int i=0;i<VFS_MAX_MOUNTS;++i)if(mounts[i].used&&mounts[i].sb.device==device){strcpy(out,mounts[i].path);rc=0;break;}
    kernel_gate_release(&gate);return rc;
}
int vfs_unmount_device(struct block_device *device)
{
    if(!kernel_gate_try(&gate))return -K_EBUSY;
    int rc=-K_ENOENT;
    for(unsigned int i=0;i<VFS_MAX_MOUNTS;++i){struct mount *m=&mounts[i];
        if(!m->used||m->sb.device!=device)continue;
        rc=-K_EBUSY;if(m->open_files)break;
        bool child=false;
        for(unsigned int j=0;j<VFS_MAX_MOUNTS;++j)if(i!=j&&mounts[j].used&&under(mounts[j].path,m->path))child=true;
        if(child)break;
        rc=device->offline?0:block_sync(device);if(rc)break;
        m->sb.ops->release(&m->sb);m->used=false;block_put(device);rc=0;break;
    }
    kernel_gate_release(&gate);return rc;
}
int vfs_resolve_directory(const char *path,char *out)
{
    if(!out)return -K_EINVAL;
    if(!kernel_gate_try(&gate))return -K_EBUSY;
    char canonical[VFS_PATH_MAX];struct inode node;struct mount *mount;
    int rc=resolve(path,canonical,false);
    if(!rc)rc=lookup(canonical,&node,&mount);
    if(!rc&&node.kind!=VFS_DIRECTORY)rc=-K_ENOTDIR;
    if(!rc)strcpy(out,canonical);
    kernel_gate_release(&gate);return rc;
}
int vfs_stat(const char *path, struct vfs_stat *out)
{
    if (!out) return -K_EINVAL;
    if (!kernel_gate_try(&gate)) return -K_EBUSY;
    char canonical[VFS_PATH_MAX]; struct inode node; struct mount *mount;
    int rc = resolve(path, canonical, false);
    if (!rc) rc = lookup(canonical, &node, &mount);
    if (!rc) *out = (struct vfs_stat){node.number, node.size, node.kind, node.attributes, (unsigned int)(mount-mounts)+1};
    kernel_gate_release(&gate); return rc;
}
int vfs_open(const char *path, unsigned int flags, struct file **out)
{
    if (!out) return -K_EINVAL;
    *out = NULL;
    if (!(flags & (VFS_READ | VFS_WRITE)) || flags & ~63U ||
        ((flags & (VFS_TRUNC | VFS_APPEND)) && !(flags & VFS_WRITE)) ||
        ((flags & VFS_EXCL) && !(flags & VFS_CREATE))) return -K_EINVAL;
    if (!kernel_gate_try(&gate)) return -K_EBUSY;
    char canonical[VFS_PATH_MAX]; struct inode node; struct mount *mount;
    int rc = resolve(path, canonical, (flags & VFS_CREATE) != 0);
    struct file *slot = NULL;
    if (rc) goto done;
    for (unsigned int i = 0; i < VFS_MAX_FILES; ++i) if (!files[i].used) { slot = &files[i]; break; }
    if (!slot) { rc = -K_ENFILE; goto done; }
    rc = lookup(canonical, &node, &mount);
    if (rc == -K_ENOENT && (flags & VFS_CREATE)) {
        rc = mount->sb.ops->create(&mount->sb, relative(mount, canonical), false);
        if (!rc) rc = lookup(canonical, &node, &mount);
    } else if (!rc && (flags & VFS_EXCL)) rc = -K_EEXIST;
    if (rc) goto done;
    if (node.kind != VFS_REGULAR) { rc = -K_EISDIR; goto done; }
    if ((flags & VFS_WRITE) && (node.attributes & 1U)) { rc = -K_EROFS; goto done; }
    if ((flags & VFS_TRUNC) && (rc = node.sb->file_ops->truncate(&node, 0))) goto done;
    slot->inode = node; slot->mount = mount; slot->position = 0; slot->flags = flags;
    slot->used = true; slot->references=1; ++mount->open_files; *out = slot;
done:
    kernel_gate_release(&gate); return rc;
}
int vfs_retain(struct file *file)
{
    if(!kernel_gate_try(&gate))return -K_EBUSY;
    int rc=-K_EBADF;
    if(valid_file(file)&&file->references<0x7fffffffU){++file->references;rc=0;}
    kernel_gate_release(&gate);return rc;
}
int vfs_close(struct file *file)
{
    if (!kernel_gate_try(&gate)) return -K_EBUSY;
    int rc = -K_EBADF;
    if (valid_file(file)) {if(!--file->references){--file->mount->open_files;file->used=false;}rc=0;}
    kernel_gate_release(&gate); return rc;
}
int vfs_fstat(struct file *file, struct vfs_stat *out)
{
    if (!out) return -K_EINVAL;
    if (!kernel_gate_try(&gate)) return -K_EBUSY;
    int rc = -K_EBADF;
    if (valid_file(file) && !(rc = refresh(file)))
        *out = (struct vfs_stat){file->inode.number, file->inode.size, file->inode.kind, file->inode.attributes, (unsigned int)(file->mount-mounts)+1};
    kernel_gate_release(&gate);
    return rc;
}
static int io(struct file *file, void *buffer, uint32_t length, uint32_t *done, bool write)
{
    if (!done || (!buffer && length)) return -K_EINVAL;
    *done = 0;
    if (!kernel_gate_try(&gate)) return -K_EBUSY;
    int rc = -K_EBADF;
    if (!valid_file(file) || !(file->flags & (write ? VFS_WRITE : VFS_READ))) goto out;
    if ((rc = refresh(file))) goto out;
    if (write && (file->flags & VFS_APPEND)) file->position = file->inode.size;
    const struct file_operations *ops = file->inode.sb->file_ops;
    rc = write ? ops->write(&file->inode, file->position, buffer, length, done)
               : ops->read(&file->inode, file->position, buffer, length, done);
    file->position += *done;
out:
    kernel_gate_release(&gate); return rc;
}
int vfs_read(struct file *f, void *b, uint32_t n, uint32_t *d) { return io(f, b, n, d, false); }
int vfs_write(struct file *f, const void *b, uint32_t n, uint32_t *d) { return io(f, (void *)b, n, d, true); }
int vfs_seek(struct file *file, int64_t offset, unsigned int whence, uint64_t *position)
{
    if (whence > 2 || !position) return -K_EINVAL;
    if (!kernel_gate_try(&gate)) return -K_EBUSY;
    int rc = -K_EBADF;
    if (!valid_file(file)) goto out;
    if ((rc = refresh(file))) goto out;
    uint64_t base = whence == 0 ? 0 : whence == 1 ? file->position : file->inode.size;
    uint64_t magnitude = offset < 0 ? 0ULL - (uint64_t)offset : (uint64_t)offset;
    if ((offset < 0 && magnitude > base) || (offset >= 0 && magnitude > 0x7FFFFFFFFFFFFFFFULL - base)) {
        rc = -K_EOVERFLOW; goto out;
    }
    *position = file->position = offset < 0 ? base - magnitude : base + magnitude;
out:
    kernel_gate_release(&gate); return rc;
}
int vfs_ftruncate(struct file *file, uint64_t size)
{
    if (!kernel_gate_try(&gate)) return -K_EBUSY;
    int rc = -K_EBADF;
    if (valid_file(file) && (file->flags & VFS_WRITE)) rc = file->inode.sb->file_ops->truncate(&file->inode, size);
    kernel_gate_release(&gate); return rc;
}
static int namespace_op(const char *path, unsigned int operation)
{
    if (!kernel_gate_try(&gate)) return -K_EBUSY;
    char canonical[VFS_PATH_MAX];
    int rc = resolve(path, canonical, operation == 0);
    if (rc) goto out;
    struct mount *mount = select_mount(canonical);
    if (!mount) { rc = -K_ENODEV; goto out; }
    if (mount->open_files) { rc = -K_EBUSY; goto out; }
    for (unsigned int i = 0; i < VFS_MAX_MOUNTS; ++i)
        if (mounts[i].used && under(mounts[i].path, canonical)) { rc = -K_EBUSY; goto out; }
    rc = operation == 0 ? mount->sb.ops->create(&mount->sb, relative(mount, canonical), true)
                        : mount->sb.ops->remove(&mount->sb, relative(mount, canonical), operation == 2);
out:
    kernel_gate_release(&gate); return rc;
}
int vfs_mkdir(const char *p) { return namespace_op(p, 0); }
int vfs_unlink(const char *p) { return namespace_op(p, 1); }
int vfs_rmdir(const char *p) { return namespace_op(p, 2); }
int vfs_rename(const char *old_path, const char *new_path)
{
    if (!kernel_gate_try(&gate)) return -K_EBUSY;
    char old[VFS_PATH_MAX], new[VFS_PATH_MAX];
    int rc = resolve(old_path, old, false);
    if (!rc) rc = resolve(new_path, new, true);
    if (rc) goto out;
    struct mount *a = select_mount(old), *b = select_mount(new);
    if (!a || !b) { rc = -K_ENODEV; goto out; }
    if (a != b) { rc = -K_EXDEV; goto out; }
    if (a->open_files) { rc = -K_EBUSY; goto out; }
    for (unsigned int i = 0; i < VFS_MAX_MOUNTS; ++i)
        if (mounts[i].used && (under(mounts[i].path, old) || under(mounts[i].path, new))) {
            rc = -K_EBUSY; goto out;
        }
    rc = a->sb.ops->rename(&a->sb, relative(a, old), relative(a, new));
out:
    kernel_gate_release(&gate); return rc;
}
struct mounted_listing {
    vfs_dir_callback callback;void *user;
    const char *names[VFS_MAX_MOUNTS];bool seen[VFS_MAX_MOUNTS];
};
static int mount_listing(void *context,const struct vfs_dirent *entry)
{
    struct mounted_listing *list=context;
    for(unsigned int i=0;i<VFS_MAX_MOUNTS;++i)if(list->names[i]&&!strcmp(list->names[i],entry->name))list->seen[i]=true;
    return list->callback(list->user,entry);
}
int vfs_list(const char *path, vfs_dir_callback callback, void *user)
{
    if (!callback) return -K_EINVAL;
    if (!kernel_gate_try(&gate)) return -K_EBUSY;
    char canonical[VFS_PATH_MAX]; struct inode node; struct mount *mount;
    int rc = resolve(path, canonical, false);
    if (!rc) rc = lookup(canonical, &node, &mount);
    if (!rc && node.kind != VFS_DIRECTORY) rc = -K_ENOTDIR;
    struct mounted_listing list={.callback=callback,.user=user};
    if(!rc){
        size_t n=strlen(canonical);
        for(unsigned int i=0;i<VFS_MAX_MOUNTS;++i)if(mounts[i].used&&under(mounts[i].path,canonical)&&strcmp(mounts[i].path,canonical)){
            const char *name=mounts[i].path+n;if(*name=='/')++name;
            if(!strchr(name,'/'))list.names[i]=name;
        }
        rc=mount->sb.ops->list(&mount->sb,relative(mount,canonical),mount_listing,&list);
        for(unsigned int i=0;!rc&&i<VFS_MAX_MOUNTS;++i)if(list.names[i]&&!list.seen[i]){
            struct vfs_dirent entry={.kind=VFS_DIRECTORY};strcpy(entry.name,list.names[i]);rc=callback(user,&entry);
        }
    }
    kernel_gate_release(&gate); return rc;
}
