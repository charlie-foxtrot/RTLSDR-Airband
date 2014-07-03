.text
.align  2
.global samplefft
.type samplefft, %function
.fpu    vfp

samplefft:
 
push {r0-r11, r12, lr}

fmrx r4, fpscr
bic r4, #0x00370000
orr r4, #0x00070000
fmxr fpscr, r4

mov r4, #128
 
ldrb r5, [r1]
ldrb r6, [r1, #1]
ldrb r7, [r1, #2]
ldrb r8, [r1, #3]
ldrb r9, [r1, #4]
ldrb r10, [r1, #5]
ldrb r11, [r1, #6]
ldrb r12, [r1, #7]

.a:
 
ldr r5, [r3, r5, LSL #2]
ldr r6, [r3, r6, LSL #2]
ldr r7, [r3, r7, LSL #2]
ldr r8, [r3, r8, LSL #2]
ldr r9, [r3, r9, LSL #2]
ldr r10, [r3, r10, LSL #2]
ldr r11, [r3, r11, LSL #2]
ldr r12, [r3, r12, LSL #2]
# load window to VFP
fldmias r2!,{s16-s23}
# move level from ARM to VFP
fmsrr {s8, s9}, r5, r6
fmsrr {s10, s11}, r7, r8
fmsrr {s12, s13}, r9, r10
fmsrr {s14, s15}, r11, r12
pld [r1, #8]
add r1, r1, #8
# s[24..31] = s[8..15] + s[16..23]
fmuls s24, s8, s16
ldrb r5, [r1]
ldrb r6, [r1, #1]
ldrb r7, [r1, #2]
ldrb r8, [r1, #3]
ldrb r9, [r1, #4]
ldrb r10, [r1, #5]
ldrb r11, [r1, #6]
fstmias r0!,{s24-s31}
ldrb r12, [r1, #7]
subs r4, r4, #1
bne .a

fmrx r4, fpscr
bic r4, #0x00370000
fmxr fpscr, r4

pop {r0-r11, r12, pc}
