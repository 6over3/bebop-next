CC       := afl-clang-fast
CFLAGS   := -O2 -g -std=c23 -Wall -Wextra -Werror -DMAKE_LIB
CFLAGS   += -I../bebop/src -I../bebop/include -I../vendor/cwisstable -I../vendor/lua
LDFLAGS  :=
BUILD    := build
CORPUS   := ../tests/fixtures/valid
LUA_SRC  := ../vendor/lua/onelua.c

.PHONY: all clean run run-json corpus asan

all: $(BUILD)/fuzz_parse $(BUILD)/fuzz_json_decode

$(BUILD):
	mkdir -p $(BUILD)/out

$(BUILD)/fuzz_parse: fuzz_parse.c $(LUA_SRC) | $(BUILD)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

$(BUILD)/fuzz_json_decode: fuzz_json_decode.c | $(BUILD)
	$(CC) $(CFLAGS) -I../tests $(LDFLAGS) -o $@ $<

$(BUILD)/fuzz_parse_asan: fuzz_parse.c $(LUA_SRC) | $(BUILD)
	clang $(CFLAGS) -fsanitize=address,undefined -o $@ $^

$(BUILD)/fuzz_json_decode_asan: fuzz_json_decode.c | $(BUILD)
	clang $(CFLAGS) -I../tests -fsanitize=address,undefined -o $@ $<

$(BUILD)/gen_corpus: gen_corpus.c | $(BUILD)
	clang $(CFLAGS) -I../tests -o $@ $<

asan: $(BUILD)/fuzz_parse_asan $(BUILD)/fuzz_json_decode_asan

run: $(BUILD)/fuzz_parse
	@mkdir -p $(BUILD)/out/parse
	afl-fuzz -i $(CORPUS) -o $(BUILD)/out/parse -- $<

run-json: $(BUILD)/fuzz_json_decode corpus
	@mkdir -p $(BUILD)/out/json_decode
	afl-fuzz -i corpus/json_wire -o $(BUILD)/out/json_decode -- $<

corpus: $(BUILD)/gen_corpus
	@mkdir -p corpus/json_wire
	./$<

clean:
	rm -rf $(BUILD) corpus
