#include"basic.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Silvia di Luise");


static struct file_operations fops = {
   .owner = THIS_MODULE,
   .open =  dev_open,
   .release = dev_release,
   .unlocked_ioctl = dev_ioctl,
   .write = dev_write,
   .read = dev_read
};


static int dev_open(struct inode *inode, struct file *file) {

   int minor;
   minor = get_minor(file);
   printk(KERN_INFO "%s: try to open minor: %d\n", MODNAME, minor);

   //controllo del minor inserito
   if ((minor >= MINORS) || (minor < 0)) {
      printk(KERN_ERR "%s: device file NOT opened for object with minor %d (wrong minor)\n", MODNAME, minor);
      return -ENODEV;
   }
   //controllo sullo stato del device file
   if (state_devices[minor] == OFF) {
      printk(KERN_ERR "%s: device file NOT opened for object with minor %d (device file disable)\n", MODNAME, minor);
      return -EACCES;
   }

   printk("%s: device file successfully opened for object with minor %d\n", MODNAME, minor);
   
   return 0;

}


static int dev_release(struct inode *inode, struct file *file) {

   int minor;
   minor = get_minor(file);
   printk("%s: device file with minor %d closed\n", MODNAME, minor);
   return 0;

}

static long dev_ioctl(struct file *filp, unsigned command, unsigned long param) {

   int minor = get_minor(filp);
   object_state *the_object;

   the_object = objects + minor;
   printk(KERN_INFO "%s: someone called iocl command %u\n", MODNAME, command);

   switch (command) {
      //comando per settare la priorità del flusso
      case IOCTL_SET_PRIORITY:
         if (param == HIGH_PRIORITY) {
            the_object->priority = HIGH_PRIORITY;
         }
         else if (param == LOW_PRIORITY) {
            the_object->priority = LOW_PRIORITY;
         }
         else {
            printk(KERN_ERR "%s: [ioctl] invalid param for IOCTL_SET_PRIORITY\n", MODNAME);
            return -EINVAL;
         }
         break;
      //comando per settare la modalità di scrittura
      case IOCTL_SET_BLOCKING_WRITE:
         if (param == ON) {
            the_object->blocking_write = ON;
         }
         else if (param == OFF) {
            the_object->blocking_write = OFF;
         }
         else {
            printk(KERN_ERR "%s: [ioctl] invalid param for IOCTL_SET_BLOCKING_WRITE\n", MODNAME);
            return -EINVAL;
         }
         break;
      //comando per settare la modalità di lettura
      case IOCTL_SET_BLOCKING_READ:
         if (param == ON) {
            the_object->blocking_read = ON;
         }
         else if (param == OFF) {
            the_object->blocking_read = OFF;
         }
         else {
            printk(KERN_ERR "%s: [ioctl] invalid param for IOCTL_SET_BLOCKING_READ\n", MODNAME);
            return -EINVAL;
         }
         break;
      //comando per attivare il timeout
      case IOCTL_SET_TIMEOUT_ON:
         the_object->timeout = ON;
         printk("il timeout vale %d\n", the_object->timeout);
         if (param >= 0) {
            the_object->jiffies = param;
         }
         //se si è passato un valore negativo si attiva il timeout con un valore di default
         else {
            the_object->jiffies = 500;
         }
         break;
      //comando per disattivare il timeout
      case IOCTL_SET_TIMEOUT_OFF:
         the_object->timeout = OFF;
         break;  
      //comando per abilitare un device file
      case IOCTL_ENABLE_DEVICE: 
         //controllo del minor passato
         if ((param >= 0) && (param < MINORS)) {
            state_devices[param] = ON;
         }
         else {
            printk(KERN_ERR "%s: [ioctl] invalid param for IOCTL_ENABLE_DEVICE\n", MODNAME);
            return -EINVAL;
         }
         break;
      //comando per disabilitare un device file
      case IOCTL_DISABLE_DEVICE:
         //controllo del minor passato
         if ((param >= 0) && (param < MINORS)) {
               state_devices[param] = OFF;
         }
         else {
            printk(KERN_ERR "%s: [ioctl] invalid param for IOCTL_DISABLE_DEVICE\n", MODNAME);
            return -EINVAL;
         }
         break;
      default:
         printk(KERN_ERR "%s: [ioctl] invalid command code %u\n", MODNAME, command);
         return -ENOTTY;
   }

   return 0;

}

static ssize_t dev_write(struct file *filp, const char *buff, size_t len, loff_t *off) {

   int minor = get_minor(filp);
   int priority;
   int firstTry = 1; //è uguale a 1 durante il primo tentativo di accedere alla risorsa
   object_state *the_object;
   the_object = objects + minor;
   printk("%s: somebody called a write on dev with [major,minor] number [%d,%d] and priority %d\n", MODNAME, get_major(filp), get_minor(filp), the_object->priority);

   //prende i mutex, essendo il primo tentativo è meno prioritario rispetto a un thread che è già passato nella waitqueue
   mutex_lock(&(the_object->first_access[the_object->priority]));
   mutex_lock(&(the_object->next_to_access[the_object->priority]));
   mutex_lock(&(the_object->data_access[the_object->priority]));
   mutex_unlock(&(the_object->next_to_access[the_object->priority]));

   priority = the_object->priority;
   //le scritture vengono eseguite in append
   *off = the_object->valid_bytes[priority];

   //se la scrittura è bloccante deve andare nella waitqueue
   //finché non c'è abbastanza spazio nel buffer
   if (the_object->blocking_write) {
      //se la scrittura è più grande del buffer stesso non sarà mai possibile eseguirla
      if (len > OBJECT_MAX_SIZE) {
         printk(KERN_ERR "%s: len too large for a blocking operation %d\n", MODNAME, minor);
         mutex_unlock(&(the_object->data_access[the_object->priority]));
         mutex_unlock(&(the_object->first_access[the_object->priority]));
         return -EINVAL;
      }

      //se il thread deve andare per la prima volta nella waitqueue rilascia il mutex 
      //dedicato al primo accesso e aggiorna la variabile firstTry
      if ((OBJECT_MAX_SIZE - (*off + the_object->reserved_bytes[priority])) < len) {
         mutex_unlock(&(the_object->first_access[the_object->priority]));
         firstTry = 0;

      }

      while ((OBJECT_MAX_SIZE - (*off + the_object->reserved_bytes[priority])) < len) {
         //rilascia il mutex e si sposta nella waitqueue
         mutex_unlock(&(the_object->data_access[the_object->priority]));
         if (the_object->timeout == ON) {
            goto_sleep(minor, priority, the_object->jiffies, write_mod, len);
         } else {
            goto_sleep(minor, priority, 0, write_mod, len);
         }
         mutex_lock(&(the_object->next_to_access[the_object->priority]));
         mutex_lock(&(the_object->data_access[the_object->priority]));
         mutex_unlock(&(the_object->next_to_access[the_object->priority]));
         *off = the_object->valid_bytes[priority];

      }
   }
   else {
      //se la scrittura non è bloccante len è imposto al massimo di byte che posso scrivere
      if ((OBJECT_MAX_SIZE - (*off + the_object->reserved_bytes[priority])) < len) len = OBJECT_MAX_SIZE - (*off + the_object->reserved_bytes[priority]);
   }

   
   //se il flusso su cui scrivere è quello a priorità alta si esegue la scrittura sincrona
   if (priority == HIGH_PRIORITY) {
      printk("synchronous write operation with priority %d --> %s\n", priority, buff);
      copy_from_user(&(the_object->stream_content[priority][*off]), buff, len);
      *off += len;
      the_object->valid_bytes[priority] = *off;
      bytes_high_priority[minor] = *off; 

      //controlla se ci sono dei thread nella waitqueue che devono leggere o scrivere e in caso ne sveglia uno
      if ((waiting_threads_high_priority[minor] > 0)) {
         awake(minor, priority, the_object->valid_bytes[HIGH_PRIORITY]);
      }

   } else {
      the_object->reserved_bytes[priority] += len;
   }

   //rilascio dei mutex
   mutex_unlock(&(the_object->data_access[the_object->priority]));
   if (firstTry == 1) {
      mutex_unlock(&(the_object->first_access[the_object->priority]));
   }

   //se il flusso su cui scrivere è quello a priorità bassa si esegue la scrittura asincrona
   if (priority == LOW_PRIORITY) {

      //allocazione del task
      packed_work *the_task;
      the_task = kzalloc(sizeof(packed_work), GFP_ATOMIC);
      if (the_task == NULL) {
         printk(KERN_ERR "%s: tasklet buffer allocation failure\n", MODNAME);
         module_put(THIS_MODULE);
         return -1;
      }

      the_task->buff = kzalloc(sizeof(char)*len, GFP_ATOMIC);
      if (the_task->buff == NULL) {
         printk(KERN_ERR "%s: buffer allocation failure\n", MODNAME);
         module_put(THIS_MODULE);
         return -1;
      }
      
      //copia della scrittura sul buffer
      copy_from_user(&(the_task->buff[0]), buff, len);
      
      the_task->buffer = the_task;
      the_task->len = len;
      the_task->minor = minor;

      printk("%s: work buffer allocation success - address is %p\n", MODNAME, the_task);

      INIT_WORK(&(the_task->the_work), (void*)deferred_write);
      schedule_work(&the_task->the_work);

   }


   return len;
}

static ssize_t dev_read(struct file *filp, char *buff, size_t len, loff_t *off) {

   int minor = get_minor(filp);
   int ret, priority; //è uguale a 1 durante il primo tentativo di accedere alla risorsa
   int firstTry = 1;
   char *copy = NULL;
   object_state *the_object;

   the_object = objects + minor;

   printk("%s: somebody called a read on dev with [major,minor] number [%d,%d] and priority %d\n", MODNAME, get_major(filp), get_minor(filp), the_object->priority);

   //le letture sono effettuate sempre dall'inizio del file device
   *off = 0;
   
   //prende i mutex, essendo il primo tentativo è meno prioritario rispetto a un thread che è già passato nella waitqueue
   mutex_lock(&(the_object->first_access[the_object->priority]));
   mutex_lock(&(the_object->next_to_access[the_object->priority]));
   mutex_lock(&(the_object->data_access[the_object->priority]));

   mutex_unlock(&(the_object->next_to_access[the_object->priority]));   priority = the_object->priority;
   
   //se la lettura è bloccante deve andare nella waitqueue
   //finché non ci sono abbastanza bytes nel buffer
   if (the_object->blocking_read) {

      //se la lettura è più grande del buffer stesso non sarà mai possibile eseguirla
      if (len > OBJECT_MAX_SIZE) {
         printk(KERN_ERR "%s: len too large for a blocking operation %d\n", MODNAME, minor);
         mutex_unlock(&(the_object->data_access[the_object->priority]));
         mutex_unlock(&(the_object->first_access[the_object->priority]));
         return -EINVAL;
      }

      //se il thread deve andare per la prima volta nella waitqueue rilascia il mutex 
      //dedicato al primo accesso e aggiorna la variabile firstTry
      if (the_object->valid_bytes[the_object->priority] < len) {
         mutex_unlock(&(the_object->first_access[the_object->priority]));
         firstTry = 0;

      }
      while(the_object->valid_bytes[the_object->priority] < len) {
         //rilascia il mutex e va nella waitqueue
         mutex_unlock(&(the_object->data_access[the_object->priority]));
         if (the_object->timeout == ON) {
            goto_sleep(minor, the_object->priority, the_object->jiffies, read_mod, len);
         } else {
            goto_sleep(minor, the_object->priority, 0, read_mod, len);
         }
         mutex_lock(&(the_object->next_to_access[the_object->priority]));
         mutex_lock(&(the_object->data_access[the_object->priority]));
         mutex_unlock(&(the_object->next_to_access[the_object->priority]));
      }
   }
   else {
         //se la lettura non è bloccante len è imposto al massimo di byte che posso leggere
         if (the_object->valid_bytes[the_object->priority] < len) len = the_object->valid_bytes[the_object->priority];
   }

   //printk("read operation with priority %d --> %s\n", the_object->priority, buff);
   ret = copy_to_user(buff, & (the_object->stream_content[the_object->priority][0]), len);
   
   //cancella i bytes letti
   copy = (char*)__get_free_page(GFP_KERNEL);
   if (copy == NULL) { 
      free_page((unsigned long)copy);
      return -ENOMEM;
   }
   //la strcpy è utilizza con due buffer entrambi nello spazio kernel
   strcpy(copy, the_object->stream_content[the_object->priority]+len-ret);
   strcpy(the_object->stream_content[the_object->priority], copy);
   free_page((unsigned long)copy);

   //update dei bytes scritti nel buffer e dei module param
   the_object->valid_bytes[the_object->priority] = the_object->valid_bytes[the_object->priority] - (len-ret);
   if (the_object->priority == 0) bytes_high_priority[minor] = the_object->valid_bytes[the_object->priority];
   else bytes_low_priority[minor] = the_object->valid_bytes[the_object->priority];

   
   //controlla se ci sono dei thread nella waitqueue che devono scrivere e in caso ne sveglia uno
   if (((priority == HIGH_PRIORITY) && (waiting_threads_high_priority[minor] > 0)) || ((priority == LOW_PRIORITY) && (waiting_threads_low_priority[minor] > 0))) {
      awake(minor, priority, the_object->valid_bytes[priority]);
   }

   //rilascio del mutex
   mutex_unlock(&(the_object->data_access[the_object->priority]));
   if (firstTry == 1) {
      mutex_unlock(&(the_object->first_access[the_object->priority]));
   }


   return len - ret;

}


long goto_sleep(int minor, int priority, int jiffies, char *mod, int bytes) {

   volatile elem me;
   elem *aux;

   me.next = NULL;
   me.task = current;
   me.bytes = bytes;
   me.pid  = current->pid;
   me.awake = OFF;

   //i parametri mod e timeout servono successivamente per svegliare il thread corretto
   me.mod = mod;
   if (jiffies == 0) {
      me.timeout = OFF;
   } else {
      me.timeout = ON;
   }

   AUDIT
   printk("%s: sys_goto_sleep on strong fifo sleep/wakeup queue called from thread %d\n",FUNCTION,current->pid);
   //prende il lock relativo alla sua waitqueue 
   spin_lock(& (queue_lock[minor][priority]));

   aux = list[minor][priority];
   if (aux == NULL){
      spin_unlock(& (queue_lock[minor][priority]));
      preempt_enable();
      printk("%s: malformed sleep-wakeup-queue - service damaged\n",FUNCTION);
      return -1;
   }

   //si accoda alla sua waitqueue
   for (; aux; ) {
      if(aux->next == NULL) {
         aux->next = (elem*) &me;        
         goto sleep;
      }
      aux = aux->next;
   }  

sleep:

   //rilascia il lock
   spin_unlock(& (queue_lock[minor][priority]));

   //aumenta i module param se il thread è in modalità bloccante
   if (priority == HIGH_PRIORITY) atomic_inc((atomic_t*)&waiting_threads_high_priority[minor]);
   else atomic_inc((atomic_t*)&waiting_threads_low_priority[minor]);
   
   AUDIT
   printk("%s: thread %d actually going to sleep\n", FUNCTION, current->pid);

   if (jiffies == 0) {  
      wait_event_interruptible(the_queues[minor][priority], me.awake == ON);
   }
   else {
      wait_event_interruptible_timeout(the_queues[minor][priority], false, jiffies);
   }

   //una volta svegliato riprende il lock
   spin_lock(& (queue_lock[minor][priority]));

   aux = list[minor][priority];
   if (aux == NULL) {
      spin_unlock(& (queue_lock[minor][priority]));
      preempt_enable();
      printk("%s: malformed sleep-wakeup-queue upon wakeup - service damaged\n",FUNCTION);
      return -1;
   }

   //rimuove se stesso dalla waitqueue
   for (; aux; ) {
      if (aux->next != NULL) {
         if (aux->next->pid == current->pid) {
            aux->next = me.next;       
            break;
         }
      }
      aux = aux->next;
   }  

   //rilascia il lock
   spin_unlock(& (queue_lock[minor][priority]));

   AUDIT
   printk("%s: thread %d exiting sleep for a wakeup or signal\n", FUNCTION, current->pid);

   //decrementa i module param
   if (priority == HIGH_PRIORITY) atomic_dec((atomic_t*)&waiting_threads_high_priority[minor]);//a new sleeper 
   else atomic_dec((atomic_t*)&waiting_threads_low_priority[minor]);
   
   if(me.awake == OFF){
      AUDIT
      printk("%s: thread %d exiting sleep for signal\n",FUNCTION, current->pid);
      return -EINTR;
   }

   return 0;

}

long awake(int minor, int priority, int bytes) {
   
   struct task_struct *the_task;
   int its_pid = -1;
   elem *aux;


   printk("%s: sys_awake called from thread %d\n", FUNCTION, current->pid);
   
   aux = list[minor][priority];
   
   //prende il lock relativo alla sua waitqueue 
   spin_lock(& (queue_lock[minor][priority]));

   if (aux == NULL) {
      spin_unlock(& (queue_lock[minor][priority]));
      printk("%s: malformed sleep-wakeup-queue\n",FUNCTION);
      return -1;
   }

   //sveglia il primo della lista che non sta aspettando un timeout e per cui ci sono abbastanza byte (da leggere o da scrivere) sul device file
   for(; aux; ) {
      if(aux->next) {
         if ((aux->next->timeout == OFF) && (((aux->next->mod == read_mod) && (aux->next->bytes <= bytes)) || ((aux->next->mod == write_mod) && (aux->next->bytes <= OBJECT_MAX_SIZE-bytes)))) {
            the_task = aux->next->task;
            aux->next->awake = ON;
            its_pid = aux->next->pid;
            wake_up_process(the_task);
            break;
         }
        
      }
   aux = aux->next;
   }  
   
   //rilascia il lock
   spin_unlock(& (queue_lock[minor][priority]));

   AUDIT
   printk("%s: called the awake of thread %d\n",FUNCTION, its_pid);

   return 0;

}

//deferred write per le scritture asincrone con bassa priorità
void deferred_write(struct work_struct *data) {

   int minor = container_of(data, packed_work, the_work)->minor;
   object_state *the_object;
   size_t len = container_of(data, packed_work, the_work)->len;

   printk("%s: deferred_write on dev with minor %d\n", MODNAME, minor);

   the_object = objects + minor;

   //prende i mutex
   mutex_lock(&(the_object->first_access[LOW_PRIORITY]));
   mutex_lock(&(the_object->next_to_access[LOW_PRIORITY]));
   mutex_lock(&(the_object->data_access[LOW_PRIORITY]));

   mutex_unlock(&(the_object->next_to_access[LOW_PRIORITY]));
   
   printk("asynchronous write operation with priority %d\n", LOW_PRIORITY);
   
   //la strcpy è utilizza con due buffer entrambi nello spazio kernel
   strncpy(& (the_object->stream_content[LOW_PRIORITY][the_object->valid_bytes[LOW_PRIORITY]]), container_of(data, packed_work, the_work)->buff, len);
   the_object->valid_bytes[LOW_PRIORITY] += len;   
   bytes_low_priority[minor] += len; 
   the_object->reserved_bytes[LOW_PRIORITY] -= len;
   
   //controlla se ci sono dei thread nella waitqueue che devono leggere o scrivere e in caso ne sveglia uno
   if ((waiting_threads_low_priority[minor] > 0)) {
      awake(minor, LOW_PRIORITY, the_object->valid_bytes[LOW_PRIORITY]);
   }

   //rilascio del mutex
   mutex_unlock(&(the_object->data_access[LOW_PRIORITY]));
   mutex_unlock(&(the_object->first_access[LOW_PRIORITY]));

   //libera la memoria utilizzata
   kfree((void*)container_of(data,packed_work,the_work)->buff);   
   kfree((void*)container_of(data,packed_work,the_work));   

}


int init_module(void) {

   int i, j;

   //inizializzazione delle strutture per gestire il device
   for (i = 0; i < MINORS; i++) {

      //module param
      state_devices[i] = ON;
      bytes_high_priority[i] = 0;
      bytes_low_priority[i] = 0;
      waiting_threads_high_priority[i] = 0;
      waiting_threads_low_priority[i] = 0;
     
      for(j = 0; j < 2; j++) {
      
         mutex_init(& (objects[i].data_access[j]));
         mutex_init(& (objects[i].next_to_access[j]));
         mutex_init(& (objects[i].first_access[j]));

         objects[i].valid_bytes[j] = 0;
         objects[i].reserved_bytes[j] = 0;
         objects[i].stream_content[j] = NULL;
         objects[i].stream_content[j] = (char*)__get_free_page(GFP_KERNEL);
         if(objects[i].stream_content[j] == NULL) goto revert_allocation;

         //inizializzazioni delle waitqueue
         init_waitqueue_head(& (the_queues[i][j]));
         head[i][j].task = NULL;
         head[i][j].pid = -1;
         head[i][j].awake = -1;
         head[i][j].next = NULL;
         list[i][j] = & (head[i][j]);
      }

      objects[i].priority = LOW_PRIORITY;
      objects[i].blocking_read = OFF;
      objects[i].blocking_write = OFF;
      objects[i].timeout = OFF;
      objects[i].jiffies = 500;
        
   }

   //registrazione del device
   Major = register_chrdev(0, DEVICE_NAME, &fops);
   if (Major < 0) {
     printk("%s: registering device failed\n", MODNAME);
     return Major;
   }

   printk(KERN_INFO "%s: new device registered, it is assigned major number %d\n", MODNAME, Major);

   return 0;


revert_allocation:
   for (; i >= 0; i--) {
      for (; j >= 0; j--) {
         free_page((unsigned long)objects[i].stream_content[j]);
      }
   }
   return -ENOMEM;
}

void cleanup_module(void) {

   int i, j;

   for (i = 0; i < MINORS; i++) {
      for (j = 0; j < 2; j++) {
         free_page((unsigned long)objects[i].stream_content[j]);
      }
   }

   unregister_chrdev(Major, DEVICE_NAME);
   printk(KERN_INFO "%s: new device unregistered, it was assigned major number %d\n", MODNAME, Major);

   return;

}
