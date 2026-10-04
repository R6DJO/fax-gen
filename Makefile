# tiff2wav - TIFF -> WAV (AMT/APT facsimile audio) generator
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 Alexander (R6DJO)

CC      = gcc
CFLAGS  = -O2 -Wall -std=c99 $(shell pkg-config --cflags libtiff-4)
LDLIBS  = $(shell pkg-config --libs libtiff-4) -lm

TARGET  = tiff2wav
SRCS    = tiff2wav.c

TEST_IN      = tests/test_card.tiff
TEST_OUT     = test_card_output.wav
# 200 image rows @ 120 lpm / 8 kHz = 4000 samples per row, plus
# 5 s start tone + 60 phasing lines + 5 s stop tone + 10 s black silence
TEST_BYTES   = 2400044

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test: $(TARGET)
	@./$(TARGET) $(TEST_IN) $(TEST_OUT) > /dev/null
	@test $$(wc -c < $(TEST_OUT)) -eq $(TEST_BYTES) \
		&& echo "test: OK ($(TEST_OUT) = $(TEST_BYTES) bytes)" \
		|| { echo "test: FAIL (expected $(TEST_BYTES) bytes)"; exit 1; }

clean:
	rm -f $(TARGET) *.o $(TEST_OUT)

.PHONY: all test clean
