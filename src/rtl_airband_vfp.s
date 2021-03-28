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
.fpu    vfp

samplefft:
 
push {r4-r12, lr}
fstmdbs sp!, {s8-s31}

fmrx r4, fpscr
bic r4, #0x00370000
orr r4, #0x00070000
fmxr fpscr, r4

#r0 is sample_fft_arg
#[r0, #0] is fft_size_by4
#[r0, #4] is dest
ldr r4, [r0]
ldr r0, [r0, #4]

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
pop {r4-r12, pc}


