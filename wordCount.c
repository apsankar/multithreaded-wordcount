#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>

//these macros give the size of the pools (shared buffers)
#define MAPPER_MAX_ITEMS 3
#define REDUCER_MAX_ITEMS 3
#define SUM_MAX_ITEMS 4

pthread_mutex_t mapper_pool_mutex;
pthread_mutex_t reducer_pool_mutex;
pthread_mutex_t sum_pool_mutex;
pthread_mutex_t mapper_exit_mutex;
pthread_mutex_t reducer_exit_mutex;
pthread_cond_t mapper_pool_full;
pthread_cond_t mapper_pool_empty;
pthread_cond_t reducer_pool_full;
pthread_cond_t reducer_pool_empty;
pthread_cond_t sum_pool_full;
pthread_cond_t sum_pool_empty;

typedef struct node { //singly linked list
	char *word;
	struct node *next;
} Node;

typedef struct reducer_node {
	char *word;
	int count;
	struct reducer_node *next;
} Rnode;

int shared_flag;
int mappers_flag;
int reducers_flag;
int sum_flag;
Node *mapper_pool_buffer[MAPPER_MAX_ITEMS] = {NULL};
Node *reducer_pool_buffer[REDUCER_MAX_ITEMS] = {NULL};
Rnode *sum_pool_buffer[SUM_MAX_ITEMS] = {NULL};
int mapper_pool_in=0,mapper_pool_out=0;
int reducer_pool_in=0,reducer_pool_out=0;
int sum_pool_in=0,sum_pool_out=0;

void *MPU(void *inputfile) {
	FILE *fp;
	char *filename, *token, *saveptr;
	char word_buffer[100];
	filename = strdup((char*)inputfile); 
	fp = fopen(filename,"r");
	if (!fp) {
		perror(filename);
		exit(EXIT_FAILURE);
	}
	Node *root = NULL;
	while(fgets(word_buffer,sizeof(word_buffer),fp) != NULL) {
		int flag=0;
		Node *head = root;
		token = strtok_r(word_buffer,"\n", &saveptr);
		if (root == NULL) {
			root = malloc(sizeof(Node));
			root->word = strdup(token);
			root->next = NULL;
		}
		else if(strncmp(head->word,token,1) == 0) {
			while (head->next != NULL) 
				head = head->next;
			Node *temp = malloc(sizeof(Node));
			temp->word = strdup(token);
			temp->next = NULL;
			head->next = temp;
		}
		else {
			flag = 1;
			pthread_mutex_lock(&mapper_pool_mutex);
			while (mapper_pool_in - mapper_pool_out == MAPPER_MAX_ITEMS)
				pthread_cond_wait(&mapper_pool_full, &mapper_pool_mutex);
			mapper_pool_buffer[mapper_pool_in % MAPPER_MAX_ITEMS] = root;
			root = NULL;
			mapper_pool_in = mapper_pool_in+1;
			//mapper_pool_num++;
			if (mapper_pool_in - mapper_pool_out == 1)
				pthread_cond_broadcast(&mapper_pool_empty);
			pthread_mutex_unlock(&mapper_pool_mutex);
			}
			if (flag == 1) { 
				root = malloc(sizeof(Node));
				root->word = strdup(token);
				root->next = NULL;
			}
		}
	pthread_mutex_lock(&mapper_pool_mutex);
	while (mapper_pool_in - mapper_pool_out == MAPPER_MAX_ITEMS)
		pthread_cond_wait(&mapper_pool_full, &mapper_pool_mutex);
	mapper_pool_buffer[mapper_pool_in % MAPPER_MAX_ITEMS] = root;
	root = NULL;
	mapper_pool_in = mapper_pool_in+1;
	if (mapper_pool_in - mapper_pool_out == 1)
		pthread_cond_broadcast(&mapper_pool_empty);
	shared_flag = 1;
	pthread_mutex_unlock(&mapper_pool_mutex);
	pthread_exit(0);
	}

void *mapper(void *ptr) {
	while (1) {
	Node *old_root = NULL;
	Node *new_root = NULL;
	char tmp_buffer[103];
	pthread_mutex_lock(&mapper_pool_mutex);
	while (mapper_pool_in == mapper_pool_out) {
		if (shared_flag == 1) {
			pthread_mutex_lock(&mapper_exit_mutex);
			mappers_flag--;
			pthread_mutex_unlock(&mapper_exit_mutex);
			pthread_mutex_unlock(&mapper_pool_mutex);
			pthread_cond_broadcast(&reducer_pool_empty);
			pthread_exit(0);
		} 
		pthread_cond_wait(&mapper_pool_empty,&mapper_pool_mutex);
	}
	old_root = mapper_pool_buffer[mapper_pool_out % MAPPER_MAX_ITEMS];
	mapper_pool_out = mapper_pool_out+1;
	if (mapper_pool_in - mapper_pool_out == MAPPER_MAX_ITEMS - 1) //if, not while.
		pthread_cond_broadcast(&mapper_pool_full);
	pthread_mutex_unlock(&mapper_pool_mutex);

	Node *temp_free, *new_head;
	while (old_root != NULL) {
		new_head = new_root; //necessary for later traversal
		sprintf(tmp_buffer,"(%s,1)",old_root->word);
		if (new_root == NULL) {
		new_root = malloc(sizeof(Node));
		new_root->word = strdup(tmp_buffer);
		new_root->next = NULL;
		}
		else {
			Node *temp = malloc(sizeof(Node));
			temp->word = strdup(tmp_buffer);
			temp->next = NULL;
			while (new_head->next != NULL)
				new_head = new_head->next;
			new_head->next = temp;
		}
		//free old_head / old_root, really free the entire list in the buffer so that it points to NULL.
		temp_free = old_root;
		old_root = old_root->next;
		free(temp_free);
	}
	pthread_mutex_lock(&reducer_pool_mutex);
	while (reducer_pool_in - reducer_pool_out == REDUCER_MAX_ITEMS)
	 	pthread_cond_wait(&reducer_pool_full,&reducer_pool_mutex);
	reducer_pool_buffer[reducer_pool_in % REDUCER_MAX_ITEMS] = new_root;
	reducer_pool_in = reducer_pool_in + 1;
	if (reducer_pool_in - reducer_pool_out == 1)
		pthread_cond_broadcast(&reducer_pool_empty);
	pthread_mutex_unlock(&reducer_pool_mutex);
		}
	}

void *reducer(void *output) {
	while(1) {
	Node *old_root = NULL;
	Rnode *new_root = NULL;
	char *token1, *saveptr1;
	pthread_mutex_lock(&reducer_pool_mutex);
	while (reducer_pool_in == reducer_pool_out) {
		pthread_mutex_lock(&mapper_exit_mutex);
		if (mappers_flag == 0) {
			pthread_mutex_unlock(&mapper_exit_mutex);
			pthread_mutex_lock(&reducer_exit_mutex);
			reducers_flag--;
			pthread_mutex_unlock(&reducer_exit_mutex);
			pthread_mutex_unlock(&reducer_pool_mutex);
			pthread_cond_broadcast(&sum_pool_empty);
			pthread_exit(0);
		}
		pthread_mutex_unlock(&mapper_exit_mutex);
		pthread_cond_wait(&reducer_pool_empty,&reducer_pool_mutex);
	}
	old_root = reducer_pool_buffer[reducer_pool_out%REDUCER_MAX_ITEMS];
	reducer_pool_out = reducer_pool_out+1;
	if (reducer_pool_in - reducer_pool_out == REDUCER_MAX_ITEMS -1)
		pthread_cond_broadcast(&reducer_pool_full);
	pthread_mutex_unlock(&reducer_pool_mutex);
	//Now work on old_root
	while (old_root != NULL) {
		Rnode *new_head = new_root;
		int local_flag = 0;
		token1 = strtok_r(old_root->word,"(",&saveptr1);
		token1 = strtok_r(token1,",",&saveptr1);
		if (new_head == NULL) {
			new_head = malloc(sizeof(Rnode));
			new_head->word = strdup(token1);
			new_head->count = 1;
			new_head->next = NULL;
			new_root = new_head;
			local_flag = 1;
		}
		else if (strncmp(new_head->word,token1,100) == 0) {
			(new_head->count)++;
			local_flag=1;
		}
		else {
			while (new_head->next != NULL) {
				new_head = new_head->next;
				if (strncmp(new_head->word,token1,100) == 0) {
					(new_head->count)++;
					local_flag=1;
					//break; commented for effic. reasons; no 2nd loop traversal. Uncomment requires change in next if.
				}
			}
		}
		if (local_flag == 0) {
			Rnode *temp = malloc(sizeof(Rnode));
			temp->word = strdup(token1);
			temp->count = 1;
			temp->next = NULL;
			new_head->next = temp;
		}
		Node *temp_free = old_root;
		old_root = old_root->next;
		free(temp_free);					
	}
	pthread_mutex_lock(&sum_pool_mutex);
	while (sum_pool_in - sum_pool_out == SUM_MAX_ITEMS) 
	 	pthread_cond_wait(&sum_pool_full,&sum_pool_mutex);
	sum_pool_buffer[sum_pool_in % SUM_MAX_ITEMS] = new_root;
	sum_pool_in = sum_pool_in + 1;
	if (sum_pool_in - sum_pool_out == 1)
		pthread_cond_broadcast(&sum_pool_empty);
	pthread_mutex_unlock(&sum_pool_mutex);
  }
}

void *WCW(void *output) {
	FILE *fp2;
	char *filename2 = strdup((char*)output); 
	fp2 = fopen(filename2,"w");
	if (!fp2) {
		perror(filename2);
		exit(EXIT_FAILURE);
	}
	while(1) {
	Rnode *old_root;
	Rnode *new_root;
	char *token2, *saveptr2;
	pthread_mutex_lock(&sum_pool_mutex);
	while (sum_pool_in == sum_pool_out) {
		pthread_mutex_lock(&reducer_exit_mutex);
		if (reducers_flag == 0) {
			pthread_mutex_unlock(&reducer_exit_mutex);
			pthread_mutex_unlock(&sum_pool_mutex);
			pthread_exit(0);
		}
		pthread_mutex_unlock(&reducer_exit_mutex);
		pthread_cond_wait(&sum_pool_empty,&sum_pool_mutex);	
	}
	old_root = sum_pool_buffer[sum_pool_out % SUM_MAX_ITEMS];
	sum_pool_out = sum_pool_out + 1;
	if (sum_pool_in - sum_pool_out == SUM_MAX_ITEMS -1)
		pthread_cond_broadcast(&sum_pool_full);
	pthread_mutex_unlock(&sum_pool_mutex);

	while (old_root != NULL) {
		fprintf(fp2,"(%s,%d)\n",old_root->word,old_root->count);
		Rnode *temp_free = old_root;
		old_root = old_root->next;
		free(temp_free);
		}
	}
 }

int main(int argc, char *argv[]) {
pthread_mutex_init(&mapper_pool_mutex,NULL);
pthread_cond_init(&mapper_pool_full,NULL);
pthread_cond_init(&mapper_pool_empty,NULL);
pthread_mutex_init(&reducer_pool_mutex,NULL);
pthread_cond_init(&reducer_pool_full,NULL);
pthread_cond_init(&reducer_pool_empty,NULL);
pthread_mutex_init(&sum_pool_mutex,NULL);
pthread_cond_init(&sum_pool_full,NULL);
pthread_cond_init(&sum_pool_empty,NULL);
pthread_mutex_init(&mapper_exit_mutex,NULL);
pthread_mutex_init(&reducer_exit_mutex,NULL);

shared_flag = 0;

if (argc != 4) {
	fprintf(stderr,"Usage: %s <input file> <num of Mappers> <num of Reducers>\n", argv[0]);
	exit(EXIT_FAILURE);
	}
if (atoi(argv[2]) == 0 || atoi(argv[3]) == 0) {
	fprintf(stderr,"0 threads mentioned in args!\n");
	exit(EXIT_FAILURE);
}
int numMappers = atoi(argv[2]);
mappers_flag = numMappers;
int numReducers = atoi(argv[3]);
reducers_flag = numReducers;

pthread_t MPU_thread;
pthread_t mapper_threads[numMappers];
pthread_t reducer_threads[numReducers];
pthread_t WCW_thread;
int rc, i;
rc = pthread_create(&MPU_thread,NULL,MPU,(void*)argv[1]);
if (rc) {
	printf("ERROR; return code from trying to create MPU thread is %d\n",rc);
	exit(EXIT_FAILURE);
	}
for (i=0; i<numMappers; i++) {
	rc = pthread_create(&mapper_threads[i],NULL,mapper,"NULL");
	if (rc) {
		printf("ERROR; return code from trying to create Mapper %d is %d\n",i,rc);
		exit(EXIT_FAILURE);
		}
	}
for (i=0; i<numReducers; i++) {
	rc = pthread_create(&reducer_threads[i],NULL,reducer,"NULL");
	if (rc) {
		printf("ERROR; return code from trying to create Reducer %d is %d\n",i,rc);
		exit(EXIT_FAILURE);
		}
	}
	rc = pthread_create(&WCW_thread,NULL,WCW,"wordCount.txt");
	if (rc) {
		printf("ERROR; return code from trying to create Summarizer %d is %d\n",i,rc);
		exit(EXIT_FAILURE);
	}
pthread_exit(NULL);
}
