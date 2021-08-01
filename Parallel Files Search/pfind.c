/* Shir Frenkel hw 4 OS *
 * ID: 318879830
 * ----------------
 * resources:
 * barriers: https://code-vault.net/lesson/18ec1942c2da46840693efe9b520b377
 * check dir permissions with access(): https://www.informit.com/articles/article.aspx?p=23618&seqNum=3
 * */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <stdatomic.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>



typedef struct dir_node dir_node;
typedef struct directory_queue directory_queue;


// ---------------- GLOBAL VARIABLES DECLARATIONS --------------
pthread_barrier_t barrier_start_search;  // threads will starts searching after all searching threads are created
pthread_mutex_t mutex_access_queue;
pthread_cond_t cond_queue_not_empty;


// global variables (defined as atomic to prevent data races)
// more info: https://developers.redhat.com/blog/2016/01/14/toward-a-better-use-of-c11-atomics-part-1#
atomic_int waiting_threads_count = ATOMIC_VAR_INIT(0);
atomic_int threads_with_error_count = ATOMIC_VAR_INIT(0);

// when kill_search_threads = 1, all threads return their count (and finish),
// threads should modify kill_search_threads only with mutex_access_queue lock
int kill_search_threads = 0;
int n_threads;  // number of searching threads to be used for the search

directory_queue *dir_queue;

// ---------------------------- STRUCTS DEFINITIONS ----------------------------

struct dir_node{
    char* path;
    dir_node *next;
};

struct directory_queue{
    dir_node *head;  // the node that was inserted earliest
    dir_node *tail;
};

char* pop_head(directory_queue *queue){
    /* returns head node's path (the one that was inserted earliest) and remove it from the queue
     * @pre: queue is not empty*/
    char *path_of_rm_node;
    dir_node *removed_node;

    removed_node = queue->head;

    queue->head = removed_node->next;
    if(queue->head == NULL)  // queue len was 1 and now it's 0
        queue->tail = NULL;

    path_of_rm_node = removed_node->path;
    free(removed_node);
    return path_of_rm_node;
}

int add_node(directory_queue *queue, char* path){
    /* creates new node and adds it to the tail of the queue
     * returns:
     * -1 on ERROR (and perror proper message)
     * 1 on success when queue was empty before insertion
     * 0 on success when queue was NOT empty before insertion
     * */
    dir_node *new_node;

    new_node = malloc(sizeof(struct dir_node));
    if(new_node == NULL){
        perror("malloc failed");
        return -1;
    }
    new_node->next = NULL;
    new_node->path = malloc(sizeof(char) * PATH_MAX);
    if(new_node->path == NULL){
        perror("malloc failed");
        return -1;
    }
    strcpy(new_node->path, path);

    // add new_node to queue
    pthread_mutex_lock(&mutex_access_queue);
    if(queue->tail == NULL){  // queue is empty
        queue->head = new_node;
        queue->tail = new_node;

        // send cond_queue_not_empty signal to all waiting threads (queue was empty before adding the new node)
        pthread_cond_broadcast(&cond_queue_not_empty);  // using broadcast to avoid lost wakeup

        pthread_mutex_unlock(&mutex_access_queue);
        return 1;
    }
    else{
        queue->tail->next = new_node;
        queue->tail = new_node;
        pthread_mutex_unlock(&mutex_access_queue);
        return 0;
    }

}


// ----------------------------------------------------------

void thread_exit_with_error(){
    // threads call this function if an error occurred
    threads_with_error_count++;
    // check if queue is empty and all other searching threads are already waiting (or finished with error)
    pthread_mutex_lock(&mutex_access_queue);
    if(dir_queue->head == NULL && waiting_threads_count + threads_with_error_count == n_threads){
        kill_search_threads = 1;  // no need for lock, all other threads are waiting anyway
        pthread_cond_broadcast(&cond_queue_not_empty);  // wakeup all searching threads (for them to exit)
    }
    pthread_mutex_unlock(&mutex_access_queue);
}

void* search_routine(void* T){
    /* T : the search term (search for file names that include the search term)
     * returns : files_found_count_ptr (pointer to integer specifying how many files that contains T were found by the thread)
     * if allocating memory for files_found_count_ptr failed, returns NULL (and in that case 0 files were found by this thread)
     * */
    char *search_term, *dir_path;
    char file_in_dir_path[PATH_MAX];
    int *files_found_count_ptr;
    DIR *dirp;
    struct dirent *dp;
    struct stat file_stat;
    int return_val;

    // wait for all searching threads to be created
    pthread_barrier_wait(&barrier_start_search);

    // allocating memory for files_found_count_ptr
    files_found_count_ptr = malloc(sizeof(int));
    if(files_found_count_ptr == NULL){
        perror("malloc failed");
        thread_exit_with_error();
        return NULL;
    }

    search_term = (char*) T;
    *files_found_count_ptr = 0;

    while(1){
        pthread_mutex_lock(&mutex_access_queue);
        // check if queue is empty, if empty, wait for cond_queue_not_empty signal
        while(dir_queue->head == NULL){
            if(kill_search_threads == 1){  // thread should exit
                pthread_mutex_unlock(&mutex_access_queue);
                return files_found_count_ptr;
            }
            // if all other searching threads are already waiting (or finished with error)
            if(waiting_threads_count + threads_with_error_count == n_threads -1){
                kill_search_threads = 1;
                pthread_cond_broadcast(&cond_queue_not_empty);  // wakeup all searching threads (for them to exit)
                pthread_mutex_unlock(&mutex_access_queue);
                return files_found_count_ptr;
            }
            // going to sleep till cond_queue_not_empty signal
            waiting_threads_count++;
            pthread_cond_wait(&cond_queue_not_empty, &mutex_access_queue);
            waiting_threads_count--;
        }
        // dir_queue is not empty
        dir_path = pop_head(dir_queue);

        pthread_mutex_unlock(&mutex_access_queue);


        // ---------------------------- SEARCH IN DIR ----------------------------
        // iterate through each file in the directory obtained from the queue

        if((dirp = opendir(dir_path)) == NULL){
            printf("opendir failed on %s: %d\n", dir_path, errno);
            thread_exit_with_error();
            return files_found_count_ptr;
        }

        // To distinguish end of stream from an error, set errno to zero before calling readdir()
        // and then check the value of errno if NULL is returned.
        errno = 0;
        while((dp = readdir(dirp)) != NULL){
            // check if the file is one of the directories "." or ".."
            if(strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0 )
                continue;

            // set file_in_dir_path
            if(sprintf(file_in_dir_path, "%s/%s", dir_path, dp->d_name) < 0){
                perror("sprintf failed");
                thread_exit_with_error();
                return files_found_count_ptr;
            }

            // get file (file_in_dir_path) information
            if(lstat(file_in_dir_path, &file_stat) != 0){
                perror("lstat failed");
                thread_exit_with_error();
                return files_found_count_ptr;
            }

            // if file_in_dir_path file is a directory
            if(S_ISDIR(file_stat.st_mode)){
                // file_in_dir_path file is a dir
                // check read and execute permissions (check if dir is searchable)
                if(access(file_in_dir_path, R_OK) != 0 || access(file_in_dir_path, X_OK) != 0){
                    printf("Directory %s: Permission denied.\n", file_in_dir_path);
                }
                else{  // dir is searchable
                    // add file_in_dir_path directory to the tail of the shared FIFO queue
                    return_val = add_node(dir_queue, file_in_dir_path);

                    if(return_val < 0){  // error occurred in add_node
                        thread_exit_with_error();
                        return files_found_count_ptr;
                    }
                }
            }
            else{  // file_in_dir_path file is NOT a directory
                // if the file's name (dp->d_name) contains the search term
                if(strstr(dp->d_name, search_term) != NULL){
                    printf("%s\n", file_in_dir_path);
                    (*files_found_count_ptr)++;
                }
            }
            errno = 0;
        }

        if (errno != 0){
            perror("readdir failed");
            thread_exit_with_error();
            return files_found_count_ptr;
        }

        (void) closedir(dirp);

    }
}


int main(int argc, char* argv[]) {
    int th_num;
    int* return_val;
    int files_found_count;
    char *root_dir_path, *search_term;
    pthread_t *th_lst;
    int exit_code;


    // ---------------------------- CHECK INPUTS ----------------------------
    if(argc != 4){
        fprintf(stderr, "Error: invalid number of arguments\n");
        exit(1);
    }

    root_dir_path = argv[1];
    search_term = argv[2];
    n_threads = atoi(argv[3]);

    // check read and execute permissions (check if dir is searchable)
    if(access(root_dir_path, R_OK) != 0 || access(root_dir_path, X_OK) != 0){
        perror("argv[1] is NOT a searchable directory");
        exit(1);
    }


    // ---------------------------- MAIN INIT ----------------------------
    // ----- pthread barrier/mutex/cond init -----
    if(pthread_barrier_init(&barrier_start_search, NULL, n_threads) != 0){
        perror("pthread_barrier_init failed");
        exit(1);
    }
    pthread_mutex_init(&mutex_access_queue, NULL);
    pthread_cond_init(&cond_queue_not_empty, NULL);

    // ----- create a FIFO queue that holds directories -----
    dir_queue = malloc(sizeof(directory_queue));
    if(dir_queue == NULL){
        perror("malloc failed");
        exit(1);
    }
    // add root dir to queue
    if(add_node(dir_queue, root_dir_path) < 0){
        exit(1);
    }


    // ----- create n_threads searching threads -----
    // allocate memory for th_lst
    th_lst = malloc(n_threads * sizeof(pthread_t));
    if(th_lst == NULL){
        perror("malloc failed");
        exit(1);
    }
    for(th_num = 0; th_num < n_threads; th_num++){

        if(pthread_create(&(th_lst[th_num]), NULL, &search_routine, search_term) != 0) {
            perror("pthread_create failed");
            exit(1);
        }
    }

    // when all threads have been created, pthread_barrier_wait reach it's threshold and then all threads will start searching


    // ----- join threads and update files_found_count accordingly -----
    files_found_count = 0;
    for(th_num = 0; th_num < n_threads; th_num++){
        if(pthread_join(th_lst[th_num], (void**) &return_val) != 0){
            perror("pthread_create failed");
            printf("Done searching, found %d files\n", files_found_count);
            exit(1);
        }
        if(return_val != NULL){
            files_found_count += *return_val;
        }
    }

    printf("Done searching, found %d files\n", files_found_count);



    // ---------------------------- MAIN DESTROY ----------------------------
    // ----- pthread barrier/mutex/cond destroy -----
    if(pthread_barrier_destroy(&barrier_start_search) != 0){
        perror("pthread_barrier_destroy failed");
        exit(1);
    }
    pthread_mutex_destroy(&mutex_access_queue);
    pthread_cond_destroy(&cond_queue_not_empty);


    // ---------------------------- EXITING PROGRAM ----------------------------
    // the exit code should be 0 if and only if no thread (searching or main) has
    // encountered an error. Otherwise, the exit code should be 1.
    exit_code = threads_with_error_count > 0 ? 1 : 0;
    exit(exit_code);
}
