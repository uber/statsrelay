#undef NDEBUG

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../buffer.h"

const char* line = "foo.bar.f:2|c\n";

int main(int argc, char **argv) {
    int len = strlen(line);

    buffer_t buf;
    buffer_init(&buf);
    buffer_newsize(&buf, 1024*1024);

    char* head = buffer_head(&buf);
    memcpy(head, line, len);
    buffer_produced(&buf, len);
    head += len;
    memcpy(head, line, len);
    buffer_produced(&buf, len);

    assert(buffer_datacount(&buf) == len * 2);

    buffer_consume_until(&buf, '\n');

    assert(buffer_datacount(&buf) == len);

    buffer_consume_until(&buf, '\n');
    assert(buffer_datacount(&buf) == 0);

    buffer_consume_until(&buf, '\n');
    assert(buffer_datacount(&buf) == 0);

}
