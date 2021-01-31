#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include "../cacti.h"

#define NO_INCREMENTS 10

int x=1;

void hello(void** stateptr, size_t size, void* data){
    assert(*stateptr == NULL);
    printf("hello, i am %ld, and my father is %ld\n", actor_id_self(), (actor_id_t)data);
}

void fun1(void** stateptr, size_t size, void* data){
    printf("fun1 called\n");
    message_t msg = {
            .message_type = 0
    };
    sleep(2);
    send_message(2,msg);
}

void fun2(void** stateptr, size_t size, void* data){
    printf("fun2 called\n");
    message_t msg = {
            .message_type = 0
    };
    send_message(1,msg);
}


int main(){
    const size_t nprompts = 3;
    void (**prompts)(void**, size_t, void*) = malloc(sizeof(void*) * nprompts);
    prompts[0] = &hello;
    prompts[1] = &fun1;
    prompts[2] = &fun2;
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

    message_t msgFun1 = {
            .message_type = 1
    };

    message_t msgFun2 = {
            .message_type = 2
    };

    actor_id_t actorId;
    actor_system_create(&actorId, &role);
    send_message(actorId, msgSpawn);
    send_message(actorId, msgSpawn);
    sleep(2);
    printf("spawning");
    send_message(1, msgFun1);
    send_message(2, msgFun2);
    sleep(2);
    send_message(1, msgGoDie);
    send_message(2, msgGoDie);
    send_message(0, msgGoDie);

    actor_system_join(0);
    actor_system_join(0);

    free(prompts);
    return 0;
}
