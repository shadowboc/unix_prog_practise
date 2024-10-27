#pragma once

/**
 * @description: 为指定通道创建共享内存，并初始化管理结构体对象。
 * @param {ipc_mem_cfg*} cfg 指定ipc通道的共享内存分配的需求，以及共享内存的前缀名
 * @param {uint8_t} channel_id 指定通道id[0-7]
 * @return {*} 0表示成功，负值表示失败
 */
int ipc_server_init(struct ipc_mem_cfg* cfg, uint8_t channel_id);

/**
 * @description: 从指定通道关联的共享内存中分配一个buf。
 * @param {uint8_t} channel_id 指定的通道id[0-7]
 * @return {*} databuf指针
 */
uint32_t* ipc_server_alloc_one_buf(uint8_t channel_id);

/**
 * @description: 将前面分配的buf,向关联的通道提交，提交后，对端才可以获取，否则对端获取不到buf
 * @param {uint8_t} channel_id 指定的通道id[0-7]
 * @param {uint32_t*} buf 从通道分配的databuf
 * @param {uint32_t} size 提交的databuf数据量
 * @return {*} 0表示成功, 负值表示失败
 */
int32_t ipc_server_put_one_buf(uint8_t channel_id, uint32_t* buf, uint32_t size);

/**
 * @description: 打开serve创建的共享内存，并初始化本地管理结构体对象
 * @param {ipc_mem_cfg*} cfg 包含共享内存的前缀名，可以为NULL，表示共享内存名使用的默认的。
 * @param {uint8_t} channel_id 指定的通道id[0-7]
 * @return {*} 0表示成功，负值表示失败
 */
int ipc_client_init(struct ipc_mem_cfg* cfg, uint8_t channel_id);

/**
 * @description: 从指定通道关联的共享内存中获取一个可用的buf。只有server端提交(put)的buf，才可以被获取
 * @param {uint8_t} channel_id 指定的通道id[0-7]
 * @param {uint32_t*} size 获取的buf中的databuf的数据量
 * @return {*} databuf指针
 */
uint32_t* ipc_client_get_one_buf(uint8_t channel_id, uint32_t* size);

/**
 * @description: 释放buf到与buf关联的指定通道
 * @param {uint8_t} channel_id 指定的通道id[0-7]
 * @param {uint32_t*} buf 为databuf的指针
 * @return {*}
 */
void ipc_client_free_one_buf(uint8_t channel_id, uint32_t* buf);

