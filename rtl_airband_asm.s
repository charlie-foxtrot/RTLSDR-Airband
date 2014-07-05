# 
# RTLSDR AM demodulator and streaming
# 
# Copyright (c) 2014 Wong Man Hang <microtony@gmail.com>
# 
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
# 

.text
.align  2
.global samplefft
.type samplefft, %function
.global fftwave
.type fftwave, %function
.fpu    vfp

samplefft:
 
push {r0-r11, r12, lr}
fstmdbs sp!, {s8-s31}

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
add r1, r1, #8
# move level from ARM to VFP
fmsrr {s8, s9}, r5, r6
fmsrr {s10, s11}, r7, r8
fmsrr {s12, s13}, r9, r10
fmsrr {s14, s15}, r11, r12
pld [r1, #16]
fmuls s24, s8, s16
pld [r2, #8]
ldrb r5, [r1]
ldrb r6, [r1, #1]
ldrb r7, [r1, #2]
ldrb r8, [r1, #3]
ldrb r9, [r1, #4]
ldrb r10, [r1, #5]
ldrb r11, [r1, #6]
ldrb r12, [r1, #7]
fstmias r0!,{s24-s31}
subs r4, r4, #1
bne .a

fmrx r4, fpscr
bic r4, #0x00370000
fmxr fpscr, r4

fldmias sp!, {s8-s31}
pop {r0-r11, r12, pc}


fftwave:

push {r4-r12, lr}
fstmdbs sp!, {s8-s31}

fmrx r12, fpscr
fmrx r11, fpscr
bic r11, #0x00370000
orr r11, #0x00070000
fmxr fpscr, r11

ldmia r3, {r4-r11}
mov r3, #8
mla r4, r3, r4, r1
mla r5, r3, r5, r1
mla r6, r3, r6, r1
mla r7, r3, r7, r1
mla r8, r3, r8, r1
mla r9, r3, r9, r1
mla r10, r3, r10, r1
mla r11, r3, r11, r1
mov r3, #250

.b:

flds s8, [r4]
flds s9, [r5]
flds s10, [r6]
flds s11, [r7]
flds s12, [r8]
flds s13, [r9]
flds s14, [r10]
flds s15, [r11]
flds s16, [r4, #4]
flds s17, [r5, #4]
fmuls s8, s8, s8
flds s18, [r6, #4]
flds s19, [r7, #4]
flds s20, [r8, #4]
flds s21, [r9, #4]
flds s22, [r10, #4]
flds s23, [r11, #4]
flds s24, [r4, #8]
flds s25, [r5, #8]
fmuls s16, s16, s16
flds s26, [r6, #8]
flds s27, [r7, #8]
flds s28, [r8, #8]
flds s29, [r9, #8]
flds s30, [r10, #8]
flds s31, [r11, #8]
fadds s8, s8, s16
fsqrts s8, s8
flds s16, [r4, #12]
flds s17, [r5, #12]
flds s18, [r6, #12]
flds s19, [r7, #12]
flds s20, [r8, #12]
flds s21, [r9, #12]
flds s22, [r10, #12]
flds s23, [r11, #12]
fmuls s24, s24, s24
fmuls s16, s16, s16
add r4, r2, r4
add r5, r2, r5
add r6, r2, r6
add r7, r2, r7
add r8, r2, r8
add r9, r2, r9
fadds s16, s16, s24
fsqrts s16, s16
add r10, r2, r10
add r11, r2, r11
pld [r4]
pld [r5]
pld [r6]
pld [r7]
pld [r8]
pld [r9]
pld [r10]
pld [r11]
push {r4-r11}
add r4, r0, #0
add r5, r0, #8192
add r6, r0, #16384
add r7, r0, #24576
add r8, r0, #32768
add r9, r0, #40960
add r10, r0, #49152
add r11, r0, #57344
fadds s8, s8, s16
fsts s8, [r4]
fsts s9, [r5]
fsts s10, [r6]
fsts s11, [r7]
fsts s12, [r8]
fsts s13, [r9]
fsts s14, [r10]
fsts s15, [r11]

pop {r4-r11}
add r0, r0, #4
subs r3, r3, #1
bne .b

fmxr fpscr, r12

fldmias sp!, {s8-s31}
pop {r4-r12, pc}
