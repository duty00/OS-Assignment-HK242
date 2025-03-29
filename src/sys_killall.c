/*
 * Copyright (C) 2025 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Sierra release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

 #include "common.h"
 #include "syscall.h"
 #include "stdio.h"
 #include "libmem.h"
 
 #include "queue.h"
 #include "string.h"
 
 void remove_process(struct queue_t *q, int index) {
     if (index < 0 || index >= q->size) return; // Kiểm tra index hợp lệ
     for (int i = index; i < q->size - 1; i++) {
         q->proc[i] = q->proc[i + 1]; // Dịch chuyển phần tử
     }
     q->size--; // Giảm kích thước hàng đợi
 }
 
 int __sys_killall(struct pcb_t *caller, struct sc_regs* regs)
 {
     char proc_name[100];
     uint32_t data;
 
     //hardcode for demo only
     uint32_t memrg = regs->a1;
     
     /* TODO: Get name of the target proc */
     //proc_name = libread..
     int i = 0;
     data = 0;
     while(data != -1){
         libread(caller, memrg, i, &data);
         proc_name[i]= data;
         if(data == -1) proc_name[i]='\0';
         i++;
     }
     printf("The procname retrieved from memregionid %d is \"%s\"\n", memrg, proc_name);
 
     /* TODO: Traverse proclist to terminate the proc
      *       stcmp to check the process match proc_name
      */
     
     //caller->running_list
     //caller->mlq_ready_queue
     struct queue_t *queue = caller->running_list;
     struct queue_t *mlq_queue = caller->mlq_ready_queue;
     for (int i = 0; i < queue->size; i++) {
         if (strcmp(queue->proc[i]->path, proc_name) == 0) {
             printf("Terminating process: %s (PID: %d)\n", queue->proc[i]->path, queue->proc[i]->pid);
             free(queue->proc[i]);
             remove_process(queue, i);
             i--; // Giảm index để tránh bỏ sót phần tử
         }
     }
 
 
     /* TODO Maching and terminating 
      *       all processes with given
      *        name in var proc_name
      */
     if (mlq_queue) {
         for (int i = 0; i < mlq_queue->size; i++) {
             if (strcmp(mlq_queue->proc[i]->path, proc_name) == 0) {
                 printf("Terminating queued process: %s (PID: %d)\n", mlq_queue->proc[i]->path, mlq_queue->proc[i]->pid);
                 free(mlq_queue->proc[i]);
                 remove_process(mlq_queue, i);
                 i--; // Giảm index để tránh bỏ sót phần tử
             }
         }
     }
 
     return 0; 
 }
 
