#include <assert.h>
#include <string.h>
#include "os/os_mempool.h"
#include "ffs/ffs.h"
#include "ffs_priv.h"

static int
ffs_delete_if_trash(struct ffs_base *base)
{
    struct ffs_inode *inode;
    struct ffs_block *block;

    switch (base->fb_type) {
    case FFS_OBJECT_TYPE_INODE:
        inode = (void *)base;
        if (inode->fi_flags & FFS_INODE_F_DELETED) {
            ffs_inode_delete_from_ram(inode);
            return 1;
        } else if (inode->fi_flags & FFS_INODE_F_DUMMY) {
            assert(0); // XXX: TESTING
            ffs_inode_delete_from_ram(inode);
            return 1;
        } else {
            return 0;
        }

    case FFS_OBJECT_TYPE_BLOCK:
        block = (void *)base;
        if (block->fb_flags & FFS_BLOCK_F_DELETED ||
            block->fb_inode == NULL) {

            ffs_block_delete_from_ram(block);
            return 1;
        } else {
            return 0;
        }

    default:
        assert(0);
        return 0;
    }
}

void
ffs_restore_sweep(void)
{
    struct ffs_base_list *list;
    struct ffs_inode *inode;
    struct ffs_base *base;
    struct ffs_base *next;
    int rc;
    int i;

    for (i = 0; i < FFS_HASH_SIZE; i++) {
        list = ffs_hash + i;

        base = SLIST_FIRST(list);
        while (base != NULL) {
            next = SLIST_NEXT(base, fb_hash_next);

            rc = ffs_delete_if_trash(base);
            if (rc == 0) {
                if (base->fb_type == FFS_OBJECT_TYPE_INODE) {
                    inode = (void *)base;
                    if (!(inode->fi_flags & FFS_INODE_F_DIRECTORY)) {
                        inode->fi_data_len = ffs_inode_calc_data_length(inode);
                    }
                }
            }

            base = next;
        }
    }
}

static int
ffs_restore_dummy_inode(struct ffs_inode **out_inode, uint32_t id, int is_dir)
{
    struct ffs_inode *inode;

    inode = ffs_inode_alloc();
    if (inode == NULL) {
        return FFS_ENOMEM;
    }
    inode->fi_base.fb_id = id;
    inode->fi_refcnt = 1;
    inode->fi_base.fb_sector_id = FFS_SECTOR_ID_SCRATCH;
    inode->fi_flags = FFS_INODE_F_DUMMY;
    if (is_dir) {
        inode->fi_flags |= FFS_INODE_F_DIRECTORY;
    }

    *out_inode = inode;

    return 0;
}

static int
ffs_restore_inode_gets_replaced(int *out_should_replace,
                                const struct ffs_inode *old_inode,
                                const struct ffs_disk_inode *disk_inode)
{
    assert(old_inode->fi_base.fb_id == disk_inode->fdi_id);

    if (old_inode->fi_flags & FFS_INODE_F_DUMMY) {
        *out_should_replace = 1;
        return 0;
    }

    if (old_inode->fi_base.fb_seq < disk_inode->fdi_seq) {
        *out_should_replace = 1;
        return 0;
    }

    if (old_inode->fi_base.fb_seq == disk_inode->fdi_seq) {
        assert(0); // XXX TESTING
        return FFS_ECORRUPT;
    }

    *out_should_replace = 0;
    return 0;
}

static int
ffs_restore_inode(const struct ffs_disk_inode *disk_inode, uint16_t sector_id,
                  uint32_t offset)
{
    struct ffs_inode *parent;
    struct ffs_inode *inode;
    int new_inode;
    int do_add;
    int rc;

    new_inode = 0;

    rc = ffs_hash_find_inode(&inode, disk_inode->fdi_id);
    switch (rc) {
    case 0:
        rc = ffs_restore_inode_gets_replaced(&do_add, inode, disk_inode);
        if (rc != 0) {
            goto err;
        }

        if (do_add) {
            if (inode->fi_parent != NULL) {
                ffs_inode_remove_child(inode);
            }
            ffs_inode_from_disk(inode, disk_inode, sector_id, offset);
        }
        break;

    case FFS_ENOENT:
        inode = ffs_inode_alloc();
        if (inode == NULL) {
            rc = FFS_ENOMEM;
            goto err;
        }
        new_inode = 1;
        do_add = 1;

        rc = ffs_inode_from_disk(inode, disk_inode, sector_id, offset);
        if (rc != 0) {
            goto err;
        }
        inode->fi_refcnt = 1;

        ffs_hash_insert(&inode->fi_base);
        break;

    default:
        assert(0); // XXX TESTING
        rc = FFS_ECORRUPT;
        goto err;
    }

    if (do_add) {
        if (disk_inode->fdi_parent_id != FFS_ID_NONE) {
            rc = ffs_hash_find_inode(&parent, disk_inode->fdi_parent_id);
            if (rc == FFS_ENOENT) {
                rc = ffs_restore_dummy_inode(&parent,
                                             disk_inode->fdi_parent_id, 1);
            }
            if (rc != 0) {
                goto err;
            }

            ffs_inode_add_child(parent, inode);
        } 


        if (ffs_disk_inode_is_root(disk_inode)) {
            ffs_root_dir = inode;
        }
    }

    if (inode->fi_base.fb_id >= ffs_next_id) {
        ffs_next_id = inode->fi_base.fb_id + 1;
    }

    return 0;

err:
    if (new_inode) {
        ffs_inode_free(inode);
    }
    return rc;
}

static int
ffs_restore_block_gets_replaced(int *out_should_replace,
                                const struct ffs_block *old_block,
                                const struct ffs_disk_block *disk_block)
{
    assert(old_block->fb_base.fb_id == disk_block->fdb_id);

    if (old_block->fb_base.fb_seq < disk_block->fdb_seq) {
        *out_should_replace = 1;
        return 0;
    }

    if (old_block->fb_base.fb_seq == disk_block->fdb_seq) {
        assert(0); // XXX TESTING
        return FFS_ECORRUPT;
    }

    *out_should_replace = 0;
    return 0;
}

static int
ffs_restore_block(const struct ffs_disk_block *disk_block, uint16_t sector_id,
                  uint32_t offset)
{
    struct ffs_block *block;
    int do_replace;
    int new_block;
    int rc;

    new_block = 0;

    rc = ffs_hash_find_block(&block, disk_block->fdb_id);
    switch (rc) {
    case 0:
        rc = ffs_restore_block_gets_replaced(&do_replace, block, disk_block);
        if (rc != 0) {
            goto err;
        }

        if (do_replace) {
            ffs_block_from_disk(block, disk_block, sector_id, offset);
        }
        break;

    case FFS_ENOENT:
        block = ffs_block_alloc();
        if (block == NULL) {
            rc = FFS_ENOMEM;
            goto err;
        }
        new_block = 1;

        ffs_block_from_disk(block, disk_block, sector_id, offset);
        ffs_hash_insert(&block->fb_base);

        rc = ffs_hash_find_inode(&block->fb_inode, disk_block->fdb_inode_id);
        if (rc == FFS_ENOENT) {
            rc = ffs_restore_dummy_inode(&block->fb_inode,
                                         disk_block->fdb_inode_id, 0);
        }
        if (rc != 0) {
            goto err;
        }
        ffs_inode_insert_block(block->fb_inode, block);
        break;

    default:
        assert(0); // XXX TESTING
        rc = FFS_ECORRUPT;
        goto err;
    }

    if (block->fb_base.fb_id >= ffs_next_id) {
        ffs_next_id = block->fb_base.fb_id + 1;
    }

    return 0;

err:
    if (new_block) {
        ffs_block_free(block);
    }
    return rc;
}

int
ffs_restore_object(const struct ffs_disk_object *disk_object)
{
    int rc;

    switch (disk_object->fdo_type) {
    case FFS_OBJECT_TYPE_INODE:
        rc = ffs_restore_inode(&disk_object->fdo_disk_inode,
                               disk_object->fdo_sector_id,
                               disk_object->fdo_offset);
        break;

    case FFS_OBJECT_TYPE_BLOCK:
        rc = ffs_restore_block(&disk_object->fdo_disk_block,
                               disk_object->fdo_sector_id,
                               disk_object->fdo_offset);
        break;

    default:
        assert(0);
        rc = FFS_EINVAL;
        break;
    }

    return rc;
}

