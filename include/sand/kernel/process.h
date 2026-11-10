#ifndef SAND_KERNEL_PROCESS_H
#define SAND_KERNEL_PROCESS_H
#include <stdint.h>
#define SAND_PROCESS_LIMIT 3u
#define SAND_PROCESS_NAME_MAX 16u
typedef uint32_t sand_pid_t;
enum sand_process_state { SAND_PROCESS_UNUSED=0, SAND_PROCESS_READY, SAND_PROCESS_RUNNING, SAND_PROCESS_TERMINATED };
struct sand_process_info { sand_pid_t pid; sand_pid_t parent_pid; uint32_t parent_thread_id; enum sand_process_state state; uint32_t page_directory; uint32_t entry; uint32_t user_stack; int32_t exit_code; char name[SAND_PROCESS_NAME_MAX]; };
int process_system_init(void);
int process_create_test(const char *name, sand_pid_t *pid_out);
int process_create_elf(const char *name, const void *image, uint32_t image_size, sand_pid_t *pid_out);
int process_activate(sand_pid_t pid);
int process_exit(sand_pid_t pid, int32_t exit_code);
int process_terminate(sand_pid_t pid);
int process_wait(sand_pid_t pid, int32_t *exit_code);
int process_destroy(sand_pid_t pid);
int process_get_info(sand_pid_t pid, struct sand_process_info *out);
sand_pid_t process_current(void);
void process_launch(sand_pid_t pid);
#endif
