# tiff2wav - TIFF -> WAV (AMT/APT facsimile audio) generator
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 Alexander (R6DJO)

CC      = gcc
CFLAGS  = -O2 -Wall -std=c99 $(shell pkg-config --cflags libtiff-4)
LDLIBS  = $(shell pkg-config --libs libtiff-4) -lm

TARGET  = tiff2wav
SRCS    = tiff2wav.c

# 200 image rows @ 120 lpm / 8 kHz = 4000 samples per row, plus
# 5 s start tone + 60 phasing lines + 5 s stop tone + 10 s black silence
TEST_CARDS = tests/test_card.tiff tests/test_card_1bit.tiff
TEST_BYTES = 2400044
TEST_OUTS  = test_card_output.wav test_card_1bit_output.wav

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# Container and length regression for every test card. The od field reads use
# native (little-endian) integer formats, i.e. they assume an x86-class host.
test: $(TARGET)
	@for card in $(TEST_CARDS); do \
		out=`basename $$card .tiff`_output.wav; \
		./$(TARGET) $$card $$out > /dev/null || exit 1; \
		size=`wc -c < $$out`; \
		magic=`od -An -tx1 -N4  $$out | tr -d ' \n'`; \
		fmt=`od  -An -tu2 -j20 -N2 $$out | tr -d ' '`; \
		chs=`od  -An -tu2 -j22 -N2 $$out | tr -d ' '`; \
		rate=`od -An -tu4 -j24 -N4 $$out | tr -d ' '`; \
		bits=`od -An -tu2 -j34 -N2 $$out | tr -d ' '`; \
		if [ "$$size" != "$(TEST_BYTES)" ] || [ "$$magic" != "52494646" ] || \
		   [ "$$fmt" != "1" ] || [ "$$chs" != "1" ] || \
		   [ "$$rate" != "8000" ] || [ "$$bits" != "16" ]; then \
			echo "test: FAIL $$out (size=$$size magic=$$magic fmt=$$fmt chs=$$chs rate=$$rate bits=$$bits)"; \
			exit 1; \
		fi; \
		echo "test: OK $$out ($$size bytes, RIFF PCM mono 8000 Hz 16 bit)"; \
	done

clean:
	rm -f $(TARGET) *.o $(TEST_OUTS)

.PHONY: all test clean
