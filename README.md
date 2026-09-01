# Cacti

## Introduction

This project implements a certain version of the
[actor model](https://en.wikipedia.org/wiki/Actor_model).

In short, actors are a processing mechanism that allows for multi-threaded
execution of a set of tasks within a single program. An actor is an entity that
accepts a set of messages. Accepting a message of a given kind meanse the
execution of a certain imperative calculation, which can have side effects, e.g.
transforming some global data structure, but also creating new actors and
sending messages to some existing actors. The messages sent in this model are
asynchronous. In this project, the work of actors is performed by a thread pool,
while the number of actors can be significantly larger than the number of
threads.

This library relies only on `pthreads` library for multithreading purposes.

## Compilation

In the project's root directory:

```bash
mkdir build && cd build
cmake ../src
make
```

This should compile the library into a file `build/libcacti.a`. Testing can be
done using the `test` phony target of `Make`.

## Demo

Project contains a [matrix](src/matrix.c) program, which calculates sums of rows
of a given array and a [factorial](src/factorial.c) program, which calculates
a factorial of a given number. Both these programs illustrate the use of the
actor library developed in this project. What they do is documented inside the
files.

TODO how to run these

## Thread pool

The system of actors is implemented with a thread pool internally. A compile
time constant `POOL_SIZE` defines the number of threads used. Threads are
terminated automatically when every actor finishes functioning.

## Actor model

The library's interface is defined in the [header](src/cacti.h).
TODO The library assumes there is at most one actor system active at a time.
After an actor system terminates, it is possible to start another one.

### Message types

Each actor supports three predefined types of messages:

1) `MSG_SPAWN`
2) `MSG_GODIE`
3) `MSG_HELLO`

The values of the first two types are so large that they will not appear in the
implementations, and their operation is predefined and cannot be changed.
The `MSG_HELLO` type has no predefined way of being handled. However, each actor
should handle messages of this type by the function under index `MSG_HELLO` in
its array of handlers.

#### `MSG_SPAWN`

Handler of this message type interpretes the `data` field as `role_t`. It
spawns a new actor with its handler lookup table defined as that and next sends
a `MSG_HELLO` message to it, with `data` containing the parent actor's id.

#### `MSG_GODIE`

Data holds no meaning for the handling of this message type. This message causes
the recipient to change into a dead state, in which it no longer accepts any
messages. Usually, this means that any messages causing the actor to release
resources (allocated memory, occupied file descriptors etc.) should be sent
before `MSG_GODIE`. However, it is worth noting that any message sent after
`MSG_GODIE` was placed in a message queue, but before the message was processed
will be added to the queue and will need handling. Therefore it is the library
user's responsibility to ensure the handling of those messages will not cause
errors.

#### `MSG_HELLO`

The handling of this message type is not predefined. However, handling these
messages is important because it allows a new actor to get another actor's ID in
the system to be able to send messages to it. `data` field contains another
actor's ID. The handler receives a pointer `stateptr`, such that
`*stateptr == NULL`. The handler may subsequently initialize the actor's
internal state and reassign `*stateptr` to point to it.

A proper operation of an actor system may require message types and handlers
that allow learning about many other actors in the system. Specific solutions
depend on the user and the type of calculations that need to be performed.

## `SIGINT`

TODO send MSG_GODIE or can this make some deadlock or sth?
The program will block the possibility of adding new actors and accepting
messages. Next, the actor system will complete handling all messages sent to
actors and terminate the system.
