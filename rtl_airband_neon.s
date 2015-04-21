# 
# RTLSDR AM demodulator and streaming
# 
# Copyright (c) 2014 Wong Man Hang <microtony@gmail.com>
#
# Updates for NEON coprocessor by Tomasz Lemiech <szpajder@gmail.com>
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
.fpu    neon

samplefft:
 
push {r4-r12, lr}
vpush {d4-d15}
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
# load window to NEON registers
vldmia r2!,{d8-d11}
add r1, r1, #8
# move level from ARM to NEON registers
vmov d4, r5, r6
vmov d5, r7, r8
vmov d6, r9, r10
vmov d7, r11, r12
pld [r1, #16]
vmul.f32 q6, q2, q4
vmul.f32 q7, q3, q5
pld [r2, #8]
ldrb r5, [r1]
ldrb r6, [r1, #1]
ldrb r7, [r1, #2]
ldrb r8, [r1, #3]
ldrb r9, [r1, #4]
ldrb r10, [r1, #5]
ldrb r11, [r1, #6]
ldrb r12, [r1, #7]
vstmia r0!,{q6-q7}
subs r4, r4, #1
bne .a

vpop {d4-d15}
pop {r4-r12, pc}


fftwave:

push {r4-r12, lr}
vpush {d4-d15}

#r2 is int[2]
#[r2, #0] is fftstep
#[r2, #4] is wavestep
ldr r12, [r2, #4]
ldr r2, [r2]
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

vldr s8, [r4]
vldr s9, [r5]
vldr s10, [r6]
vldr s11, [r7]
vldr s12, [r8]
vldr s13, [r9]
vldr s14, [r10]
vldr s15, [r11]
vldr s16, [r4, #4]
vldr s17, [r5, #4]
vmul.f32 q2, q2, q2
vmul.f32 q3, q3, q3
vldr s18, [r6, #4]
vldr s19, [r7, #4]
vldr s20, [r8, #4]
vldr s21, [r9, #4]
vldr s22, [r10, #4]
vldr s23, [r11, #4]
vldr s24, [r4, #8]
vldr s25, [r5, #8]
vmul.f32 q4, q4, q4
vmul.f32 q5, q5, q5
vldr s26, [r6, #8]
vldr s27, [r7, #8]
vldr s28, [r8, #8]
vldr s29, [r9, #8]
vldr s30, [r10, #8]
vldr s31, [r11, #8]
vadd.f32 q2, q2, q4
vadd.f32 q3, q3, q5
# vrsqrte returns 1/sqrt(x), so we do 1/x (vrecpe) on result
vrsqrte.f32 q2, q2
vrecpe.f32 q2, q2
vrsqrte.f32 q3, q3
vrecpe.f32 q3, q3
vldr s16, [r4, #12]
vldr s17, [r5, #12]
vldr s18, [r6, #12]
vldr s19, [r7, #12]
vldr s20, [r8, #12]
vldr s21, [r9, #12]
vldr s22, [r10, #12]
vldr s23, [r11, #12]
vmul.f32 q6, q6, q6
vmul.f32 q7, q7, q7
vmul.f32 q4, q4, q4
vmul.f32 q5, q5, q5
add r4, r2, r4
add r5, r2, r5
add r6, r2, r6
add r7, r2, r7
add r8, r2, r8
add r9, r2, r9
vadd.f32 q4, q4, q6
vadd.f32 q5, q5, q5
vrsqrte.f32 q4, q4
vrecpe.f32 q4, q4
vrsqrte.f32 q5, q5
vrecpe.f32 q5, q5
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
mov r4, r0
add r5, r4, r12
add r6, r5, r12
add r7, r6, r12
add r8, r7, r12
add r9, r8, r12
add r10, r9, r12
add r11, r10, r12
vadd.f32 q2, q2, q4
vadd.f32 q5, q5, q5
vstr s8, [r4]
vstr s9, [r5]
vstr s10, [r6]
vstr s11, [r7]
vstr s12, [r8]
vstr s13, [r9]
vstr s14, [r10]
vstr s15, [r11]

pop {r4-r11}
add r0, r0, #4
subs r3, r3, #1
bne .b

vpop {d4-d15}
pop {r4-r12, pc}
