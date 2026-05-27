/*=====================================================================================
 HEADER NAME: .c
 MODULE NAME: application module.
 
 PRE-INCLUDE FILES DESCRIPTION: 	
 
 GENERAL DESCRIPTION: 	
 	This File will gather functions that special handle msg(UserEvent,Timer) from 
 	Intergration.These functions don't be changed by project changed.
 =======================================================================================	
 Revision History: 
 ---------------------------------------
 Author: sheng.dong
 Date: 2025-03-10 13:27:00
 LastEditors: sheng.dong
 LastEditTime: 2025-03-10 14:09:09
 FilePath: \SDK\apps\earphone\third_part\tuya\rdx_queue.h
 
 Self-documenting Code
=====================================================================================*/

#ifndef __RDX_QUEUE_H__
#define __RDX_QUEUE_H__

/******************************************************************************
* Include files
******************************************************************************/ 
#include "includes.h"

/******************************************************************************
* Macro Define Section
******************************************************************************/ 
#define MAX_SEND_QUEUE_SIZE                  15    // 队列中最多允许的元素个数
#define MAX_RECV_QUEUE_SIZE                  15    // 队列中最多允许的元素个数

/******************************************************************************
* Structure and Enum Section
******************************************************************************/ 
// 数据包结构
typedef struct {
    uint8_t *data;
    uint32_t data_length;
} DataPacket;

// 队列节点结构
typedef struct QueueNode {
    DataPacket packet;
    struct QueueNode* next;
} QueueNode;

typedef void (*queue_packet_free_fn)(DataPacket *packet);

// 队列结构
typedef struct {
    QueueNode* front;
    QueueNode* rear;
    uint32_t size;
    uint32_t max_size;
    queue_packet_free_fn custom_free;
} Queue;

/******************************************************************************
* Function Section
******************************************************************************/ 
void rdx_queue_init(Queue* queue, uint32_t max_size);
bool rdx_queue_enqueue(Queue* queue, DataPacket packet);
bool rdx_queue_enqueue_head(Queue* queue, DataPacket packet);
bool rdx_queue_dequeue(Queue* queue, DataPacket* packet);
uint32_t rdx_queue_getQueueSize(Queue* queue);
void rdx_queue_clearQueue(Queue* queue);
bool rdx_queue_isEmpty(Queue* queue);
bool rdx_queue_peek(Queue* queue, DataPacket* packet);
bool rdx_queue_drop_rear(Queue* queue);


#endif /* __RDX_QUEUE_H__ */