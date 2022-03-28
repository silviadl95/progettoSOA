#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <stdbool.h>

#define SLOT_IOCTL_MAGIC 'x'
#define IOCTL_SET_PRIORITY _IOW(SLOT_IOCTL_MAGIC, 0, unsigned int)
#define IOCTL_SET_BLOCKING_WRITE _IOW(SLOT_IOCTL_MAGIC, 1, unsigned int)
#define IOCTL_SET_BLOCKING_READ _IOW(SLOT_IOCTL_MAGIC, 2, unsigned int)
#define IOCTL_SET_TIMEOUT_ON _IOW(SLOT_IOCTL_MAGIC, 3, unsigned int)
#define IOCTL_SET_TIMEOUT_OFF _IO(SLOT_IOCTL_MAGIC, 4)
#define IOCTL_ENABLE_DEVICE _IOW(SLOT_IOCTL_MAGIC, 5, unsigned int)
#define IOCTL_DISABLE_DEVICE _IOW(SLOT_IOCTL_MAGIC, 6, unsigned int)



void * the_thread_little_writer(void* file) {

	int fd = *((int*) file);
	int ret;
	char msg[4096];
	int sum = 0;
	sprintf(msg, "thread scrittore %ld...", pthread_self());
	int sizeMsg = strlen(msg);
	for (int i = 0; i < 1; i++) {
		ret = write(fd, msg, sizeMsg);
		if (ret < 0) {
			printf("Errore nella write\n");
			return NULL;
		}
		sum = sum + ret;
	}
	printf("thread little-writer %ld ha finito, ha scritto %d bytes\n", pthread_self(), sum);
	return NULL;


}

void * the_thread_little_reader(void* file) {

	int fd = *((int*) file);
	int ret;
	int sum = 0;
	char readText[4096];
	for (int i = 0; i < 1; i++) {
		ret = read(fd , readText, 5);
		if (ret < 0) {
			printf("Errore nella read\n");
			return NULL;
		}
		sum = sum + ret;
		//printf("\nsono il thread lettore %ld ho letto:\n '%s'\n", pthread_self(), readText);
	}

	printf("thread little-reader %ld ha finito, ha letto %d bytes\n", pthread_self(), sum);
	return NULL;

}

void * the_thread_big_writer(void* file) {

	//vuole scrivere 4130 byte
	int fd = *((int*) file);
	char msg[4096];
	int ret;
	int sum = 0;
	sprintf(msg, "thread scrittore %ld...", pthread_self());
	int sizeMsg = strlen(msg);
	for (int i = 0; i < 118; i++) {
		ret = write(fd, msg, sizeMsg);
		if (ret < 0) {
			printf("Erroe nella write\n");
			return NULL;
		}
		sum = sum + ret;
		//printf("---%d---%d\n", i, sum);
	}
	printf("thread big-writer %ld ha finito, ha scritto %d bytes\n", pthread_self(), sum);
	return NULL;


}

void * the_thread_big_reader(void* file) {

	//vuole leggere 4000 byte
	int fd = *((int*) file);
	int sum = 0;
	int ret;
	char readText[4096];
	for (int i = 0; i < 20; i++) {
		ret = read(fd , readText, 200);
		if (ret < 0) {
			printf("Erroe nella read\n");
			return NULL;
		}
		sum = sum + ret;
		//printf("sono il thread lettore %ld ho letto:\n '%s'\n", pthread_self(), readText);
	}

	printf("thread big-reader %ld ha finito, ha letto %d bytes\n", pthread_self(), sum);
	return NULL;

}




int main(int argc, char** argv) {

	int i;
	int ret;
	int major;
	int minors;
	int fd;
	char *path;
	char buff[100];
	int priority, timeout;

	pthread_t tid;

	if (argc < 4) {
		printf("useg: prog pathname major minors\n");
		return -1;
	}

	path = argv[1];
	major = strtol(argv[2], NULL, 10);
	minors = strtol(argv[3], NULL, 10);

	if ((minors > 128) || (minors <= 0)) {
		printf("minors errato\n");
		return EXIT_FAILURE;
	}
	printf("creating %d minors for device %s with major %d\n", minors, path,major);

	for(i = 0; i < minors; i++) {

 		//test NON BLOCCANTI
     	if (i == 2 && true) {

     		sprintf(buff,"mknod %s%d c %d %i\n", path, i, major, i);
			system(buff);
			sprintf(buff, "%s%d", path, i);
			printf("opening device %s\n", buff);
			fd = open(buff, O_RDWR);
			if (fd == -1) {
				printf("open error on device %s\n", buff);
				return EXIT_FAILURE;
			}
			printf("device %s successfully opened\n", buff);

     		for (int priority = 0; priority < 2; priority++) {
				
				printf("\ntest non bloccanti, priortà = %d\n", priority);

				ioctl(fd, IOCTL_SET_PRIORITY, priority);

				pthread_create(&tid, NULL, the_thread_big_writer, (void*) &fd);
				pthread_create(&tid, NULL, the_thread_big_writer, (void*) &fd);
				sleep(2);
				pthread_create(&tid, NULL, the_thread_big_reader, (void*) &fd);
				pthread_create(&tid, NULL, the_thread_big_reader, (void*) &fd);
				pthread_create(&tid, NULL, the_thread_big_reader, (void*) &fd);
				sleep(2);
				pthread_create(&tid, NULL, the_thread_little_writer, (void*) &fd);
				pthread_create(&tid, NULL, the_thread_little_reader, (void*) &fd);
				pthread_create(&tid, NULL, the_thread_little_writer, (void*) &fd);
				pthread_create(&tid, NULL, the_thread_little_reader, (void*) &fd);
				sleep(5);

			}
		}
		//test BLOCCANTI
		if (i == 12 && true) {

			sprintf(buff,"mknod %s%d c %d %i\n", path, i, major, i);
			system(buff);
			sprintf(buff, "%s%d", path, i);
			printf("opening device %s\n", buff);
			fd = open(buff, O_RDWR);
			if (fd == -1) {
				printf("open error on device %s\n", buff);
				return EXIT_FAILURE;
			}
			printf("device %s successfully opened\n", buff);

     		for (priority = 0; priority < 2; priority++) {
     			for (timeout = 0; timeout < 2; timeout++) {

     				printf("\ntest lettura bloccante, priortà:%d, timeout:%d\n", priority, timeout);

					ioctl(fd, IOCTL_SET_PRIORITY, priority);
					ioctl(fd, IOCTL_SET_BLOCKING_READ, 1);
					ioctl(fd, IOCTL_SET_BLOCKING_WRITE, 0);
					if (timeout) {ioctl(fd, IOCTL_SET_TIMEOUT_ON, 400);}
					else {ioctl(fd, IOCTL_SET_TIMEOUT_OFF);}

					pthread_create(&tid, NULL, the_thread_little_reader, (void*) &fd);
					pthread_create(&tid, NULL, the_thread_little_reader, (void*) &fd);
					pthread_create(&tid, NULL, the_thread_little_reader, (void*) &fd);
					pthread_create(&tid, NULL, the_thread_little_reader, (void*) &fd);
					sleep(3);
					pthread_create(&tid, NULL, the_thread_little_writer, (void*) &fd);
					sleep(3);
					
     				printf("\ntest scrittura e lettura bloccante, priortà:%d, timeout:%d\n", priority, timeout);
					ioctl(fd, IOCTL_SET_BLOCKING_WRITE, 1);
					ioctl(fd, IOCTL_SET_BLOCKING_READ, 1);
					pthread_create(&tid, NULL, the_thread_big_writer, (void*) &fd);
					pthread_create(&tid, NULL, the_thread_big_writer, (void*) &fd);
					pthread_create(&tid, NULL, the_thread_little_writer, (void*) &fd);
					sleep(3);
					pthread_create(&tid, NULL, the_thread_big_reader, (void*) &fd);
					sleep(3);
					pthread_create(&tid, NULL, the_thread_big_reader, (void*) &fd);
					sleep(3);

					printf("\n----Thread per riportare i valid bytes a 0\n");
					ioctl(fd, IOCTL_SET_BLOCKING_READ, 0);
					pthread_create(&tid, NULL, the_thread_big_reader, (void*) &fd);
					sleep(3);
					ioctl(fd, IOCTL_SET_BLOCKING_READ, 0);

					
				}
			}
		}

	}
	
	pause();
	return 0;

}
