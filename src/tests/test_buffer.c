#undef NDEBUG

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../buffer.h"

const char* line = "foo.bar.f:2|c\n"; // strlen() == 14

void test_basic() {
    int len = strlen(line);
    int err;

    buffer_t buf;
    buffer_init(&buf);
    buffer_newsize(&buf, 32);
    assert(buf.size == 32);

    buffer_append(&buf, line, len);
    buffer_append(&buf, line, len);
    assert(buffer_datacount(&buf) == len * 2);
    assert(buffer_spacecount(&buf) == 4); // 32 - 2*14 = 4

    buffer_consume_until(&buf, '\n');
    assert(buffer_datacount(&buf) == len);

    buffer_consume_until(&buf, '\n');
    assert(buffer_datacount(&buf) == 0);

    buffer_consume_until(&buf, '\n');
    assert(buffer_datacount(&buf) == 0);
    assert(buffer_spacecount(&buf) == 4); // 32 - 2*14 = 4

    // insufficient space
    err = buffer_append(&buf, line, len);
    assert(err == -1);
    assert(buf.size == 32);
    assert(buffer_datacount(&buf) == 0);
    assert(buffer_spacecount(&buf) == 4); // 32 - 2*14 = 4

    // expand
    buffer_expand(&buf);
    assert(buf.size == 32 * 2);
    assert(buffer_datacount(&buf) == 0);
    assert(buffer_spacecount(&buf) == 36); // 64 - 2*14 = 36

    buffer_append(&buf, line, len);
    assert(buffer_datacount(&buf) == len);
    assert(buffer_spacecount(&buf) == 22); // 36 - 14 = 22

    buffer_consume_until(&buf, '\n');
    assert(buffer_datacount(&buf) == 0);

    // realign when head == tail
    buffer_realign(&buf);
    assert(buffer_datacount(&buf) == 0);
    assert(buffer_spacecount(&buf) == 64);

    buffer_append(&buf, line, len);
    buffer_append(&buf, line, len);
    assert(buffer_datacount(&buf) == len * 2);
    assert(buffer_spacecount(&buf) == 36); // 64 - 2*14 = 36

    buffer_consume_until(&buf, '\n');
    assert(buffer_datacount(&buf) == len);

    // realign when head != tail
    buffer_realign(&buf);
    assert(buffer_datacount(&buf) == len);
    assert(buffer_spacecount(&buf) == 50); // 64 - 14 = 50

    buffer_consume_until(&buf, '\n');
    assert(buffer_datacount(&buf) == 0);

    // consume more when there's nothing left to consume
    err = buffer_consume(&buf, 1);
    assert(err == -1);
    err = buffer_consume(&buf, len);
    assert(err == -1);
    err = buffer_consume(&buf, 50);
    assert(err == -1);
}

void test_memory_content() {
    buffer_t buf;
    buffer_init(&buf);
    int size = buf.size;
    int offset = 1024;

    // write data to the buffer, then consume the offset amount
    FILE *words;
    words = fopen("/usr/share/dict/words", "r");

    fread(buf.tail, size, 1, words);
    buffer_produced(&buf, size);
    buffer_consume(&buf, offset);
    assert(buf.head == buf.ptr + offset);
    assert(buf.tail == buf.ptr + size);
    assert(buffer_spacecount(&buf) == 0);

    // mimic tcpclient_sendall: realign to create the offset amount of space
    buffer_realign(&buf);
    assert(buf.head == buf.ptr);
    assert(buf.tail == buf.ptr + size - offset);
    assert(buffer_spacecount(&buf) == offset);

    // mimic tcpclient_sendall: expand to accomodate more data
    buffer_expand(&buf);
    buffer_expand(&buf);
    assert(buf.head == buf.ptr);
    assert(buf.tail == buf.ptr + size - offset);
    assert(buffer_spacecount(&buf) == offset + 3*size);
    assert(buf.size == 4*size);

    // write some more data to the buffer
    int write_amount = 2*size + 2*offset;
    fread(buf.tail, write_amount, 1, words);
    buffer_produced(&buf, write_amount);
    assert(buf.head == buf.ptr);
    assert(buf.tail == buf.ptr + 3*size + offset);
    assert(buffer_spacecount(&buf) == size - offset);
    buffer_consume(&buf, 2*size);

    // realign again
    buffer_realign(&buf);
    assert(buf.head == buf.ptr);
    assert(buf.tail == buf.ptr + size + offset);
    assert(buffer_spacecount(&buf) == 3*size - offset);

    // expand again
    buffer_expand(&buf);
    assert(buf.size == 8*size);
    assert(buffer_spacecount(&buf) == 7*size - offset);

    // write again
    write_amount = 2*size + 3*offset;
    fread(buf.tail, write_amount, 1, words);
    buffer_produced(&buf, write_amount);
    assert(buffer_datacount(&buf) == 4*size);

    // build the expected string
    char expected[buf.size];
    fseek(words, 2*size + offset, SEEK_SET); // skip the consumed bit
    fread(expected, 4*size, 1, words);

    assert(strncmp(expected, buf.ptr, buffer_datacount(&buf)) == 0);

    fclose(words);
}

// test without realign or expand
void test_memory_content_simple() {
    buffer_t buf;
    buffer_init(&buf);
    int size = buf.size;
    int offset = 1024;

    FILE *words;
    words = fopen("/usr/share/dict/words", "r");

    int amount = 2*offset;
    fread(buf.tail, amount, 1, words);
    buffer_produced(&buf, amount);
    buffer_consume(&buf, offset);

    fread(buf.tail, offset, 1, words);
    buffer_produced(&buf, offset);
    assert(buffer_datacount(&buf) == 2*offset);
    assert(buffer_spacecount(&buf) == offset);

    // build the expected string
    char expected[buf.size];
    fseek(words, offset, SEEK_SET); // skip the consumed bit
    fread(expected, buffer_datacount(&buf), 1, words);

    assert(strncmp(expected, buf.head, buffer_datacount(&buf)) == 0);

    fclose(words);
}

int main(int argc, char **argv) {
    test_basic();
    test_memory_content();
    test_memory_content_simple();
}
