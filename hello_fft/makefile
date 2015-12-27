CFLAGS += -I..

S = hex/shader_256.hex \
    hex/shader_512.hex \
    hex/shader_1k.hex \
    hex/shader_2k.hex \
    hex/shader_4k.hex \
    hex/shader_8k.hex \
    hex/shader_16k.hex \
    hex/shader_32k.hex \
    hex/shader_64k.hex \
    hex/shader_128k.hex \
    hex/shader_256k.hex \
    hex/shader_512k.hex \
    hex/shader_1024k.hex \
    hex/shader_2048k.hex

OBJ = mailbox.o gpu_fft.o gpu_fft_twiddles.o gpu_fft_shaders.o gpu_fft_base.o
LIB = hello_fft.a

.PHONY: all clean

all: $(LIB)

$(LIB): $(OBJ)
	ar r $(LIB) $(OBJ)

gpu_fft_base.o: gpu_fft.h mailbox.h

gpu_fft.o: gpu_fft.h

gpu_fft_shaders.o: $(S)

gpu_fft_twiddles.o: gpu_fft.h

mailbox.o: mailbox.h ../rtl_airband.h

clean:
	rm -f *.o *.a

