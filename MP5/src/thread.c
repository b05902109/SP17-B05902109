#include <stdio.h>
#include <stdlib.h>
#include "thread.h"
void init_thread(thread** t, void* (*func)(void* arg), void* arg)
{
    *t = (thread*)malloc(sizeof(thread));
    (*t)->thread_func = func;
    (*t)->arg = arg;
    if (pthread_create(&((*t)->tid), NULL, func, arg)) {
        fprintf(stderr, "pthread_create fail\n");
    }
    else
    {
        //fprintf(stderr, "pthread_create suc\n");
    }
}

void destroy_thread(thread** t)
{
    free(*t);
}

void init_task_thread(task_thread** t)
{
    *t = (task_thread*)malloc(sizeof(task_thread));

    /* Init the mutex */
    if (pthread_mutex_init(&((*t)->control_lock), NULL)) {
        fprintf(stderr, "pthread_create fail");
        return;
    }
    pthread_mutex_unlock(&((*t)->control_lock));

    thread *temp = (*t)->thread_instance;
    (*t)->busy = 1;
    init_thread(&temp, task_thread_func, *t);
 //   fprintf(stderr, "busy = %p\n", &((*t)->busy));
}

void destroy_task_thread(task_thread** t)
{
    destroy_thread(&((*t)->thread_instance));
    pthread_mutex_destroy(&((*t)->control_lock));
    free(*t);
}

/* launch thread by release controll lock */
void launch(task_thread* t, task_thread_arg* arg)
{
    t->arg = *arg;
    pthread_mutex_unlock(&(t->control_lock));
}
/* real thread run function of task_thread */
void* task_thread_func(void* arg)
{
    task_thread *task = (task_thread*)arg;
    pthread_mutex_lock(&(task->control_lock));
    for(;;) {
        task->busy = 0;
        pthread_mutex_lock(&(task->control_lock));
        //fprintf(stderr, "thread start\n");
        task->arg.func(task->arg.input, task->arg.output);
        //fprintf(stderr, "thread end\n");
    }
    pthread_mutex_unlock(&(task->control_lock));
    pthread_exit(NULL);
    return(NULL);
}

void init_thread_pool(thread_pool** pool, int num)
{
    int i = 0;
    task_thread **temp = NULL;

    *pool = (thread_pool*)malloc(sizeof(thread_pool));
    (*pool)->thread_num = num;

    /* Init the mutex */
    if (pthread_mutex_init(&((*pool)->dispatch_lock), NULL)) {
      //  fprintf(stderr, "pthread_create fail");
        return;
    }

    /*Init the task_threads*/
    (*pool)->threads = (task_thread**)malloc(sizeof(task_thread*) * num);
    temp = (*pool)->threads;
    for(i = 0; i < num; i++)
    {
        init_task_thread(&temp[i]);
    }
}

/* dispatch a task to a thread */
int run_task(thread_pool* pool, task_thread_arg* arg)
{
    int i, suc = 1;
    task_thread **temp = pool->threads;
    pthread_mutex_lock(&(pool->dispatch_lock));
    for(i = 0; i < pool->thread_num; i++)
    {
        //fprintf(stderr, "%d'busy = %d\n", i, temp[i]->busy);

        if(temp[i]->busy == 0)
        {
            temp[i]->busy = 1;
            //fprintf(stderr, "the No.%d selected\n", i);
            launch(temp[i], arg);
            break;
        }
    }

    fprintf(stderr, "\n");
    if(i == pool->thread_num){
        suc = 0;
    }
    
    pthread_mutex_unlock(&(pool->dispatch_lock));
    return suc;
}

int is_finished(thread_pool *pool){
  for(int i = 0; i < pool->thread_num; i++){
    if(pool->threads[i]->busy)
      return 0;
  }
  return 1;
}
void destroy_thread_pool(thread_pool** pool)
{
    int i = 0;
    task_thread **temp = (*pool)->threads;
    for(i = 0; i < (*pool)->thread_num; i++)
    {
        destroy_task_thread(&temp[i]);
    }
    free(temp);
    free(*pool);
}
