#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include "../cacti.h"

#define NO_INCREMENTS 1000

int x=1;

void hello(void** stateptr, size_t size, void* data){
    assert(*stateptr == NULL);
    printf("hello, i am %ld, and my father is %ld\n", actor_id_self(), (actor_id_t)data);
    message_t msg = {
            .data = &x,
            .message_type = 1
    };
    for(int i=0;i<NO_INCREMENTS;i++){
    int retval = send_message((actor_id_t)data, msg);
    if (retval != 0 && retval != -1) {
        fprintf(stderr, "could not send fun successfully\n");
        exit(1);
    } else if (retval == -1) {
        printf("already dead\n");
        //printf("fun sent successfully\n");
    }
    }
    //free(data);
    message_t gdmsg = {
            .message_type = MSG_GODIE
    };
    sleep(1);
    printf("thread: %ld, actor: %ld\n", pthread_self(), actor_id_self());
    if (send_message(actor_id_self(), gdmsg) != 0) {
        fprintf(stderr, "could not send gdmsg to self successfully\n");
        exit(1);
    } else {
        printf("gdmsg to self sent successfully\n");
    }
}

void fun(void** stateptr, size_t size, void* data){
    printf("%ld is incrementing data: %d\n", actor_id_self(), ++*(int*)data);
    message_t gdmsg = {
            .message_type = MSG_GODIE
    };
    if(*(int*)data == 1+2*NO_INCREMENTS || *(int*)data == 1+4*NO_INCREMENTS)
    {
        printf("thread: %ld, actor: %ld\n", pthread_self(), actor_id_self());
/*
        int retval = send_message(actor_id_self(), gdmsg);
        if (retval != 0) {
            fprintf(stderr, "could not stop myselft with retval %d\n", retval);
            exit(1);
        } else {
            printf("stopped myself successfully\n");
        }
*/
        //printf("finished\n");
    }
}

int main(){
    const size_t nprompts = 2;
    void (**prompts)(void**, size_t, void*) = malloc(sizeof(void*) * nprompts);
    prompts[0] = &hello;
    prompts[1] = &fun;
    role_t role = {
            .nprompts = nprompts,
            .prompts = prompts
    };

    message_t msgSpawn = {
            .message_type = MSG_SPAWN,
            .data = &role
    };

    message_t msgGoDie = {
            .message_type = MSG_GODIE
    };


    actor_id_t actorId;
    if (actor_system_create(&actorId, &role) != 0) {
        fprintf(stderr, "not success creation\n");
        exit(1);
    } else {
        printf("system successfully created\n");
    }

    if (send_message(actorId, msgSpawn) != 0) {
        fprintf(stderr, "could not send message successfully\n");
        exit(1);
    } else {
        printf("sent successfully\n");
    }


    if (send_message(actorId, msgSpawn) != 0) {
        fprintf(stderr, "could not send message successfully\n");
        exit(1);
    } else {
        printf("sent successfully\n");
    }
    sleep(2);

/*
    int retval = send_message(actorId, msgGoDie);
    if (retval != 0) {
        fprintf(stderr, "could not send message successfully with retval %d\n", retval);
        exit(1);
    } else {
        printf("sent DIE successfully\n");
    }
*/

    actor_system_join(0);

printf(" ========= ROUND 2 ========\n");

    if (actor_system_create(&actorId, &role) != 0) {
        fprintf(stderr, "not success creation\n");
        exit(1);
    } else {
        printf("system successfully created\n");
    }



    if (send_message(actorId, msgSpawn) != 0) {
        fprintf(stderr, "could not send message successfully\n");
        exit(1);
    } else {
        printf("sent successfully\n");
    }
    if (send_message(actorId, msgSpawn) != 0) {
        fprintf(stderr, "could not send message successfully\n");
        exit(1);
    } else {
        printf("sent successfully\n");
    }

    actor_system_join(0);

printf(" ========= ROUND 3 ========\n");


    if (actor_system_create(&actorId, &role) != 0) {
        fprintf(stderr, "not success creation\n");
        exit(1);
    } else {
        printf("system successfully created\n");
    }

    if (send_message(actorId, msgGoDie) != 0) {
        fprintf(stderr, "could not send message successfully\n");
        exit(1);
    } else {
        printf("sent successfully\n");
    }

    if (send_message(actorId, msgSpawn) != 0) {
        fprintf(stderr, "could not send message successfully\n");
        exit(1);
    } else {
        printf("sent successfully\n");
    }
    if (send_message(actorId, msgSpawn) != 0) {
        fprintf(stderr, "could not send message successfully\n");
        exit(1);
    } else {
        printf("sent successfully\n");
    }
    if (send_message(actorId, msgSpawn) != 0) {
        fprintf(stderr, "could not send message successfully\n");
        exit(1);
    } else {
        printf("sent successfully\n");
    }

    actor_system_join(0);

    free(prompts);
    return 0;
}
