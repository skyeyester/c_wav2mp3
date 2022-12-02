#include <dirent.h>
#include <errno.h>
#include <lame.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_PATH_SIZE 255
#define MAX_BUF_SIZE 2048

struct node {
  char absolutePath[MAX_PATH_SIZE];
  struct node *next;
};

struct node *front;
struct node *rear;
pthread_mutex_t dequeueMutex;
pthread_mutex_t thrdCoordMut;

/* This is the queueing infra used to add all the filenames in the queue,
 * to be picked up by the threads and processed parallely.
 */
int enqueue(char *pathName) {
  int ret = -1;
  struct node *newNode = NULL;

  if (pathName == NULL) {
    printf("Invalid Parameter. Pathname is NULL\n");
    goto terminated;
  }

  newNode = (struct node *)calloc(1, sizeof(struct node));
  if (!newNode) {
    printf("Failed to create newNode.\nError: %s\n", strerror(errno));
    ret = errno;
    goto terminated;
  }

  memset(newNode->absolutePath, '\0', sizeof(newNode->absolutePath));
  strncpy(newNode->absolutePath, pathName, sizeof(newNode->absolutePath));
  newNode->next = NULL;

  /* Not taking any mutex locks here, as the enqueue will happen
   * only in the main thread, and at this point no other threads
   * are spawned.
   */
  if (rear == NULL) {
    front = rear = newNode;
  } else {
    rear->next = newNode;
    rear = newNode;
  }

  ret = 0;
terminated:
  return ret;
}

struct node *dequeue() {
  struct node *dequeuedNode = NULL;

  pthread_mutex_lock(&dequeueMutex);

  if (front == NULL) {
    pthread_mutex_unlock(&dequeueMutex);
    goto terminated;
  }

  dequeuedNode = front;
  front = front->next;

  if (front == NULL) rear = NULL;

  pthread_mutex_unlock(&dequeueMutex);
terminated:
  return dequeuedNode;
}

void printFromThread(const char *fmt, ...) {
  va_list args;
  pthread_mutex_lock(&thrdCoordMut);
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
  pthread_mutex_unlock(&thrdCoordMut);
}

int encodeReadData(int bytesRead, lame_global_flags *lgf, short *wavBuf,
                   unsigned char *mp3Buf, FILE *mp3File, char *wavFilePath) {
  int ret = -1;
  int i = 0, j = 0;
  short *leftBuf = NULL;
  short *rightBuf = NULL;
  int bytesWrote = -1;

  leftBuf = (short *)calloc(bytesRead, sizeof(short));
  rightBuf = (short *)calloc(bytesRead, sizeof(short));
  if (!leftBuf || !rightBuf) {
    printFromThread(
        "Failed to allocate memory for channel buffer.\nError: %s\n",
        strerror(errno));
    ret = errno;
    goto terminated;
  }

  for (i = 0, j = 0; i < bytesRead; i++) {
    leftBuf[i] = wavBuf[j++];
    rightBuf[i] = wavBuf[j++];
  }

  bytesWrote = lame_encode_buffer(lgf, leftBuf, rightBuf, bytesRead, mp3Buf,
                                  MAX_BUF_SIZE);
  if (bytesWrote < 0) {
    printFromThread("Failed to encode buffer for %s.\nError: %s\n", wavFilePath,
                    strerror(errno));
    ret = errno;
    goto terminated;
  }

  ret = fwrite(mp3Buf, bytesWrote, 1, mp3File);
  if (ret < 0) {
    printFromThread("Failed to write to the mp3File for %s.\nError: %s\n",
                    wavFilePath, strerror(errno));
    ret = errno;
    goto terminated;
  }

  ret = 0;
terminated:
  if (leftBuf) free(leftBuf);

  if (rightBuf) free(rightBuf);

  return ret;
}

int encodeWav(char *wavFilePath) {
  int ret = -1;
  lame_global_flags *lgf = NULL;
  char mp3FilePath[MAX_PATH_SIZE];
  FILE *wavFile = NULL, *mp3File = NULL;
  short *wavBuf = NULL;
  unsigned char *mp3Buf = NULL;
  int len = -1;
  int bytesRead = -1;
  int bytesWrote = -1;

  if (wavFilePath == NULL) goto terminated;

  len = strlen(wavFilePath);
  strncpy(mp3FilePath, wavFilePath, sizeof(mp3FilePath));
  strncpy(mp3FilePath + (len - 3), "mp3", 3);

  printFromThread("Encoding %s...\n", wavFilePath);

  lgf = lame_init();
  if (lgf == NULL) {
    printFromThread("Failed to initialize lameflags.\nError: %s\n",
                    strerror(errno));
    ret = errno;
    goto terminated;
  }

  /* Setting the quality as high * 2=high 5 = medium 7=low * */
  lame_set_quality(lgf, 2);

  ret = lame_init_params(lgf);
  if (ret < 0) {
    printFromThread("Failed to initialize lameflag params.\nError: %s\n",
                    strerror(errno));
    ret = errno;
    goto terminated;
  }

  wavFile = fopen(wavFilePath, "rb");
  if (wavFile == NULL) {
    printFromThread("Failed to open wavFilePath: %s.\nError: %s\n", wavFilePath,
                    strerror(errno));
    ret = errno;
    goto terminated;
  }

  mp3File = fopen(mp3FilePath, "wb");
  if (mp3File == NULL) {
    printFromThread("Failed to open mp3FilePath: %s.\nError: %s\n", mp3FilePath,
                    strerror(errno));
    ret = errno;
    goto terminated;
  }

  wavBuf = (short *)calloc(1, sizeof(short) * 2 * MAX_BUF_SIZE);
  mp3Buf = (unsigned char *)calloc(1, sizeof(unsigned char) * MAX_BUF_SIZE);
  if ((wavBuf == NULL) || (mp3Buf == NULL)) {
    printFromThread("Failed to allocate memory for buffers.\nError: %s\n",
                    strerror(errno));
    ret = errno;
    goto terminated;
  }

  do {
    bytesRead = fread(wavBuf, 2 * sizeof(short), MAX_BUF_SIZE, wavFile);
    if (ferror(wavFile)) {
      printFromThread("Failed to read from %s.\nError: %s\n", wavFilePath,
                      strerror(errno));
      ret = errno;
      goto terminated;
    }

    if (bytesRead != 0) {
      ret =
          encodeReadData(bytesRead, lgf, wavBuf, mp3Buf, mp3File, wavFilePath);
      if (ret != 0) goto terminated;
    }

    if (bytesRead != MAX_BUF_SIZE) {
      bytesWrote = lame_encode_flush(lgf, mp3Buf, MAX_BUF_SIZE);

      ret = fwrite(mp3Buf, bytesWrote, 1, mp3File);
      if (ret < 0) {
        printFromThread("Failed to write to the mp3File for %s.\nError: %s\n",
                        wavFilePath, strerror(errno));
        ret = errno;
        goto terminated;
      }
    }
  } while (bytesRead == MAX_BUF_SIZE);

  ret = 0;
terminated:
  if (lgf) lame_close(lgf);

  if (wavFile) fclose(wavFile);

  if (mp3File) fclose(mp3File);

  if (wavBuf) free(wavBuf);

  if (mp3Buf) free(mp3Buf);

  return ret;
}

/* Each thread will read from the queue the name of the file,
 * and then encode it, and write it to the same location. After
 * that it will check it will again dequeue. If the queue is
 * empty, the thread will terminate.
 */
void *process_file() {
  pthread_t thread_id;
  struct node *dequeuedNode = NULL;
  int ret = -1;

  thread_id = pthread_self();

  while ((dequeuedNode = dequeue()) != NULL) {
    ret = encodeWav(dequeuedNode->absolutePath);
    if (ret != 0) {
      printFromThread("Failed to encode %s.\n", dequeuedNode->absolutePath);
    } else
      printFromThread("Successfully encoded %s.\n", dequeuedNode->absolutePath);
    free(dequeuedNode);
    dequeuedNode = NULL;
  }

  return NULL;
}

/* This function will spawn as many threads as there
 * are cores in the system. These threads will dequeue
 * jobs(filenames) and encode them. Once all the enqueued
 * jobs have been processed, the threads will terminate.
 */
int initiate_syncop(int numOfJobs) {
  int ret = 0;
  int cores = -1, numOfThreads = -1;
  int i = 0, j = 0;
  pthread_t *thread_id = NULL;
  int isInitialized = 0;

#ifdef _WIN32
  SYSTEM_INFO sysinfo;
  GetSystemInfo(&sysinfo);
  cores = sysinfo.dwNumberOfProcessors;
#else
  cores = sysconf(_SC_NPROCESSORS_ONLN);
#endif

  if (numOfJobs < cores)
    numOfThreads = numOfJobs;
  else
    numOfThreads = cores;

  printf("Spawning %d threads...\n", numOfThreads);

  ret = pthread_mutex_init(&thrdCoordMut, NULL);
  if (ret != 0) {
    printf("Failed to initialize mutex.\nError: %s\n", strerror(errno));
    ret = errno;
    goto terminated;
  }

  isInitialized = 1;

  thread_id = (pthread_t *)calloc(numOfThreads, sizeof(pthread_t));
  if (thread_id == NULL) {
    printf("Failed to create thread_ids.\nError: %s\n", strerror(errno));
    ret = errno;
    goto terminated;
  }

  for (i = 0; i < numOfThreads; i++) {
    ret = pthread_create(&(thread_id[i]), NULL, process_file, NULL);
    if (ret != 0) {
      printf("Failed to create thread.\nError: %s\n", strerror(errno));
      ret = errno;
      break;
    }
  }

  for (j = 0; j < i; j++) pthread_join(thread_id[j], NULL);

terminated:
  if (thread_id) free(thread_id);

  if (isInitialized) pthread_mutex_destroy(&thrdCoordMut);

  return ret;
}

/* This function will open the given pathname and read its contents.
 * It will filter out the filenames ending with '.wav', and add them
 * to a queue. The syncop will then pick files up from the queue. It
 * returns the number of filenames enqueued.
 */
int readDirAndEnqueue(char *pathName) {
  int ret = -1;
  struct dirent *de = NULL;
  DIR *dir = NULL;
  int len = -1;
  int count = 0;
  char absolutePath[MAX_PATH_SIZE];

  if (pathName == NULL) {
    printf("Invalid Parameter. Pathname is NULL\n");
    goto terminated;
  }

  dir = opendir(pathName);
  if (dir == NULL) {
    printf("Failed to open %s.\nError: %s\n", pathName, strerror(errno));
    ret = errno;
    goto terminated;
  }

  /*
   * convert all WAV-files contained directly in the given folder.
   */
  while ((de = readdir(dir)) != NULL) {
    len = strlen(de->d_name);
    if (len < 5) continue;

    if (!strncmp(de->d_name + (len - 4), ".wav", 4)) {
      memset(absolutePath, '\0', sizeof(absolutePath));
      snprintf(absolutePath, sizeof(absolutePath), "%s%s", pathName,
               de->d_name);
      printf("%s\n", absolutePath);
      ret = enqueue(absolutePath);
      if (ret != 0) {
        printf("Failed to add %s to the processing queue.\nError:%s\n",
               absolutePath, strerror(errno));
      } else
        count++;
    }
  }

  ret = count;
terminated:
  if (dir != NULL) closedir(dir);

  return ret;
}

int main(int argc, char *argv[]) {
  int ret = -1;
  int count = -1;
  int isInitialized = -1;
  char pathName[MAX_PATH_SIZE];
  int len = -1;

  /* check the number of input argument.*/
  if (argc != 2) {
    printf("Usage : wave2mp3 <folder containing the wav files>\n");
    return 0;
  }

  /* check the argument format: pathName always ends with a '/' */
  strncpy(pathName, argv[1], sizeof(pathName));
  len = strlen(pathName);
#ifdef _WIN32
  if (pathName[len - 1] != '\\') {
    strcat(pathName, "\\");
    len++;
  }
#else
  if (pathName[len - 1] != '/') {
    strcat(pathName, "/");
    len++;
  }
#endif

  printf("pathName = %s\n", pathName);

  front = rear = NULL;
  ret = pthread_mutex_init(&dequeueMutex, NULL);
  if (ret != 0) {
    printf("Failed to initialize mutex.\nError: %s\n", strerror(errno));
    ret = errno;
    goto terminated;
  }

  isInitialized = 1;

  /* Read all the wav file names from the given folder and enqueue them */
  count = readDirAndEnqueue(pathName);
  if (count < 0) goto terminated;

  if (count == 0) {
    printf("No wav files in the folder %s.\n", pathName);
    goto terminated;
  }

  /* Initiate syncop to spawn threads and process the folder in the queue */
  ret = initiate_syncop(count);

terminated:
  if (isInitialized) pthread_mutex_destroy(&dequeueMutex);

  return ret;
}
