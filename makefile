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

S = rtl_airband_asm.s

C = rtl_airband.cpp hello_fft/mailbox.c hello_fft/gpu_fft.c hello_fft/gpu_fft_base.c hello_fft/gpu_fft_twiddles.c hello_fft/gpu_fft_shaders.c

B = rtl_airband

F = -O3 -mcpu=arm1176jzf-s -mtune=arm1176jzf-s -march=armv6zk -mfpu=vfp -L/opt/vc/lib/ -lbcm_host -I/opt/vc/include -I/opt/vc/include/interface/vcos/pthreads -I/opt/vc/include/interface/vmcs_host/linux -lrt -lm -lvorbisenc -lmp3lame -lshout -lpthread -lrtlsdr -o $(B)

$(B):	$(H) $(C) $(S)
	as -o rtl_airband_asm.o $(S)
	g++ $(F) $(C) $(S)

clean:
	rm -f $(B)
