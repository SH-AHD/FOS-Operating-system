# FOS_Operating-System'24 Project
# About The Project

FOS is an educational operating system developed for the Operating Systems Course (CSW355) at Ain Shams University. Forked and refactored from MIT's Operating Systems Lab 6.828, it is designed as a simplified implementation to demonstrate core OS concepts. 

This project was a collaborative effort by a team of ###six students, each contributing to different modules and functionalities, under the guidance of Dr. Ahmed Salah. It serves as a practical educational tool to provide hands-on experience with operating system concepts.

# Modules

## Command Prompt
- Features a simple command-line interface for executing kernel-level commands.
- Supports commands like `kernel_info`, `nclock`, and others for interaction and testing.

## System Calls
- Acts as a secure bridge between user programs and the kernel.
- Handles user-provided pointers and ensures robust processing of invalid memory access.

## Dynamic Allocator
### Kernel Heap
- Implements dynamic memory allocation using block and page-level allocators.
- Utilizes the **First-Fit** strategy for efficient block and page allocation.
- Provides essential functions such as:
  - `kmalloc`: For memory allocation.
  - `kfree`: For freeing allocated memory.

### User Heap
- Offers memory allocation for user programs with lazy allocation techniques through system calls.

## Memory Manager
- **Page Allocator**: Manages memory allocation at the page level for better efficiency.
- **Shared Memory**: Enables interprocess communication by allowing processes to share memory regions.

## Fault Handler
- **Page Fault Handling**:
  - Implements lazy allocation to optimize memory usage.
  - Utilizes replacement policies to address memory faults efficiently.
- **Nth Chance Clock Replacement**:
  - An optimized page replacement algorithm balancing memory usage and performance.

## Synchronization and Protection (Locks & Semaphores)
### Locks
- **Spinlocks**:
  - Designed for short critical sections using busy-waiting.
- **Sleeplocks**:
  - Used for longer critical sections by blocking threads, reducing CPU wastage.
- Ensures safe and efficient access to shared kernel resources.

### Semaphores
- Provides user-level semaphores for process synchronization.
- Key operations include:
  - `semWait`: Decrements the semaphore value; blocks the process if the value is negative.
  - `semSignal`: Increments the semaphore value; unblocks waiting processes if applicable.
- Addresses issues like deadlocks and priority inversion using proper locking mechanisms.

## CPU Scheduler
- **Priority Round-Robin Scheduler**:
  - A preemptive scheduling algorithm with multiple priority levels.
  - Prevents process starvation by promoting processes based on their wait time.
  - Supports commands like `schedPRIRR` for dynamic scheduler configuration.


