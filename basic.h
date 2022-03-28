#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/sched.h>	
#include <linux/pid.h>		
#include <linux/tty.h>		
#include <linux/version.h>
#include <linux/workqueue.h>
#include <linux/moduleparam.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/slab.h>

#define MODNAME "CHAR DEV"

#define MINORS 128


#define HIGH_PRIORITY 0
#define LOW_PRIORITY 1
#define ON 1
#define OFF 0

int state_devices[MINORS];
int bytes_high_priority[MINORS];
int bytes_low_priority[MINORS];
int waiting_threads_high_priority[MINORS];
int waiting_threads_low_priority[MINORS];

module_param_array(state_devices, int, NULL, 0660);
module_param_array(bytes_high_priority, int, NULL, 0660);
module_param_array(bytes_low_priority, int, NULL, 0660);
module_param_array(waiting_threads_high_priority, int, NULL, 0660);
module_param_array(waiting_threads_low_priority, int, NULL, 0660);

#define SLOT_IOCTL_MAGIC 'x'
#define IOCTL_SET_PRIORITY _IOW(SLOT_IOCTL_MAGIC, 0, unsigned int)
#define IOCTL_SET_BLOCKING_WRITE _IOW(SLOT_IOCTL_MAGIC, 1, unsigned int)
#define IOCTL_SET_BLOCKING_READ _IOW(SLOT_IOCTL_MAGIC, 2, unsigned int)
#define IOCTL_SET_TIMEOUT_ON _IOW(SLOT_IOCTL_MAGIC, 3, unsigned int)
#define IOCTL_SET_TIMEOUT_OFF _IO(SLOT_IOCTL_MAGIC, 4)
#define IOCTL_ENABLE_DEVICE _IOW(SLOT_IOCTL_MAGIC, 5, unsigned int)
#define IOCTL_DISABLE_DEVICE _IOW(SLOT_IOCTL_MAGIC, 6, unsigned int)



#define DEVICE_NAME "my-new-dev" 

static int Major;          

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
#define get_major(session)	MAJOR(session->f_inode->i_rdev)
#define get_minor(session)	MINOR(session->f_inode->i_rdev)
#else
#define get_major(session)	MAJOR(session->f_dentry->d_inode->i_rdev)
#define get_minor(session)	MINOR(session->f_dentry->d_inode->i_rdev)
#endif


#define OBJECT_MAX_SIZE  (4096) //just one page
//struttura per lo stato di ogni device
//con indice [0] si accedono agli attributi del flusso con alta priorità
//con indice [1] si accedono agli attributi del flusso con bassa priorità
typedef struct _object_state {
	struct mutex operation_synchronizer[2]; //un mutex per flusso
	loff_t valid_bytes[2]; //byte validi per ogni flusso
	char *stream_content[2]; //buffer per le scritture/letture sul device
	int priority; //vale 1 se è priorità bassa, 0 priorità alta, vale 1 di default.
	int blocking_read; //vale 1 (ON) se è bloccante, 0 (OFF) altrimenti, vale 0FF di default
	int blocking_write; //vale 1 (ON) se è bloccante, 0 (OFF) altrimenti, vale OFF di deault
	int timeout; //vale 1 (ON) se è attivo, 0 (OFF) altrimenti, vale OFF di default
	int jiffies; //tempo trascorso nella waitqueue, vale 500 (5 secondi) di default
} object_state;

object_state objects[MINORS];

//struttura per gestire le waitqueue
typedef struct _elem {
	struct task_struct *task;	
	int pid;
	char *mod; //vale "r" se deve eseguire una read o "w" una write
	int timeout; //vale 1 se si sveglia dopo un timeout o 0 se si sveglia per un awake
	int awake;
	int bytes; //il numero di bytes letti da una scrittura asincrona 
			   //se il thread nella waitqueue non è in attesa di una scrittura asincrona allora bytes vale -1
	struct _elem *next;
} elem;

wait_queue_head_t the_queues[MINORS][2];
elem head[MINORS][2];
elem *list[MINORS][2];
spinlock_t queue_lock[MINORS][2]; 

//struttura per gestire il work deferred
typedef struct _packed_work{
	void *buffer;
	int pid; //il pid del thread che ha chiamato la scrittura asincrona	
	int minor;
	char *buff; 
	size_t len;
	struct work_struct the_work;
} packed_work;


#define AUDIT if(1)

#define FUNCTION "SLEEP/WAKEUP QUEUE"

char *read_mod = "read_mod";
char *write_mod = "write_mod";
long goto_sleep(int minor, int priority, int jiffies, char *mod);
long awake(int minor, int priority, char *mod, int bytes, int pid);
void deferred_write(struct work_struct *data);

static int dev_open(struct inode *, struct file *);
static int dev_release(struct inode *, struct file *);
static ssize_t dev_write(struct file *, const char *, size_t, loff_t *);
static ssize_t dev_read(struct file *, char *, size_t , loff_t *);
static long dev_ioctl(struct file *, unsigned, unsigned long);
