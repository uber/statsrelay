#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include "../hashmap.h"

static int print_callback(void* _s, const char* key, void* _value, void* metadata) {
    int *v = (int *)_s;
    *v += 1;
    return 0;
}

static int print_callback_break(void* _s, const char* key, void* _value, void* metadata) {
    int *v = (int *)_s;
    *v += 1;
    return 1; // will break the iter loop
}

void test_map_get_no_keys() {
    hashmap *map;
    int res = hashmap_init(0, &map);
    assert(res == 0);

    assert(hashmap_size(map) == 0);

    char buf[100];
    void *out;
    for (int i = 0; i < 100; i++) {
        snprintf((char*)&buf, 100, "test%d", i);
        assert(hashmap_get(map, (char*)buf, &out) == -1);
    }

    res = hashmap_destroy(map);
    assert(res == 0);
}

void test_map_put() {
    hashmap *map;
    int res = hashmap_init(0, &map);
    assert(res == 0);
    assert(hashmap_size(map) == 0);

    char buf[100];
    void *out;
    for (int i = 0; i < 100; i++) {
        snprintf((char*)&buf, 100, "test%d", i);
        assert(hashmap_put(map, (char*)buf, NULL, NULL) == 1);
    }
    assert(hashmap_size(map) == 100);

    res = hashmap_destroy(map);
    assert(res == 0);
}

void test_map_put_get() {
    hashmap *map;
    int res = hashmap_init(0, &map);
    assert(res == 0);

    char buf[100];
    void *out;
    int j;
    for (int i = 0; i < 100; i++) {
        snprintf((char*)&buf, 100, "test%d", i);
        j = 0 & i;
        out = (void *)&j;
        assert(hashmap_put(map, (char*)buf, out, NULL) == 1);
    }

    for (int i = 0; i < 100; i++) {
        snprintf((char*)&buf, 100, "test%d", i);
        assert(hashmap_get(map, (char*)buf, &out) == 0);
        j = 0 & i;
        int x = *((int*)(out));
        assert(x == j);
    }

    res = hashmap_destroy(map);
    assert(res == 0);
}

void test_map_delete_no_keys() {
    hashmap *map;
    int res = hashmap_init(0, &map);
    assert(res == 0);
    assert(hashmap_size(map) == 0);

    char buf[100];
    for (int i = 0; i < 100; i++) {
        snprintf((char*)&buf, 100, "test%d", i);
        assert(hashmap_delete(map, (char*)buf) == -1);
    }

    res = hashmap_destroy(map);
    assert(res == 0);
}

void test_map_put_delete() {
    hashmap *map;
    int res = hashmap_init(0, &map);
    assert(res == 0);

    char buf[100];
    void *out;
    int j;
    for (int i = 0; i < 100; i++) {
        snprintf((char*)&buf, 100, "test%d", i);
        j = 0 & i;
        out = (void *)&j;
        assert(hashmap_put(map, (char*)buf, out, NULL) == 1);
    }
    assert(hashmap_size(map) == 100);

    for (int i = 0; i < 100; i++) {
        snprintf((char*)&buf, 100, "test%d", i);
        assert(hashmap_delete(map, (char*)buf) == 0);
        assert(hashmap_size(map) == (100 - i - 1));
    }

    assert(hashmap_size(map) == 0);
    res = hashmap_destroy(map);
    assert(res == 0);
}

void test_map_put_delete_get() {
    hashmap *map;
    int res = hashmap_init(0, &map);
    assert(res == 0);

    char buf[100];
    void *out;
    int j;
    for (int i = 0; i < 100;i++) {
        snprintf((char*)&buf, 100, "test%d", i);
        j = 0 & i;
        out = (void *)&j;
        assert(hashmap_put(map, (char*)buf, out, NULL) == 1);
    }

    for (int i  =0; i < 100;i++) {
        snprintf((char*)&buf, 100, "test%d", i);
        assert(hashmap_delete(map, (char*)buf) == 0);
    }

    for (int i = 0; i < 100;i++) {
        snprintf((char*)&buf, 100, "test%d", i);
        assert(hashmap_get(map, (char*)buf, &out) == -1);
    }

    res = hashmap_destroy(map);
    assert(res == 0);
}

void test_map_clear_no_keys() {
    hashmap *map;
    int res = hashmap_init(0, &map);
    assert(res == 0);

    assert(hashmap_clear(map) == 0);

    res = hashmap_destroy(map);
    assert(res == 0);
}

void test_map_put_clear_get() {
    hashmap *map;
    int res = hashmap_init(0, &map);
    assert(res == 0);

    char buf[100];
    void *out;
    int j;
    for (int i = 0; i < 100; i++) {
        snprintf((char*)&buf, 100, "test%d", i);
        j = 0 & i;
        out = (void *)&j;
        assert(hashmap_put(map, (char*)buf, out, NULL) == 1);
    }

    assert(hashmap_size(map) == 100);
    assert(hashmap_clear(map) == 0);
    assert(hashmap_size(map) == 0);

    for (int i = 0; i < 100; i++) {
        snprintf((char*)&buf, 100, "test%d", i);
        assert(hashmap_get(map, (char*)buf, &out) == -1);
    }

    res = hashmap_destroy(map);
    assert(res == 0);
}

void test_map_iter_no_keys() {
    hashmap *map;
    int res = hashmap_init(0, &map);
    assert(res == 0);

    int val = 0;
    assert(hashmap_iter(map, print_callback, (void*)&val) == 0);
    assert(val == 0);

    res = hashmap_destroy(map);
    assert(res == 0);
}

void test_map_put_iter_break() {
    hashmap *map;
    int res = hashmap_init(0, &map);
    assert(res == 0);

    char buf[100];
    void *out;
    for (int i = 0; i < 100; i++) {
        snprintf((char*)&buf, 100, "test%d", i);
        assert(hashmap_put(map, (char*)buf, NULL, NULL) == 1);
    }

    int val = 0;
    assert(hashmap_iter(map, print_callback_break, (void*)&val) == 1);
    assert(val == 1);

    res = hashmap_destroy(map);
    assert(res == 0);
}

void test_map_put_grow() {
    hashmap *map;
    int res = hashmap_init(32, &map);
    assert(res == 0);

    char buf[100];
    void *out;
    for (int i = 0; i < 1000; i++) {
        snprintf((char*)&buf, 100, "test%d", i);
        assert(hashmap_put(map, (char*)buf, NULL, NULL) == 1);
    }

    int val = 0;
    assert(hashmap_iter(map, print_callback, (void*)&val) == 0);
    assert(val == 1000);
    assert(hashmap_size(map) == 1000);

    res = hashmap_destroy(map);
    assert(res == 0);
}

int main(int argc, char **argv) {

    // 1. hashmap call with break
    test_map_put_iter_break();

    // 2. hashmap callback continue
    test_map_put_grow();

    // 3. hashmap empty
    test_map_iter_no_keys();

    // 4. hashmap iter, non existent key get
    test_map_get_no_keys();

    // 5. clear no keys
    test_map_clear_no_keys();

    // 6. Put test
    test_map_put();

    // 7. put, clear and get (-1)
    test_map_put_clear_get();

    // 8. put, delete and get (-1)
    test_map_put_delete_get();

    // 9. put and delete hashmap
    test_map_put_delete();

    // 10. delete with no keys
    test_map_delete_no_keys();

    // 11. put and get hashmap
    test_map_put_get();

    return 0;
}

