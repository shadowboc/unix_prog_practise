#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/file.h>
#include <sys/stat.h>

#define SHM_NAME "/my_shared_memory"
#define LOCK_FILE "/tmp/my_lockfile.lock"
#define SHM_SIZE sizeof(int)

int main(int argc, char** argv) {
    int lock_fd = open(LOCK_FILE, O_CREAT | O_RDWR, 0666);
    if (lock_fd == -1) {
        perror("Failed to open lock file");
        return 1;
    }
    int shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("Failed to open shm memory");
        return 1;
    }

    int* share_mem = (int*)(mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0));
    if (MAP_FAILED == share_mem) {
        perror("Failed to mmap the shm mem");
        close(shm_fd);
        close(lock_fd);
        return 1;
    }

    for (int i = 0; i < 5; ++i) {
        // 获取文件锁
        if (flock(lock_fd, LOCK_EX) == -1) {
            perror("Failed to acquire lock");
            break;
        }

        // 读取共享计数器值
        // std::cout << "Reader: Current counter value is " << *share_mem << std::endl;
        printf("Reader: Current counter value is %d, addr=%p\n", *share_mem, share_mem);

        // 释放锁
        if (flock(lock_fd, LOCK_UN) == -1) {
            perror("Failed to release lock");
            break;
        }

        sleep(1);  // 模拟一些处理时间
    }

    // 清理资源
    munmap(share_mem, SHM_SIZE);
    close(shm_fd);
    close(lock_fd);

    return 0;
}