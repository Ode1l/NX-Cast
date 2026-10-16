CC = cc
ROOT := ../..
OUT := $(ROOT)/build/miracast-protocol
SOURCES := options.c response.c test_options.c test_response.c $(ROOT)/source/protocol/rtsp/rtsp_request.c
FLAGS := -std=c11 -Wall -Wextra -Werror -I$(ROOT)/source

.PHONY: test sanitize
test:
	mkdir -p $(OUT)
	$(CC) $(FLAGS) $(SOURCES) -o $(OUT)/test-options
	$(OUT)/test-options

sanitize:
	mkdir -p $(OUT)
	$(CC) $(FLAGS) -g -fsanitize=address,undefined -fno-omit-frame-pointer $(SOURCES) -o $(OUT)/test-options-sanitize
	$(OUT)/test-options-sanitize
