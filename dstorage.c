#include "dtypes.h"
#include "duefi.h"

#define EFI_BUFFER_TOO_SMALL 0x8000000000000005ULL
#define STORAGE_HANDLE_CAPACITY 512

typedef struct efi_block_io_media {
    u32 media_id;
    u8 removable_media;
    u8 media_present;
    u8 logical_partition;
    u8 read_only;
    u8 write_caching;
    u32 block_size;
    u32 io_align;
    u64 last_block;
} efi_block_io_media_t;

typedef struct efi_block_io_protocol {
    u64 revision;
    efi_block_io_media_t *media;
} efi_block_io_protocol_t;

typedef struct dstorage_info {
    u64 total_bytes;
    u32 count;
    u64 per_device[16];
    u32 per_device_count;
} dstorage_info_t;

static dstorage_info_t g_storage = {0};

void dstorage_detect(void *boot_services) {
    static void *handles[STORAGE_HANDLE_CAPACITY];
    volatile u64 *dbg = (volatile u64 *)0x301000;

    g_storage.total_bytes = 0;
    g_storage.count = 0;
    g_storage.per_device_count = 0;

    u64 size = 0;
    u64 status = uefi_locate_handle_blockio(boot_services, 0, &size);
    dbg[70] = status;
    dbg[71] = size;

    if (status != 0x8000000000000005ULL || size == 0) {
        return;
    }

    if (size > sizeof(handles)) {
        dbg[72] = EFI_BUFFER_TOO_SMALL;
        dbg[73] = size;
        return;
    }

    status = uefi_locate_handle_blockio(boot_services, handles, &size);
    dbg[72] = status;
    dbg[73] = size;

    if (status != 0) return;
    if (size > sizeof(handles) || size % sizeof(handles[0]) != 0) return;

    u64 handle_count = size / sizeof(void *);
    dbg[74] = handle_count;

    for (u64 i = 0; i < handle_count; i++) {
        void *iface = 0;
        status = uefi_handle_protocol_blockio(boot_services, handles[i], &iface);
        if (i < 16) dbg[75 + i] = status;
        if (status != 0 || !iface) continue;

        efi_block_io_protocol_t *block_io = (efi_block_io_protocol_t *)iface;
        efi_block_io_media_t *media = block_io->media;
        if (!media || !media->media_present || media->block_size == 0) continue;

        if (media->last_block == ~(u64)0) continue;
        u64 block_count = media->last_block + 1;
        if (block_count > (~(u64)0) / media->block_size) continue;

        u64 size_bytes = block_count * media->block_size;

        if (g_storage.total_bytes > (~(u64)0) - size_bytes) {
            g_storage.total_bytes = ~(u64)0;
        } else {
            g_storage.total_bytes += size_bytes;
        }
        g_storage.count++;

        if (g_storage.per_device_count < 16) {
            g_storage.per_device[g_storage.per_device_count++] = size_bytes;
        }
    }
}

u64 dstorage_get_total(void) {
    return g_storage.total_bytes;
}

u32 dstorage_get_count(void) {
    return g_storage.count;
}

u64 dstorage_get_device_size(u32 index) {
    if (index >= g_storage.per_device_count) return 0;
    return g_storage.per_device[index];
}