#ifndef SAND_IPC_TIDEWAY_H
#define SAND_IPC_TIDEWAY_H
#include <stdint.h>
/* TIDEWAY: SAND's own message-passing layer. Named channels, each a small ring of short messages.
 * Syscalls run with IRQs off on one CPU, so no locks are needed. Messages carry the sender's pid
 * (stamped by the kernel, not by the sender). Any process holding the ipc.channel permission can
 * open any channel by name: a channel is shared by name, not private to a pair of processes. */
#define TIDEWAY_CHANNELS 8u
#define TIDEWAY_QUEUE 8u
#define TIDEWAY_NAME_MAX 16u
#define TIDEWAY_MSG_MAX 112u
struct tideway_msg { uint32_t from; uint32_t length; uint8_t data[TIDEWAY_MSG_MAX]; };
int32_t tideway_open(const char *name);                 /* handle >= 1, creating the channel if needed; <0 table full / bad name */
int32_t tideway_send(int32_t handle, uint32_t from, const void *data, uint32_t length);  /* 0, or SAND_E_FAIL (bad handle/length), SAND_E_FULL */
int32_t tideway_recv(int32_t handle, struct tideway_msg *out);   /* 1 message, 0 none, SAND_E_FAIL bad handle */
uint32_t tideway_pending(int32_t handle);
#endif
