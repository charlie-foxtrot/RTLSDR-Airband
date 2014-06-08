H = hello_fft/hex/shader_1k.hex \
    hello_fft/hex/shader_2k.hex \
    hello_fft/hex/shader_4k.hex \
    hello_fft/hex/shader_8k.hex \
    hello_fft/hex/shader_16k.hex \
    hello_fft/hex/shader_32k.hex \
    hello_fft/hex/shader_64k.hex \
    hello_fft/hex/shader_128k.hex \
    hello_fft/hex/shader_256.hex \
    hello_fft/hex/shader_512.hex

C = rtl_airband.cpp hello_fft/mailbox.c hello_fft/gpu_fft.c hello_fft/gpu_fft_twiddles.c hello_fft/gpu_fft_shaders.c

B = rtl_airband

F = -O3 -mcpu=arm1176jzf-s -mtune=arm1176jzf-s -march=armv6zk -mfpu=vfp -lrt -lm -lvorbisenc -lmp3lame -lshout -lpthread -lrtlsdr -o $(B)

$(B):	$(H) $(C)
	g++ $(F) $(C)

clean:
	rm -f $(B)
