#include <assert.h>
#include <debounce.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define ITERATIONS 1000000

typedef struct {
        struct debounce *db;
        atomic_bool *stop;
} thread_args_t;

static void *
isr_thread_func(void *arg)
{
        thread_args_t *args = (thread_args_t *)arg;

        for (int i = 0; i < ITERATIONS && !atomic_load(args->stop); i++) {
                /* Rapidly toggle input */
                debounce_update(args->db, (i % 3 == 0));
        }
        return NULL;
}

static void *
main_loop_thread_func(void *arg)
{
        thread_args_t *args = (thread_args_t *)arg;

        for (int i = 0; i < ITERATIONS && !atomic_load(args->stop); i++) {
                /* Rapidly query state */
                bool active = debounce_is_active(args->db);
                uint16_t counter = debounce_get_counter(args->db);
                bool latched = debounce_is_latched(args->db);
                uint16_t trip = debounce_get_trip(args->db);

                /* Basic sanity checks */
                assert(counter <= trip);
                (void)active;
                (void)latched;
        }
        return NULL;
}

int
main(void)
{
        struct debounce db;
        atomic_bool stop = false;
        thread_args_t args = {.db = &db, .stop = &stop};

        /* Initialise before starting threads */
        if (!debounce_init_symmetric(&db, 10, 5)) {
                fprintf(stderr, "Failed to init debounce\n");
                return 1;
        }

        pthread_t isr_thread, main_thread;

        printf("Starting concurrency stress test (%d iterations)...\n",
               ITERATIONS);

        if (pthread_create(&isr_thread, NULL, isr_thread_func, &args) != 0) {
                perror("Failed to create ISR thread");
                return 1;
        }
        if (pthread_create(&main_thread, NULL, main_loop_thread_func, &args)
            != 0) {
                perror("Failed to create Main Loop thread");
                return 1;
        }

        pthread_join(isr_thread, NULL);
        pthread_join(main_thread, NULL);

        printf("Concurrency stress test passed!\n");
        return 0;
}
