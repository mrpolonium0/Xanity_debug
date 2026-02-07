#include "ui/xemu-snapshots.h"
#include "qapi/error.h"

const char **g_snapshot_shortcut_index_key_map[] = { NULL };

char *xemu_get_currently_loaded_disc_path(void)
{
    return NULL;
}

int xemu_snapshots_list(QEMUSnapshotInfo **info, XemuSnapshotData **extra_data,
                        Error **err)
{
    (void)err;
    if (info) {
        *info = NULL;
    }
    if (extra_data) {
        *extra_data = NULL;
    }
    return 0;
}

void xemu_snapshots_load(const char *vm_name, Error **err)
{
    (void)vm_name;
    if (err) {
        *err = NULL;
    }
}

void xemu_snapshots_save(const char *vm_name, Error **err)
{
    (void)vm_name;
    if (err) {
        *err = NULL;
    }
}

void xemu_snapshots_delete(const char *vm_name, Error **err)
{
    (void)vm_name;
    if (err) {
        *err = NULL;
    }
}

void xemu_snapshots_save_extra_data(QEMUFile *f)
{
    (void)f;
}

bool xemu_snapshots_offset_extra_data(QEMUFile *f)
{
    (void)f;
    return false;
}

void xemu_snapshots_mark_dirty(void)
{
}

void xemu_snapshots_set_framebuffer_texture(GLuint tex, bool flip)
{
    (void)tex;
    (void)flip;
}

bool xemu_snapshots_load_png_to_texture(GLuint tex, void *buf, size_t size)
{
    (void)tex;
    (void)buf;
    (void)size;
    return false;
}

void *xemu_snapshots_create_framebuffer_thumbnail_png(size_t *size)
{
    if (size) {
        *size = 0;
    }
    return NULL;
}
