;++
;
; Module Name:
;
;    stubless.asm
;
; Abstract:
;
;    The call of a server manager routine and the methods of stubless
;    proxies: the ARM port of RPC\NDR20\MIPS\STUBLESS.S.
;
;    The arguments form one array of words, r0 - r3 then the stack. Float
;    and double arguments travel in the floating point registers s0 - s15
;    (d0 - d7), whose image both sides keep in the ARM_FLOAT_ARGUMENT_SIZE
;    bytes below the array (interp.h), at negative stack offsets in the
;    format string (typeinfo.cxx).
;
;--

#include "ksarm.h"
#include "kxarm.h"

        IMPORT  ObjectStublessClient

        TEXTAREA

;++
;
; REGISTER_TYPE
; Invoke (
;     IN MANAGER_FUNCTION pFunction,
;     IN REGISTER_TYPE *pArgumentList,
;     IN double *pFloatingPointArgumentList,
;     IN ULONG cArguments
;     )
;
; Routine Description:
;
;    This function calls a function with the specified arguments: the first
;    four words of the argument list in r0 - r3, the other words on the
;    stack, and d0 - d7 from the floating point argument list.
;
; Arguments:
;
;    pFunction (r0) - The function to call.
;
;    pArgumentList (r1) - The words of the arguments.
;
;    pFloatingPointArgumentList (r2) - The image of d0 - d7, or NULL when
;        the function has no float or double argument.
;
;    cArguments (r3) - The number of words of the argument list.
;
; Return Value:
;
;    The value the function returns.
;
;--

        NESTED_ENTRY Invoke
        PROLOG_PUSH {r4-r7, r11, lr}
        PROLOG_STACK_SAVE r7

        mov     r4, r0                  ; the function
        mov     r5, r1                  ; the argument list
        mov     r6, r3                  ; its number of words
        subs    r3, r3, #4              ; the number of stack words
        ble     %F20                    ; if none
        lsl     r12, r3, #2             ; their space, 8-byte aligned
        add     r12, r12, #7
        bic     r12, r12, #7
        sub     sp, sp, r12
        add     r0, r5, #16             ; copy them
        mov     r1, sp
10      ldr     r12, [r0], #4
        str     r12, [r1], #4
        subs    r3, r3, #1
        bne     %B10
20      cbz     r2, %F30                ; if no floating point arguments
        vldm    r2, {d0-d7}
30      cmp     r6, #4                  ; the register words
        it      hs
        ldrhs   r3, [r5, #12]
        cmp     r6, #3
        it      hs
        ldrhs   r2, [r5, #8]
        cmp     r6, #2
        it      hs
        ldrhs   r1, [r5, #4]
        cmp     r6, #1
        it      hs
        ldrhs   r0, [r5]
        blx     r4                      ; call the function

        EPILOG_STACK_RESTORE r7
        EPILOG_POP {r4-r7, r11, pc}
        NESTED_END Invoke

;++
;
; long
; ObjectStubless (
;     IN void *This,
;     ...
;     )
;
; Routine Description:
;
;    This function makes the call of a method of a stubless proxy, with the
;    method number in r12: it homes r0 - r3 next to the arguments on the
;    stack, so that the arguments form one array of words, saves d0 - d7
;    in the 64 bytes below it and calls ObjectStublessClient with the
;    address of the array and the method number.
;
; Return Value:
;
;    The value of ObjectStublessClient.
;
;--

        NESTED_ENTRY ObjectStubless
        PROLOG_PUSH {r0-r3}
        PROLOG_VPUSH {d0-d7}
        PROLOG_PUSH {r11, lr}

        add     r0, sp, #8 + 64         ; the arguments
        mov     r1, r12                 ; the method number
        bl      ObjectStublessClient

        EPILOG_POP {r11, lr}
        EPILOG_STACK_FREE 64 + 16
        EPILOG_RETURN
        NESTED_END ObjectStubless

;++
;
; ObjectStublessClient3 ... ObjectStublessClient511
;
; Routine Description:
;
;    The methods of stubless proxies (StublessClientVtbl): each continues in
;    ObjectStubless with its method number in r12.
;
;--

        MACRO
        STUBLESS_CLIENT $Method
        LEAF_ENTRY ObjectStublessClient$Method
        movw    r12, #$Method
        b       ObjectStubless
        LEAF_END ObjectStublessClient$Method
        MEND

        STUBLESS_CLIENT 3
        STUBLESS_CLIENT 4
        STUBLESS_CLIENT 5
        STUBLESS_CLIENT 6
        STUBLESS_CLIENT 7
        STUBLESS_CLIENT 8
        STUBLESS_CLIENT 9
        STUBLESS_CLIENT 10
        STUBLESS_CLIENT 11
        STUBLESS_CLIENT 12
        STUBLESS_CLIENT 13
        STUBLESS_CLIENT 14
        STUBLESS_CLIENT 15
        STUBLESS_CLIENT 16
        STUBLESS_CLIENT 17
        STUBLESS_CLIENT 18
        STUBLESS_CLIENT 19
        STUBLESS_CLIENT 20
        STUBLESS_CLIENT 21
        STUBLESS_CLIENT 22
        STUBLESS_CLIENT 23
        STUBLESS_CLIENT 24
        STUBLESS_CLIENT 25
        STUBLESS_CLIENT 26
        STUBLESS_CLIENT 27
        STUBLESS_CLIENT 28
        STUBLESS_CLIENT 29
        STUBLESS_CLIENT 30
        STUBLESS_CLIENT 31
        STUBLESS_CLIENT 32
        STUBLESS_CLIENT 33
        STUBLESS_CLIENT 34
        STUBLESS_CLIENT 35
        STUBLESS_CLIENT 36
        STUBLESS_CLIENT 37
        STUBLESS_CLIENT 38
        STUBLESS_CLIENT 39
        STUBLESS_CLIENT 40
        STUBLESS_CLIENT 41
        STUBLESS_CLIENT 42
        STUBLESS_CLIENT 43
        STUBLESS_CLIENT 44
        STUBLESS_CLIENT 45
        STUBLESS_CLIENT 46
        STUBLESS_CLIENT 47
        STUBLESS_CLIENT 48
        STUBLESS_CLIENT 49
        STUBLESS_CLIENT 50
        STUBLESS_CLIENT 51
        STUBLESS_CLIENT 52
        STUBLESS_CLIENT 53
        STUBLESS_CLIENT 54
        STUBLESS_CLIENT 55
        STUBLESS_CLIENT 56
        STUBLESS_CLIENT 57
        STUBLESS_CLIENT 58
        STUBLESS_CLIENT 59
        STUBLESS_CLIENT 60
        STUBLESS_CLIENT 61
        STUBLESS_CLIENT 62
        STUBLESS_CLIENT 63
        STUBLESS_CLIENT 64
        STUBLESS_CLIENT 65
        STUBLESS_CLIENT 66
        STUBLESS_CLIENT 67
        STUBLESS_CLIENT 68
        STUBLESS_CLIENT 69
        STUBLESS_CLIENT 70
        STUBLESS_CLIENT 71
        STUBLESS_CLIENT 72
        STUBLESS_CLIENT 73
        STUBLESS_CLIENT 74
        STUBLESS_CLIENT 75
        STUBLESS_CLIENT 76
        STUBLESS_CLIENT 77
        STUBLESS_CLIENT 78
        STUBLESS_CLIENT 79
        STUBLESS_CLIENT 80
        STUBLESS_CLIENT 81
        STUBLESS_CLIENT 82
        STUBLESS_CLIENT 83
        STUBLESS_CLIENT 84
        STUBLESS_CLIENT 85
        STUBLESS_CLIENT 86
        STUBLESS_CLIENT 87
        STUBLESS_CLIENT 88
        STUBLESS_CLIENT 89
        STUBLESS_CLIENT 90
        STUBLESS_CLIENT 91
        STUBLESS_CLIENT 92
        STUBLESS_CLIENT 93
        STUBLESS_CLIENT 94
        STUBLESS_CLIENT 95
        STUBLESS_CLIENT 96
        STUBLESS_CLIENT 97
        STUBLESS_CLIENT 98
        STUBLESS_CLIENT 99
        STUBLESS_CLIENT 100
        STUBLESS_CLIENT 101
        STUBLESS_CLIENT 102
        STUBLESS_CLIENT 103
        STUBLESS_CLIENT 104
        STUBLESS_CLIENT 105
        STUBLESS_CLIENT 106
        STUBLESS_CLIENT 107
        STUBLESS_CLIENT 108
        STUBLESS_CLIENT 109
        STUBLESS_CLIENT 110
        STUBLESS_CLIENT 111
        STUBLESS_CLIENT 112
        STUBLESS_CLIENT 113
        STUBLESS_CLIENT 114
        STUBLESS_CLIENT 115
        STUBLESS_CLIENT 116
        STUBLESS_CLIENT 117
        STUBLESS_CLIENT 118
        STUBLESS_CLIENT 119
        STUBLESS_CLIENT 120
        STUBLESS_CLIENT 121
        STUBLESS_CLIENT 122
        STUBLESS_CLIENT 123
        STUBLESS_CLIENT 124
        STUBLESS_CLIENT 125
        STUBLESS_CLIENT 126
        STUBLESS_CLIENT 127
        STUBLESS_CLIENT 128
        STUBLESS_CLIENT 129
        STUBLESS_CLIENT 130
        STUBLESS_CLIENT 131
        STUBLESS_CLIENT 132
        STUBLESS_CLIENT 133
        STUBLESS_CLIENT 134
        STUBLESS_CLIENT 135
        STUBLESS_CLIENT 136
        STUBLESS_CLIENT 137
        STUBLESS_CLIENT 138
        STUBLESS_CLIENT 139
        STUBLESS_CLIENT 140
        STUBLESS_CLIENT 141
        STUBLESS_CLIENT 142
        STUBLESS_CLIENT 143
        STUBLESS_CLIENT 144
        STUBLESS_CLIENT 145
        STUBLESS_CLIENT 146
        STUBLESS_CLIENT 147
        STUBLESS_CLIENT 148
        STUBLESS_CLIENT 149
        STUBLESS_CLIENT 150
        STUBLESS_CLIENT 151
        STUBLESS_CLIENT 152
        STUBLESS_CLIENT 153
        STUBLESS_CLIENT 154
        STUBLESS_CLIENT 155
        STUBLESS_CLIENT 156
        STUBLESS_CLIENT 157
        STUBLESS_CLIENT 158
        STUBLESS_CLIENT 159
        STUBLESS_CLIENT 160
        STUBLESS_CLIENT 161
        STUBLESS_CLIENT 162
        STUBLESS_CLIENT 163
        STUBLESS_CLIENT 164
        STUBLESS_CLIENT 165
        STUBLESS_CLIENT 166
        STUBLESS_CLIENT 167
        STUBLESS_CLIENT 168
        STUBLESS_CLIENT 169
        STUBLESS_CLIENT 170
        STUBLESS_CLIENT 171
        STUBLESS_CLIENT 172
        STUBLESS_CLIENT 173
        STUBLESS_CLIENT 174
        STUBLESS_CLIENT 175
        STUBLESS_CLIENT 176
        STUBLESS_CLIENT 177
        STUBLESS_CLIENT 178
        STUBLESS_CLIENT 179
        STUBLESS_CLIENT 180
        STUBLESS_CLIENT 181
        STUBLESS_CLIENT 182
        STUBLESS_CLIENT 183
        STUBLESS_CLIENT 184
        STUBLESS_CLIENT 185
        STUBLESS_CLIENT 186
        STUBLESS_CLIENT 187
        STUBLESS_CLIENT 188
        STUBLESS_CLIENT 189
        STUBLESS_CLIENT 190
        STUBLESS_CLIENT 191
        STUBLESS_CLIENT 192
        STUBLESS_CLIENT 193
        STUBLESS_CLIENT 194
        STUBLESS_CLIENT 195
        STUBLESS_CLIENT 196
        STUBLESS_CLIENT 197
        STUBLESS_CLIENT 198
        STUBLESS_CLIENT 199
        STUBLESS_CLIENT 200
        STUBLESS_CLIENT 201
        STUBLESS_CLIENT 202
        STUBLESS_CLIENT 203
        STUBLESS_CLIENT 204
        STUBLESS_CLIENT 205
        STUBLESS_CLIENT 206
        STUBLESS_CLIENT 207
        STUBLESS_CLIENT 208
        STUBLESS_CLIENT 209
        STUBLESS_CLIENT 210
        STUBLESS_CLIENT 211
        STUBLESS_CLIENT 212
        STUBLESS_CLIENT 213
        STUBLESS_CLIENT 214
        STUBLESS_CLIENT 215
        STUBLESS_CLIENT 216
        STUBLESS_CLIENT 217
        STUBLESS_CLIENT 218
        STUBLESS_CLIENT 219
        STUBLESS_CLIENT 220
        STUBLESS_CLIENT 221
        STUBLESS_CLIENT 222
        STUBLESS_CLIENT 223
        STUBLESS_CLIENT 224
        STUBLESS_CLIENT 225
        STUBLESS_CLIENT 226
        STUBLESS_CLIENT 227
        STUBLESS_CLIENT 228
        STUBLESS_CLIENT 229
        STUBLESS_CLIENT 230
        STUBLESS_CLIENT 231
        STUBLESS_CLIENT 232
        STUBLESS_CLIENT 233
        STUBLESS_CLIENT 234
        STUBLESS_CLIENT 235
        STUBLESS_CLIENT 236
        STUBLESS_CLIENT 237
        STUBLESS_CLIENT 238
        STUBLESS_CLIENT 239
        STUBLESS_CLIENT 240
        STUBLESS_CLIENT 241
        STUBLESS_CLIENT 242
        STUBLESS_CLIENT 243
        STUBLESS_CLIENT 244
        STUBLESS_CLIENT 245
        STUBLESS_CLIENT 246
        STUBLESS_CLIENT 247
        STUBLESS_CLIENT 248
        STUBLESS_CLIENT 249
        STUBLESS_CLIENT 250
        STUBLESS_CLIENT 251
        STUBLESS_CLIENT 252
        STUBLESS_CLIENT 253
        STUBLESS_CLIENT 254
        STUBLESS_CLIENT 255
        STUBLESS_CLIENT 256
        STUBLESS_CLIENT 257
        STUBLESS_CLIENT 258
        STUBLESS_CLIENT 259
        STUBLESS_CLIENT 260
        STUBLESS_CLIENT 261
        STUBLESS_CLIENT 262
        STUBLESS_CLIENT 263
        STUBLESS_CLIENT 264
        STUBLESS_CLIENT 265
        STUBLESS_CLIENT 266
        STUBLESS_CLIENT 267
        STUBLESS_CLIENT 268
        STUBLESS_CLIENT 269
        STUBLESS_CLIENT 270
        STUBLESS_CLIENT 271
        STUBLESS_CLIENT 272
        STUBLESS_CLIENT 273
        STUBLESS_CLIENT 274
        STUBLESS_CLIENT 275
        STUBLESS_CLIENT 276
        STUBLESS_CLIENT 277
        STUBLESS_CLIENT 278
        STUBLESS_CLIENT 279
        STUBLESS_CLIENT 280
        STUBLESS_CLIENT 281
        STUBLESS_CLIENT 282
        STUBLESS_CLIENT 283
        STUBLESS_CLIENT 284
        STUBLESS_CLIENT 285
        STUBLESS_CLIENT 286
        STUBLESS_CLIENT 287
        STUBLESS_CLIENT 288
        STUBLESS_CLIENT 289
        STUBLESS_CLIENT 290
        STUBLESS_CLIENT 291
        STUBLESS_CLIENT 292
        STUBLESS_CLIENT 293
        STUBLESS_CLIENT 294
        STUBLESS_CLIENT 295
        STUBLESS_CLIENT 296
        STUBLESS_CLIENT 297
        STUBLESS_CLIENT 298
        STUBLESS_CLIENT 299
        STUBLESS_CLIENT 300
        STUBLESS_CLIENT 301
        STUBLESS_CLIENT 302
        STUBLESS_CLIENT 303
        STUBLESS_CLIENT 304
        STUBLESS_CLIENT 305
        STUBLESS_CLIENT 306
        STUBLESS_CLIENT 307
        STUBLESS_CLIENT 308
        STUBLESS_CLIENT 309
        STUBLESS_CLIENT 310
        STUBLESS_CLIENT 311
        STUBLESS_CLIENT 312
        STUBLESS_CLIENT 313
        STUBLESS_CLIENT 314
        STUBLESS_CLIENT 315
        STUBLESS_CLIENT 316
        STUBLESS_CLIENT 317
        STUBLESS_CLIENT 318
        STUBLESS_CLIENT 319
        STUBLESS_CLIENT 320
        STUBLESS_CLIENT 321
        STUBLESS_CLIENT 322
        STUBLESS_CLIENT 323
        STUBLESS_CLIENT 324
        STUBLESS_CLIENT 325
        STUBLESS_CLIENT 326
        STUBLESS_CLIENT 327
        STUBLESS_CLIENT 328
        STUBLESS_CLIENT 329
        STUBLESS_CLIENT 330
        STUBLESS_CLIENT 331
        STUBLESS_CLIENT 332
        STUBLESS_CLIENT 333
        STUBLESS_CLIENT 334
        STUBLESS_CLIENT 335
        STUBLESS_CLIENT 336
        STUBLESS_CLIENT 337
        STUBLESS_CLIENT 338
        STUBLESS_CLIENT 339
        STUBLESS_CLIENT 340
        STUBLESS_CLIENT 341
        STUBLESS_CLIENT 342
        STUBLESS_CLIENT 343
        STUBLESS_CLIENT 344
        STUBLESS_CLIENT 345
        STUBLESS_CLIENT 346
        STUBLESS_CLIENT 347
        STUBLESS_CLIENT 348
        STUBLESS_CLIENT 349
        STUBLESS_CLIENT 350
        STUBLESS_CLIENT 351
        STUBLESS_CLIENT 352
        STUBLESS_CLIENT 353
        STUBLESS_CLIENT 354
        STUBLESS_CLIENT 355
        STUBLESS_CLIENT 356
        STUBLESS_CLIENT 357
        STUBLESS_CLIENT 358
        STUBLESS_CLIENT 359
        STUBLESS_CLIENT 360
        STUBLESS_CLIENT 361
        STUBLESS_CLIENT 362
        STUBLESS_CLIENT 363
        STUBLESS_CLIENT 364
        STUBLESS_CLIENT 365
        STUBLESS_CLIENT 366
        STUBLESS_CLIENT 367
        STUBLESS_CLIENT 368
        STUBLESS_CLIENT 369
        STUBLESS_CLIENT 370
        STUBLESS_CLIENT 371
        STUBLESS_CLIENT 372
        STUBLESS_CLIENT 373
        STUBLESS_CLIENT 374
        STUBLESS_CLIENT 375
        STUBLESS_CLIENT 376
        STUBLESS_CLIENT 377
        STUBLESS_CLIENT 378
        STUBLESS_CLIENT 379
        STUBLESS_CLIENT 380
        STUBLESS_CLIENT 381
        STUBLESS_CLIENT 382
        STUBLESS_CLIENT 383
        STUBLESS_CLIENT 384
        STUBLESS_CLIENT 385
        STUBLESS_CLIENT 386
        STUBLESS_CLIENT 387
        STUBLESS_CLIENT 388
        STUBLESS_CLIENT 389
        STUBLESS_CLIENT 390
        STUBLESS_CLIENT 391
        STUBLESS_CLIENT 392
        STUBLESS_CLIENT 393
        STUBLESS_CLIENT 394
        STUBLESS_CLIENT 395
        STUBLESS_CLIENT 396
        STUBLESS_CLIENT 397
        STUBLESS_CLIENT 398
        STUBLESS_CLIENT 399
        STUBLESS_CLIENT 400
        STUBLESS_CLIENT 401
        STUBLESS_CLIENT 402
        STUBLESS_CLIENT 403
        STUBLESS_CLIENT 404
        STUBLESS_CLIENT 405
        STUBLESS_CLIENT 406
        STUBLESS_CLIENT 407
        STUBLESS_CLIENT 408
        STUBLESS_CLIENT 409
        STUBLESS_CLIENT 410
        STUBLESS_CLIENT 411
        STUBLESS_CLIENT 412
        STUBLESS_CLIENT 413
        STUBLESS_CLIENT 414
        STUBLESS_CLIENT 415
        STUBLESS_CLIENT 416
        STUBLESS_CLIENT 417
        STUBLESS_CLIENT 418
        STUBLESS_CLIENT 419
        STUBLESS_CLIENT 420
        STUBLESS_CLIENT 421
        STUBLESS_CLIENT 422
        STUBLESS_CLIENT 423
        STUBLESS_CLIENT 424
        STUBLESS_CLIENT 425
        STUBLESS_CLIENT 426
        STUBLESS_CLIENT 427
        STUBLESS_CLIENT 428
        STUBLESS_CLIENT 429
        STUBLESS_CLIENT 430
        STUBLESS_CLIENT 431
        STUBLESS_CLIENT 432
        STUBLESS_CLIENT 433
        STUBLESS_CLIENT 434
        STUBLESS_CLIENT 435
        STUBLESS_CLIENT 436
        STUBLESS_CLIENT 437
        STUBLESS_CLIENT 438
        STUBLESS_CLIENT 439
        STUBLESS_CLIENT 440
        STUBLESS_CLIENT 441
        STUBLESS_CLIENT 442
        STUBLESS_CLIENT 443
        STUBLESS_CLIENT 444
        STUBLESS_CLIENT 445
        STUBLESS_CLIENT 446
        STUBLESS_CLIENT 447
        STUBLESS_CLIENT 448
        STUBLESS_CLIENT 449
        STUBLESS_CLIENT 450
        STUBLESS_CLIENT 451
        STUBLESS_CLIENT 452
        STUBLESS_CLIENT 453
        STUBLESS_CLIENT 454
        STUBLESS_CLIENT 455
        STUBLESS_CLIENT 456
        STUBLESS_CLIENT 457
        STUBLESS_CLIENT 458
        STUBLESS_CLIENT 459
        STUBLESS_CLIENT 460
        STUBLESS_CLIENT 461
        STUBLESS_CLIENT 462
        STUBLESS_CLIENT 463
        STUBLESS_CLIENT 464
        STUBLESS_CLIENT 465
        STUBLESS_CLIENT 466
        STUBLESS_CLIENT 467
        STUBLESS_CLIENT 468
        STUBLESS_CLIENT 469
        STUBLESS_CLIENT 470
        STUBLESS_CLIENT 471
        STUBLESS_CLIENT 472
        STUBLESS_CLIENT 473
        STUBLESS_CLIENT 474
        STUBLESS_CLIENT 475
        STUBLESS_CLIENT 476
        STUBLESS_CLIENT 477
        STUBLESS_CLIENT 478
        STUBLESS_CLIENT 479
        STUBLESS_CLIENT 480
        STUBLESS_CLIENT 481
        STUBLESS_CLIENT 482
        STUBLESS_CLIENT 483
        STUBLESS_CLIENT 484
        STUBLESS_CLIENT 485
        STUBLESS_CLIENT 486
        STUBLESS_CLIENT 487
        STUBLESS_CLIENT 488
        STUBLESS_CLIENT 489
        STUBLESS_CLIENT 490
        STUBLESS_CLIENT 491
        STUBLESS_CLIENT 492
        STUBLESS_CLIENT 493
        STUBLESS_CLIENT 494
        STUBLESS_CLIENT 495
        STUBLESS_CLIENT 496
        STUBLESS_CLIENT 497
        STUBLESS_CLIENT 498
        STUBLESS_CLIENT 499
        STUBLESS_CLIENT 500
        STUBLESS_CLIENT 501
        STUBLESS_CLIENT 502
        STUBLESS_CLIENT 503
        STUBLESS_CLIENT 504
        STUBLESS_CLIENT 505
        STUBLESS_CLIENT 506
        STUBLESS_CLIENT 507
        STUBLESS_CLIENT 508
        STUBLESS_CLIENT 509
        STUBLESS_CLIENT 510
        STUBLESS_CLIENT 511

        END
