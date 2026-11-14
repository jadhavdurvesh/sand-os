#ifndef SAND_KERNEL_PROCESS_H
#define SAND_KERNEL_PROCESS_H
#include <stdint.h>
#define SAND_PROCESS_LIMIT 6u
#define SAND_PID_ORPHAN 0xFFFFFFFFu /* parent exited: auto-reaped on exit */
#define SAND_PROCESS_NAME_MAX 16u
typedef uint32_t sand_pid_t;
enum sand_process_state { SAND_PROCESS_UNUSED=0, SAND_PROCESS_READY, SAND_PROCESS_RUNNING, SAND_PROCESS_TERMINATED };
struct sand_process_info { sand_pid_t pid; sand_pid_t parent_pid; uint32_t parent_thread_id; enum sand_process_state state; uint32_t page_directory; uint32_t entry; uint32_t user_stack; int32_t exit_code; char name[SAND_PROCESS_NAME_MAX]; };
int process_system_init(void);
int process_create_test(const char *name, sand_pid_t *pid_out);
int process_create_elf(const char *name, const void *image, uint32_t image_size, sand_pid_t *pid_out);
int process_create_sapp(const char *name, const void *image, uint32_t image_size, sand_pid_t *pid_out);
int process_activate(sand_pid_t pid);
int process_exit(sand_pid_t pid, int32_t exit_code);
int process_terminate(sand_pid_t pid);
int process_wait(sand_pid_t pid, int32_t *exit_code);
int process_destroy(sand_pid_t pid);
int process_get_info(sand_pid_t pid, struct sand_process_info *out);
sand_pid_t process_current(void);
void process_launch(sand_pid_t pid);

/* M7: processes that run as scheduler-controlled ring-3 threads. */
int process_spawn_sapp(const char *name, const void *image, uint32_t image_size, sand_pid_t *pid_out);
int process_spawn_detached(const char *name, const void *image, uint32_t image_size, sand_pid_t *pid_out);
void process_scheduler_hook(uint32_t thread_id);
uint32_t process_exit_current(int32_t exit_code, uint32_t saved_stack);
uint32_t process_fault_current(uint32_t vector, uint32_t saved_stack);
/* Returns 1 and the status when pid is a terminated child of the caller,
 * 0 while it still runs, -1 when pid is not a child of the caller. */
int process_poll_child(sand_pid_t pid, int32_t *exit_code);
const void *process_wait_object(sand_pid_t pid);
uint32_t *process_sleep_deadline(void);
int process_list_at(uint32_t ordinal, struct sand_process_info *out);
uint32_t process_live_count(void);
#endif
