MINGW_CC ?= i686-w64-mingw32-gcc
CC := $(MINGW_CC)

CFLAGS += -O2 -municode -Wall -Wextra
LDFLAGS += -municode -Wl,--subsystem,windows
LIBS += -lole32 -loleaut32 -lshell32

TARGET := namidiaforxp.exe
OBJS := main.o

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

main.o: main.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

# After building namidiaforxp.exe, append the payload zip and footer marker:
#   python3 - <<'PY'
#   import struct
#   data = open("zip.zip", "rb").read()
#   footer = struct.pack("<II", 0x3050495A, len(data))
#   with open("namidiaforxp.exe", "ab") as f:
#       f.write(data)
#       f.write(footer)
#   PY
