/*
 * @Author: chenbo shadowboc0302@gmail.com
 * @Date: 2024-10-27 15:07:18
 * @LastEditors: chenbo shadowboc0302@gmail.com
 * @LastEditTime: 2024-10-27 22:00:50
 * @FilePath: /chapter/share_mem_use/ipc_utils.c
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/file.h>
#include <sys/stat.h>

#define SHM_PRIX_NAME "/s3d_ipc_shared_memory"
#define LOCK_FILE "/tmp/s3d_ipc_lockfile.lock"
#define LOCK_TIMEOUT 1 // 超时时间（秒）

#define MAX_CHANNEL_NUM 8
#define MAX_BLOCK_NUM 32
#define MAX_SHM_NAME 256

typedef enum
{
    PUT_STATE_NOT_DONE = 0,
    PUT_STATE_DONE,
} put_state_e;

typedef enum
{
    FREE_STATE_NOT_DONE = 0,
    FREE_STATE_DONE,
} free_state_e;

typedef struct ipc_mem_cfg
{
    uint32_t buffer_frame_num;
    uint32_t buffer_frame_size;
    char *shm_name_prefix;
} ipc_mem_cfg_t;

typedef struct buffer_block_header
{
    uint32_t size;
    uint32_t put_state;  // put_state_e
    uint32_t free_state; // free_state_e
} buffer_block_header_t;

typedef struct
{
    // int shm_fd;
    // uint8_t* block_base_addr; // block buffers is continues, need base
    uint32_t block_num;
    uint32_t block_size;
    uint32_t total_size;
    char shm_name[MAX_SHM_NAME];
} shm_block_buffer_t;

typedef enum
{
    IPC_CHANNEL_DISABLE = 0,
    IPC_CHANNEL_ENABLE
} ipc_channel_state_e;

typedef struct ctrl_block
{
    ipc_channel_state_e channel_state;
    struct
    {
        uint32_t alloc;
        uint32_t put;
    };
    struct
    {
        uint32_t get;
        uint32_t free;
    };
    struct
    {
        uint32_t ring_len;
        uint32_t ring_mask;
    };
    uint64_t block_bufs_offset[MAX_BLOCK_NUM]; // 基于blocks buffer的基地址的偏移
} ctrl_block_t;

typedef struct ipc_ctrl_info
{
    ctrl_block_t rw_ctrl;
    shm_block_buffer_t shm_block_buf;
} ipc_ctrl_info_t;

typedef struct ipc_ctrl_mgr
{
    // bool local_ch_en;
    int shm_fd;
    ipc_ctrl_info_t *ipc_ctrl_info;
    uint8_t *block_base_addr;
} ipc_ctrl_mgr_t;

static ipc_ctrl_mgr_t gIpcCtrlMgr[MAX_CHANNEL_NUM];

// ipc_ctrl_info_t* gIpcCtrlInfo[MAX_CHANNEL_NUM];

ipc_ctrl_mgr_t *get_ipc_ctrl_mgr(uint8_t channel_id)
{
    return &gIpcCtrlMgr[channel_id % (MAX_CHANNEL_NUM - 1)];
}

void enable_ipc_channel(uint8_t channel_id, bool enable)
{
    gIpcCtrlMgr[channel_id % (MAX_CHANNEL_NUM - 1)].ipc_ctrl_info->rw_ctrl.channel_state = enable ? IPC_CHANNEL_ENABLE : IPC_CHANNEL_DISABLE;
}

bool is_ipc_channel_enable(uint8_t channel_id)
{
    ipc_ctrl_info_t *ipc_ctrl_info = gIpcCtrlMgr[channel_id % (MAX_CHANNEL_NUM - 1)].ipc_ctrl_info;
    if (ipc_ctrl_info && (ipc_ctrl_info->rw_ctrl.channel_state == IPC_CHANNEL_ENABLE))
    {
        return true;
    }
    else
    {
        return false;
    }
}
// ipc_ctrl_info_t* get_ipc_ctrl_info(uint8_t channel_id) {
//     return get_ipc_ctrl_mgr(channel_id)->ipc_ctrl_info;
// }

// void set_ipc_ctrl_info(uint8_t channel_id, ipc_ctrl_info_t* ipc_ctrl_info) {
//     gIpcCtrlMgr[channel_id % (MAX_CHANNEL_NUM - 1)].ipc_ctrl_info = ipc_ctrl_info;
// }

int acquire_lock(int fd)
{
    struct flock lock;
    lock.l_type = F_WRLCK; // 写锁
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0; // 锁定整个文件

    time_t start = time(NULL);

    while (1)
    {
        // 尝试获取锁
        if (fcntl(fd, F_SETLK, &lock) == 0)
        {
            return 0; // 成功获取锁
        }

        // 检查是否超时
        if (time(NULL) - start >= LOCK_TIMEOUT)
        {
            return -1; // 超时未获取锁
        }

        usleep(100000); // 100ms
    }
}

int release_lock(int fd)
{
    struct flock lock;
    lock.l_type = F_UNLCK; // 释放锁
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0; // 锁定整个文件

    if (fcntl(fd, F_SETLK, &lock) == -1)
    {
        return -1;
    }

    return 0;
}

// -----------------------------------------------------------------------------------------------server
void ipc_ctrl_info_init(char *shm_name, uint8_t *shm_mem, uint32_t shm_size, uint32_t block_num, uint32_t block_size)
{
    ipc_ctrl_info_t *ipc_ctrl = (ipc_ctrl_info_t *)shm_mem;

    // shm_block_buf
    // memset(&ipc_ctrl->shm_block_buf, 0, sizeof(shm_block_buffer_t));
    // ipc_ctrl->shm_block_buf.block_base_addr = (uint8_t* )(ipc_ctrl + 1);
    ipc_ctrl->shm_block_buf.block_num = block_num;
    ipc_ctrl->shm_block_buf.block_size = block_size;
    ipc_ctrl->shm_block_buf.total_size = shm_size;
    memcpy_s(ipc_ctrl->shm_block_buf.shm_name, sizeof(ipc_ctrl->shm_block_buf.shm_name), shm_name, strlen(shm_name));

    // memset(&ipc_ctrl->rw_ctrl, 0, sizeof(ctrl_block_t));
    ipc_ctrl->rw_ctrl.ring_len = block_num;
    ipc_ctrl->rw_ctrl.ring_mask = block_num - 1;

    for (uint32_t ix = 0; ix < block_num; ix++)
    {
        ipc_ctrl->rw_ctrl.block_bufs_offset[ix] = ix * block_size;
        // memset(ipc_ctrl->rw_ctrl.block_bufs[ix], 0, block_size);
    }

    if (block_num * block_size + sizeof(ipc_ctrl_info_t) != shm_size)
    {
        perror("alloc_size not match!");
    }
}

int ipc_shm_create(char *shm_name, uint32_t alloc_size, uint8_t **shm_mem, int *fd)
{
    // shm alloc
    int shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1)
    {
        perror("Failed to open shm memory(%s), alloc_size=%d", shm_name, alloc_size);
        return -1;
    }
    *fd = shm_fd;

    if (ftruncate(shm_fd, alloc_size) == -1)
    {
        perror("Failed to resize(%d) the shm mem(%s)", alloc_size, shm_name);
        shm_unlink(shm_name);
        close(shm_fd);
        return -1;
    }

    *shm_mem = (int *)(mmap(NULL, alloc_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0));
    if (MAP_FAILED == *shm_mem)
    {
        perror("Failed to mmap the shm mem(%s), alloc_size=%d", shm_name, alloc_size);
        // close(shm_fd);
        shm_unlink(shm_name);
        close(shm_fd);
        return -1;
    }

    memset(*shm_mem, 0, alloc_size);

    return 0;
}

int ipc_shm_init(struct ipc_mem_cfg *cfg, uint8_t channel_id)
{
    uint32_t block_size = cfg->buffer_frame_size + sizeof(buffer_block_header_t);
    uint32_t alloc_size = cfg->buffer_frame_num * block_size + sizeof(ipc_ctrl_info_t);
    char shm_name[MAX_SHM_NAME] = {0};
    int ch_num = snprintf(shm_name, MAX_SHM_NAME - 1, "%s_%d", cfg->shm_name_prefix ? cfg->shm_name_prefix : SHM_PRIX_NAME, channel_id);
    if (ch_num < 0)
    {
        perror("%s, snprintf fail", __func__);
        return -1;
    }

    // 创建 shm mem
    uint8_t *shm_mem = NULL;
    int fd = 0;
    int call_ret = ipc_shm_create(shm_name, alloc_size, &shm_mem, &fd);
    if (call_ret)
    {
        perror("%s, ipc_shm_create fail", __func__);
        return -1;
    }

    /** shm 内存结构:
        |[ipc_ctrl_info_t]|[block0][block1]...[blockn]|
    **/
    ipc_ctrl_info_init(shm_name, shm_mem, alloc_size, cfg->buffer_frame_num, block_size);

    // set mgr
    ipc_ctrl_mgr_t *ipc_ctrl_mgr = get_ipc_ctrl_mgr(channel_id);
    ipc_ctrl_mgr->shm_fd = fd;
    ipc_ctrl_mgr->ipc_ctrl_info = (ipc_ctrl_info_t *)shm_mem;
    ipc_ctrl_mgr->block_base_addr = shm_mem + sizeof(ipc_ctrl_info_t);
    // ipc_ctrl_mgr->local_ch_en = true;
    enable_ipc_channel(channel_id, true);
    return 0;
}
void reset_ipc_ctrl_mgr(uint8_t channel_id)
{
    ipc_ctrl_mgr_t *ipc_ctrl_mgr = get_ipc_ctrl_mgr(channel_id);
    // ipc_ctrl_mgr->local_ch_en = false;
    ipc_ctrl_mgr->shm_fd = -1;
    ipc_ctrl_mgr->ipc_ctrl_info = NULL;
    ipc_ctrl_mgr->block_base_addr = NULL;
}

void ipc_shm_uninit(uint8_t channel_id)
{
    // ipc_ctrl_info_t* ipc_ctrl_info = get_ipc_ctrl_info(channel_id);
    ipc_ctrl_mgr_t *ipc_ctrl_mgr = get_ipc_ctrl_mgr(channel_id);
    ipc_ctrl_info_t *ipc_ctrl_info = ipc_ctrl_mgr->ipc_ctrl_info;
    munmap(ipc_ctrl_info, ipc_ctrl_info->shm_block_buf.total_size);
    shm_unlink(ipc_ctrl_info->shm_block_buf.shm_name);
    close(ipc_ctrl_mgr->shm_fd);
    enable_ipc_channel(channel_id, false);
    reset_ipc_ctrl_mgr(channel_id);
}

static inline bool is_power_of2(int n)
{
    return (n > 0) && ((n & (n - 1)) == 0);
}

static bool ipc_params_check(struct ipc_mem_cfg *cfg, uint8_t channel_id)
{
    if (!cfg)
    {
        return false;
    }
    if (channel_id >= MAX_CHANNEL_NUM || cfg->buffer_frame_num <= 0 || cfg->buffer_frame_size <= 0)
    {
        return false;
    }
    if (!is_power_of2(cfg->buffer_frame_num))
    {
        return false;
    }

    return true;
}

int ipc_server_init(struct ipc_mem_cfg *cfg, uint8_t channel_id)
{
    if (!ipc_params_check(cfg, channel_id))
    {
        perror("%s, input parameter is invalid", __func__);
        return -1;
    }

    // 打开文件锁
    int lock_fd = open(LOCK_FILE, O_CREAT | O_RDWR, 0666);
    if (lock_fd == -1)
    {
        perror("Failed to open lock file");
        return -1;
    }

    // 获取文件锁，超时1s
    if (acquire_lock(lock_fd, LOCK_EX) == -1)
    {
        perror("Failed to acquire lock");
        return -2;
    }
    // 创建 shm mem
    int call_ret = ipc_shm_init(cfg, channel_id);
    if (call_ret)
    {
        perror("%s, ipc_shm_create fail", __func__);
        return -1;
    }

    // 释放锁
    if (release_lock(lock_fd, LOCK_UN) == -1)
    {
        perror("Failed to release lock");
        ipc_shm_uninit(channel_id);
        return -1;
    }
    return 0;
}

uint32_t *ipc_server_alloc_one_buf(uint8_t channel_id)
{
    ipc_ctrl_mgr_t *ipc_ctrl_mgr = get_ipc_ctrl_mgr(channel_id);
    if (!s_ipc_channel_enable(channel_id))
    {
        perror("%s, channel invalid", __func__);
        return NULL;
    }

    ctrl_block_t *ctrl_blk = &ipc_ctrl_mgr->ipc_ctrl_info->rw_ctrl;
    uint32_t used_block_num = (ctrl_blk->alloc - ctrl_blk->free);
    if (used_block_num >= ctrl_blk->ring_len)
    {
        return NULL; // run out
    }
    uint32_t index = ctrl_blk->alloc & ctrl_blk->ring_mask;
    uint32_t offset = ctrl_blk->block_bufs_offset[index];

    buffer_block_header_t *block_hdr = (buffer_block_header_t *)(ipc_ctrl_mgr->block_base_addr + offset);
    block_hdr->put_state = PUT_STATE_NOT_DONE;
    block_hdr->size = 0;
    uint32_t *buf = (uint32_t *)(block_hdr + 1);

    ctrl_blk->alloc++;
    return buf;
}

int32_t ipc_server_put_one_buf(uint8_t channel_id, uint32_t *buf, uint32_t size)
{
    ipc_ctrl_mgr_t *ipc_ctrl_mgr = get_ipc_ctrl_mgr(channel_id);
    if (!s_ipc_channel_enable(channel_id))
    {
        perror("%s, channel invalid", __func__);
        return -1;
    }

    ctrl_block_t *ctrl_blk = &ipc_ctrl_mgr->ipc_ctrl_info->rw_ctrl;

    uint8_t *block_buf = (uint8_t *)buf - sizeof(buffer_block_header_t);
    uint8_t *min_addr = ipc_ctrl_mgr->block_base_addr;
    uint8_t *max_addr = ipc_ctrl_mgr->block_base_addr + ipc_ctrl_mgr->ipc_ctrl_info->shm_block_buf.total_size - sizeof(ipc_ctrl_info_t);
    if (block_buf < min_addr || block_buf >= max_addr)
    {
        perror("%s, invalid buf addr", __func__);
        return -1;
    }

    if (size > ipc_ctrl_mgr->ipc_ctrl_info->shm_block_buf.block_size - sizeof(buffer_block_header_t))
    {
        perror("%s, invalid buf size", __func__);
    }

    buffer_block_header_t *block_hdr = (buffer_block_header_t *)block_buf;

    if (block_hdr->put_state == PUT_STATE_DONE)
    {
        perror("%s, this buf already put before", __func__);
        return -2;
    }

    uint32_t putable_num = ctrl_blk->alloc - ctrl_blk->put;
    if (putable_num == 0)
    {
        perror("%s, nothing to put", __func__);
        return -3;
    }

    block_hdr->size = size;
    block_hdr->put_state == PUT_STATE_DONE;

    uint32_t offset = block_buf - ipc_ctrl_mgr->block_base_addr;
    uint32_t index = ctrl_blk->put & ctrl_blk->ring_mask;
    ctrl_blk->block_bufs_offset[index] = offset;
    ctrl_blk->put++;

    return 0;
}

// -----------------------------------------------------------------------------------------------
int ipc_client_init(struct ipc_mem_cfg *cfg, uint8_t channel_id)
{
    int ret = 0;
    char shm_name[MAX_SHM_NAME] = {0};
    char *prefix = NULL;
    if (cfg && cfg->shm_name_prefix)
    {
        prefix = cfg->shm_name_prefix;
    }
    else
    {
        prefix = SHM_PRIX_NAME;
    }
    int ch_num = snprintf(shm_name, MAX_SHM_NAME - 1, "%s_%d", prefix, channel_id);
    if (ch_num < 0 || channel_id >= MAX_CHANNEL_NUM)
    {
        perror("%s, snprintf fail", __func__);
        ret = -1;
        goto end_process3;
    }
    reset_ipc_ctrl_mgr(channel_id);
    // 打开文件锁
    int lock_fd = open(LOCK_FILE, O_CREAT | O_RDWR, 0666);
    if (lock_fd == -1)
    {
        perror("Failed to open lock file");
        ret = -1;
        goto end_process3;
    }
    // 获取文件锁
    if (acquire_lock(lock_fd) == -1)
    {
        perror("Failed to acquire lock, tr");
        ret = -3;
        goto end_process3;
    }

    // shm share
    int shm_fd = shm_open(shm_name, O_RDWR, 0666);
    if (shm_fd == -1)
    {
        perror("Failed to open shm memory(%s)", shm_name);
        ret = -1;
        goto end_process3;
    }

    // 获得shm_size
    struct stat buf;
    if (fstat(shm_fd, &buf) == -1)
    {
        perror("fstat failed");
        ret = -1;
        goto end_process2;
    }
    size_t shm_size = buf.st_size;

    int *shm_mem = (int *)(mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0));
    if (MAP_FAILED == *shm_mem)
    {
        perror("Failed to mmap the shm mem(%s), alloc_size=%d", shm_name, shm_size);
        ret = -1;
        goto end_process2;
    }

    if (((ipc_ctrl_info_t *)shm_mem)->rw_ctrl.channel_state == IPC_CHANNEL_ENABLE)
    {
        // set mgr
        ipc_ctrl_mgr_t *ipc_ctrl_mgr = get_ipc_ctrl_mgr(channel_id);
        ipc_ctrl_mgr->shm_fd = shm_fd;
        ipc_ctrl_mgr->ipc_ctrl_info = (ipc_ctrl_info_t *)shm_mem;
        ipc_ctrl_mgr->block_base_addr = shm_mem + sizeof(ipc_ctrl_info_t);
    }
    else
    {
        perror("ipc channel is not enable");
        ret = -3;
        goto end_process1;
    }

end_process1:
    // 释放锁
    if (release_lock(lock_fd) == -1)
    {
        perror("Failed to release lock");
        ret = -1;
    }
    munmap(shm_mem, shm_size);
end_process2:
    close(shm_fd);
end_process3:
    return ret;
}

uint32_t *ipc_client_get_one_buf(uint8_t channel_id, uint32_t *size)
{
    ipc_ctrl_mgr_t *ipc_ctrl_mgr = get_ipc_ctrl_mgr(channel_id);
    if (!s_ipc_channel_enable(channel_id))
    {
        perror("%s, channel invalid", __func__);
        return NULL;
    }

    ctrl_block_t *ctrl_blk = &ipc_ctrl_mgr->ipc_ctrl_info->rw_ctrl;
    uint32_t getable_num = (ctrl_blk->put - ctrl_blk->get);
    if (getable_num == 0)
    {
        return NULL; // no valid buf
    }
    uint32_t index = ctrl_blk->get & ctrl_blk->ring_mask;
    uint32_t offset = ctrl_blk->block_bufs_offset[index];

    buffer_block_header_t *block_hdr = (buffer_block_header_t *)(ipc_ctrl_mgr->block_base_addr + offset);
    *size = block_hdr->size;
    uint32_t *buf = (uint32_t *)(block_hdr + 1);

    block_hdr->free_state = FREE_STATE_NOT_DONE;
    ctrl_blk->get++;
    return buf;
}

void ipc_client_free_one_buf(uint8_t channel_id, uint32_t *buf)
{
    ipc_ctrl_mgr_t *ipc_ctrl_mgr = get_ipc_ctrl_mgr(channel_id);
    if (!s_ipc_channel_enable(channel_id))
    {
        perror("%s, channel invalid", __func__);
        return NULL;
    }
    ctrl_block_t *ctrl_blk = &ipc_ctrl_mgr->ipc_ctrl_info->rw_ctrl;
    uint8_t *block_buf = (uint8_t *)buf - sizeof(buffer_block_header_t);
    buffer_block_header_t *block_hdr = (buffer_block_header_t *)block_buf;
    uint8_t *min_addr = ipc_ctrl_mgr->block_base_addr;
    uint8_t *max_addr = ipc_ctrl_mgr->block_base_addr + ipc_ctrl_mgr->ipc_ctrl_info->shm_block_buf.total_size - sizeof(ipc_ctrl_info_t);
    if (block_buf < min_addr || block_buf >= max_addr)
    {
        perror("%s, invalid buf addr", __func__);
        return -1;
    }

    if (block_hdr->free_state == FREE_STATE_DONE)
    {
        perror("%s, this buf already free before", __func__);
        return -2;
    }

    block_hdr->free_state = FREE_STATE_DONE;
    block_hdr->size = 0;

    ctrl_blk->free++;
}
