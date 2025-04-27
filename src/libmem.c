/*
 * Copyright (C) 2025 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Sierra release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

 #include "string.h"
 #include "mm.h"
 #include "syscall.h"
 #include "libmem.h"
 #include <stdlib.h>
 #include <stdio.h>
 #include <pthread.h>
 
 static pthread_mutex_t mmvm_lock = PTHREAD_MUTEX_INITIALIZER;
 
 /*
  * enlist_vm_freerg_list - add new rg to freerg_list
  */
 int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct *rg_elmt)
 {
     struct vm_rg_struct *rg_node = mm->mmap->vm_freerg_list;
 
     if (rg_elmt->rg_start >= rg_elmt->rg_end)
         return -1;
 
     if (rg_node != NULL)
         rg_elmt->rg_next = rg_node;
 
     mm->mmap->vm_freerg_list = rg_elmt;
 
     return 0;
 }
 
 /*
  * get_symrg_byid - get mem region by region ID
  */
 struct vm_rg_struct *get_symrg_byid(struct mm_struct *mm, int rgid)
 {
     if (rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
         return NULL;
 
     return &mm->symrgtbl[rgid];
 }
 
 /*
  * __alloc - allocate a region memory
  */
 int __alloc(struct pcb_t *caller, int vmaid, int rgid, int size, int *alloc_addr)
 {
     struct vm_rg_struct rgnode;
     pthread_mutex_lock(&mmvm_lock);
 
     if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0)
     {
         caller->mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
         caller->mm->symrgtbl[rgid].rg_end = rgnode.rg_end;
 
         *alloc_addr = rgnode.rg_start;
 
         #ifdef IODUMP
         printf("===== PHYSICAL MEMORY AFTER ALLOCATION =====\n");
         printf("PID=%d - Region=%d - Address=%08X - Size=%d byte\n", 
                caller->pid, rgid, *alloc_addr, size);
         print_pgtbl(caller, 0, -1);
         printf("================================================================\n");
         #endif
 
         pthread_mutex_unlock(&mmvm_lock);
         return 0;
     }
 
     struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
     if (cur_vma == NULL) {
         pthread_mutex_unlock(&mmvm_lock);
         return -1; /* Invalid VMA */
     }
 
     int inc_sz = PAGING_PAGE_ALIGNSZ(size);
     int old_sbrk = cur_vma->sbrk;
 
     struct sc_regs regs;
     regs.a1 = SYSMEM_INC_OP;
     regs.a2 = vmaid;
     regs.a3 = inc_sz;
 
     if (syscall(caller, 17, &regs) < 0) {
         pthread_mutex_unlock(&mmvm_lock);
         return -1; /* Syscall failed */
     }
 
     if (inc_vma_limit(caller, vmaid, inc_sz) < 0) {
         pthread_mutex_unlock(&mmvm_lock);
         return -1; /* Failed to increase limit */
     }
 
     caller->mm->symrgtbl[rgid].rg_start = old_sbrk;
     caller->mm->symrgtbl[rgid].rg_end = old_sbrk + inc_sz;
     *alloc_addr = old_sbrk;
 
     #ifdef IODUMP
     printf("===== PHYSICAL MEMORY AFTER ALLOCATION =====\n");
     printf("PID=%d - Region=%d - Address=%08X - Size=%d byte\n", 
            caller->pid, rgid, *alloc_addr, size);
     print_pgtbl(caller, 0, -1);
     printf("================================================================\n");
     #endif
 
     pthread_mutex_unlock(&mmvm_lock);
     return 0;
 }
 
 /*
  * __free - remove a region memory
  */
 int __free(struct pcb_t *caller, int vmaid, int rgid)
 {
     if (rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
         return -1;
 
     struct vm_rg_struct *rgnode = malloc(sizeof(struct vm_rg_struct));
     rgnode->rg_start = caller->mm->symrgtbl[rgid].rg_start;
     rgnode->rg_end = caller->mm->symrgtbl[rgid].rg_end;
 
     caller->mm->symrgtbl[rgid].rg_start = 0;
     caller->mm->symrgtbl[rgid].rg_end = 0;
 
     enlist_vm_freerg_list(caller->mm, rgnode);
 
     #ifdef IODUMP
     printf("===== PHYSICAL MEMORY AFTER DEALLOCATION =====\n");
     printf("PID=%d - Region=%d\n", caller->pid, rgid);
     print_pgtbl(caller, 0, -1);
     printf("================================================================\n");
     #endif
 
     return 0;
 }
 
 /*
  * liballoc - PAGING-based allocate a region memory
  */
 int liballoc(struct pcb_t *proc, uint32_t size, uint32_t reg_index)
 {
     int addr;
     return __alloc(proc, 0, reg_index, size, &addr);
 }
 
 /*
  * libfree - PAGING-based free a region memory
  */
 int libfree(struct pcb_t *proc, uint32_t reg_index)
 {
     return __free(proc, 0, reg_index);
 }
 
 /*
  * pg_getpage - get the page in ram
  */
 int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller)
 {
     uint32_t pte = mm->pgd[pgn];
 
     if (!PAGING_PAGE_PRESENT(pte))
     {
         int vicpgn;
         if (find_victim_page(caller->mm, &vicpgn) < 0) {
             return -1; /* No victim available */
         }
 
         uint32_t vicpte = mm->pgd[vicpgn];
         int vicfpn = PAGING_PTE_FPN(vicpte);
 
         int swpfpn;
         if (MEMPHY_get_freefp(caller->active_mswp, &swpfpn) < 0) {
             return -1; /* No swap space */
         }
 
         struct sc_regs regs;
         regs.a1 = SYSMEM_SWP_OP;
         regs.a2 = vicfpn;
         regs.a3 = swpfpn;
         if (syscall(caller, 17, &regs) < 0) {
             MEMPHY_put_freefp(caller->active_mswp, swpfpn);
             return -1;
         }
 
         pte_set_swap(&mm->pgd[vicpgn], 0, swpfpn);
 
         int tgtfpn = PAGING_PTE_SWP(pte);
 
         regs.a1 = SYSMEM_SWP_OP;
         regs.a2 = tgtfpn;
         regs.a3 = vicfpn;
         if (syscall(caller, 17, &regs) < 0) {
             return -1; /* Assume kernel rollback */
         }
 
         MEMPHY_put_freefp(caller->active_mswp, tgtfpn);
         pte_set_fpn(&mm->pgd[pgn], vicfpn);
 
         enlist_pgn_node(&caller->mm->fifo_pgn, pgn);
     }
 
     *fpn = PAGING_PTE_FPN(mm->pgd[pgn]);
     return 0;
 }
 
 /*
  * pg_getval - read value at given offset
  */
 int pg_getval(struct mm_struct *mm, int addr, BYTE *data, struct pcb_t *caller, int *phyaddr_out)
 {
     int pgn = PAGING_PGN(addr);
     int off = PAGING_OFFST(addr);
     int fpn;
 
     if (pg_getpage(mm, pgn, &fpn, caller) != 0) {
         #ifdef IODUMP
         printf("\tpg_getval failed: invalid page pgn=%d\n", pgn);
         #endif
         return -1; /* invalid page access */
     }
 
     int phyaddr = (fpn * PAGING_PAGESZ) + off;
     if (phyaddr >= caller->mram->maxsz) {
         #ifdef IODUMP
         printf("\tpg_getval failed: phyaddr=%08X out of bounds\n", phyaddr);
         #endif
         return -1; /* Address out of bounds */
     }
 
     struct sc_regs regs;
     regs.a1 = SYSMEM_IO_READ;
     regs.a2 = phyaddr;
 
     if (syscall(caller, 17, &regs) < 0) {
         #ifdef IODUMP
         printf("\tpg_getval failed: syscall SYSMEM_IO_READ phyaddr=%08X\n", phyaddr);
         #endif
         return -1;
     }
 
     *data = (BYTE)regs.a3;
     *phyaddr_out = phyaddr;
     
     #ifdef IODUMP
     printf("\tpg_getval: phyaddr=%08X value=%d\n", phyaddr, *data);
     #endif
     return 0;
 }
 
 /*
  * pg_setval - write value to given offset
  */
 int pg_setval(struct mm_struct *mm, int addr, BYTE value, struct pcb_t *caller, int *phyaddr_out)
 {
     int pgn = PAGING_PGN(addr);
     int off = PAGING_OFFST(addr);
     int fpn;
 
     if (pg_getpage(mm, pgn, &fpn, caller) != 0) {
         #ifdef IODUMP
         printf("\tpg_setval failed: invalid page pgn=%d\n", pgn);
         #endif
         return -1; /* invalid page access */
     }
 
     int phyaddr = (fpn * PAGING_PAGESZ) + off;
     if (phyaddr >= caller->mram->maxsz) {
         #ifdef IODUMP
         printf("\tpg_setval failed: phyaddr=%08X out of bounds\n", phyaddr);
         #endif
         return -1; /* Address out of bounds */
     }
 
     struct sc_regs regs;
     regs.a1 = SYSMEM_IO_WRITE;
     regs.a2 = phyaddr;
     regs.a3 = value;
 
     if (syscall(caller, 17, &regs) < 0) {
         #ifdef IODUMP
         printf("\tpg_setval failed: syscall SYSMEM_IO_WRITE phyaddr=%08X\n", phyaddr);
         #endif
         return -1; /* Write failed */
     }
 
     *phyaddr_out = phyaddr;
 
     #ifdef IODUMP
     printf("\tpg_setval: phyaddr=%08X value=%d\n", phyaddr, value);
     #endif
     return 0;
 }
 
 /*
  * __read - read value in region memory
  */
 int __read(struct pcb_t *caller, int vmaid, int rgid, int offset, BYTE *data)
 {
     struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);
     struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
 
     if (currg == NULL || cur_vma == NULL) {
         #ifdef IODUMP
         printf("\t__read failed: invalid rgid=%d or vmaid=%d\n", rgid, vmaid);
         #endif
         return -1; /* Invalid memory identify */
     }
 
     int phyaddr; /* Không sử dụng ở đây, nhưng giữ để tương thích */
     return pg_getval(caller->mm, currg->rg_start + offset, data, caller, &phyaddr);
 }
 
 /*
  * libread - PAGING-based read a region memory
  */
 int libread(struct pcb_t *proc, uint32_t source, uint32_t offset, uint32_t *destination)
 {
     BYTE data;
     int phyaddr;
     struct vm_rg_struct *currg = get_symrg_byid(proc->mm, source);
     struct vm_area_struct *cur_vma = get_vma_by_num(proc->mm, 0);
 
     if (currg == NULL || cur_vma == NULL) {
         #ifdef IODUMP
         printf("\tlibread failed: invalid region=%d or vmaid=0\n", source);
         #endif
         return -1;
     }
 
     #ifdef IODUMP
     printf("===== PHYSICAL MEMORY AFTER READING =====\n");
     printf("\tread region=%d offset=%d value=", source, offset);
     #endif
 
     int val = pg_getval(proc->mm, currg->rg_start + offset, &data, proc, &phyaddr);
 
     if (val == 0) {
         *destination = data;
         #ifdef IODUMP
         printf("%d\n", data);
         print_pgtbl(proc, 0, -1);
         printf("================================================================\n");
         MEMPHY_dump(proc->mram);
         printf("================================================================\n");
         #endif
     } else {
         #ifdef IODUMP
         printf("failed\n");
         #endif
     }
 
     return val;
 }
 
 /*
  * __write - write a region memory
  */
 int __write(struct pcb_t *caller, int vmaid, int rgid, int offset, BYTE value)
 {
     struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);
     struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
 
     if (currg == NULL || cur_vma == NULL) {
         #ifdef IODUMP
         printf("\t__write failed: invalid rgid=%d or vmaid=%d\n", rgid, vmaid);
         #endif
         return -1; /* Invalid memory identify */
     }
 
     int phyaddr; /* Không sử dụng ở đây, nhưng giữ để tương thích */
     return pg_setval(caller->mm, currg->rg_start + offset, value, caller, &phyaddr);
 }
 
 /*
  * libwrite - PAGING-based write a region memory
  */
 int libwrite(struct pcb_t *proc, BYTE data, uint32_t destination, uint32_t offset)
 {
     int phyaddr;
     struct vm_rg_struct *currg = get_symrg_byid(proc->mm, destination);
     struct vm_area_struct *cur_vma = get_vma_by_num(proc->mm, 0);
 
     if (currg == NULL || cur_vma == NULL) {
         #ifdef IODUMP
         printf("\tlibwrite failed: invalid region=%d or vmaid=0\n", destination);
         #endif
         return -1;
     }
 
     #ifdef IODUMP
     printf("===== PHYSICAL MEMORY AFTER WRITING =====\n");
     printf("\twrite region=%d offset=%d value=%d\n", destination, offset, data);
     print_pgtbl(proc, 0, -1);
     printf("================================================================\n");
     #endif
 
     int ret = pg_setval(proc->mm, currg->rg_start + offset, data, proc, &phyaddr);
 
     #ifdef IODUMP
     MEMPHY_dump(proc->mram);
     printf("================================================================\n");
     #endif
 
     return ret;
 }
 
 /*
  * free_pcb_memphy - collect all memphy of pcb
  */
 int free_pcb_memph(struct pcb_t *caller)
 {
     int pagenum, fpn;
     uint32_t pte;
 
     for (pagenum = 0; pagenum < PAGING_MAX_PGN; pagenum++)
     {
         pte = caller->mm->pgd[pagenum];
 
         if (PAGING_PAGE_PRESENT(pte)) {
             fpn = PAGING_PTE_FPN(pte);
             MEMPHY_put_freefp(caller->mram, fpn);
         } else {
             fpn = PAGING_PTE_SWP(pte);
             MEMPHY_put_freefp(caller->active_mswp, fpn);
         }
     }
 
     return 0;
 }
 
 /*
  * find_victim_page - find victim page
  */
 int find_victim_page(struct mm_struct *mm, int *retpgn)
 {
     struct pgn_t *pg = mm->fifo_pgn;
 
     if (pg == NULL) {
         return -1;
     }
     *retpgn = pg->pgn;
     mm->fifo_pgn = pg->pg_next;
     free(pg);
 
     return 0;
 }
 
 /*
  * get_free_vmrg_area - get a free vm region
  */
 int get_free_vmrg_area(struct pcb_t *caller, int vmaid, int size, struct vm_rg_struct *newrg)
 {
     struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
     if (cur_vma == NULL) {
         return -1;
     }
 
     struct vm_rg_struct *rgit = cur_vma->vm_freerg_list;
 
     if (rgit == NULL)
         return -1;
 
     newrg->rg_start = newrg->rg_end = -1;
 
     while (rgit != NULL) {
         int region_size = rgit->rg_end - rgit->rg_start;
         if (region_size >= size) {
             newrg->rg_start = rgit->rg_start;
             newrg->rg_end = rgit->rg_start + size;
             return 0;
         }
         rgit = rgit->rg_next;
     }
 
     return -1;
 }