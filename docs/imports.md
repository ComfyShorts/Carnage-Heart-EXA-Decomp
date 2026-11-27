# PSP imports: `EBOOT.BIN` (module `chpsp`)

Generated from the module's import stub table (`.lib.stub`, 0x3BF59C-0x3BF7E0) by `tools/dump_imports.py`. NIDs were resolved to names using PPSSPP's HLE tables (`Core/HLE/*.cpp`). All 299 NIDs resolved.

29 libraries, 299 functions, 0 variables imported. The *Stub* column is the address of the 8-byte syscall stub in the stub text sections; these addresses are also in `config/symbol_addrs.txt`.

## Summary

| Subsystem | Library | Funcs |
|---|---|---:|
| Kernel: threads, sync, timing | `ThreadManForUser` | 46 |
| Kernel: threads, sync, timing | `Kernel_Library` | 9 |
| Kernel: threads, sync, timing | `InterruptManager` | 3 |
| Kernel: threads, sync, timing | `sceSuspendForUser` | 3 |
| Kernel: memory, modules, process | `SysMemUserForUser` | 6 |
| Kernel: memory, modules, process | `ModuleMgrForUser` | 7 |
| Kernel: memory, modules, process | `LoadExecForUser` | 2 |
| Kernel: memory, modules, process | `UtilsForUser` | 7 |
| Kernel: memory, modules, process | `StdioForUser` | 3 |
| File I/O and UMD | `IoFileMgrForUser` | 20 |
| File I/O and UMD | `sceUmdUser` | 6 |
| Graphics | `sceGe_user` | 11 |
| Graphics | `sceDisplay` | 6 |
| Graphics | `sceDmac` | 1 |
| Input | `sceCtrl` | 2 |
| Audio | `sceAudio` | 12 |
| Audio | `sceSasCore` | 27 |
| Audio | `sceAtrac3plus` | 11 |
| Audio | `sceMp3` | 13 |
| Video | `sceMpeg` | 25 |
| Networking (ad hoc / WLAN) | `sceNet` | 4 |
| Networking (ad hoc / WLAN) | `sceNetAdhoc` | 16 |
| Networking (ad hoc / WLAN) | `sceNetAdhocctl` | 15 |
| Networking (ad hoc / WLAN) | `sceNetAdhocMatching` | 10 |
| Networking (ad hoc / WLAN) | `sceWlanDrv` | 2 |
| System services | `sceUtility` | 25 |
| System services | `sceImpose` | 2 |
| System services | `scePower` | 4 |
| System services | `sceRtc` | 1 |

## Kernel: threads, sync, timing

### `ThreadManForUser` (flags 0x4001, 46 funcs)

| Stub | NID | Function |
|---|---|---|
| 0x3BED50 | 0xBED27435 | `sceKernelAllocateVpl` |
| 0x3BED80 | 0xEA748E31 | `sceKernelChangeCurrentThreadAttr` |
| 0x3BEE48 | 0x71BC9871 | `sceKernelChangeThreadPriority` |
| 0x3BEDC0 | 0x349D6D6C | `sceKernelCheckCallback` |
| 0x3BEE58 | 0x812346E4 | `sceKernelClearEventFlag` |
| 0x3BED70 | 0xE81CAF8F | `sceKernelCreateCallback` |
| 0x3BEE08 | 0x55C20A00 | `sceKernelCreateEventFlag` |
| 0x3BEEA8 | 0x19CFF145 | `sceKernelCreateLwMutex` |
| 0x3BEE60 | 0x8125221D | `sceKernelCreateMbx` |
| 0x3BEEB8 | 0xB7D098C6 | `sceKernelCreateMutex` |
| 0x3BED60 | 0xD6DA4BA1 | `sceKernelCreateSema` |
| 0x3BEDF0 | 0x446D8DE6 | `sceKernelCreateThread` |
| 0x3BEE10 | 0x56C039B5 | `sceKernelCreateVpl` |
| 0x3BED58 | 0xCEADEB47 | `sceKernelDelayThread` |
| 0x3BEE30 | 0x68DA9E36 | `sceKernelDelayThreadCB` |
| 0x3BED88 | 0xEDBA5844 | `sceKernelDeleteCallback` |
| 0x3BED90 | 0xEF9E4C70 | `sceKernelDeleteEventFlag` |
| 0x3BEE28 | 0x60107536 | `sceKernelDeleteLwMutex` |
| 0x3BEE70 | 0x86255ADA | `sceKernelDeleteMbx` |
| 0x3BEDA0 | 0xF8170FBE | `sceKernelDeleteMutex` |
| 0x3BEDB0 | 0x28B6489C | `sceKernelDeleteSema` |
| 0x3BEE90 | 0x9FA03CD3 | `sceKernelDeleteThread` |
| 0x3BEE78 | 0x89B3D48C | `sceKernelDeleteVpl` |
| 0x3BEE50 | 0x809CE29B | `sceKernelExitDeleteThread` |
| 0x3BEEA0 | 0xAA73C935 | `sceKernelExitThread` |
| 0x3BEEB0 | 0xB736E9FF | `sceKernelFreeVpl` |
| 0x3BEDC8 | 0x369ED59D | `sceKernelGetSystemTimeLow` |
| 0x3BEE68 | 0x82BC5777 | `sceKernelGetSystemTimeWide` |
| 0x3BEE88 | 0x94AA61EE | `sceKernelGetThreadCurrentPriority` |
| 0x3BEDD8 | 0x3B183E26 | `sceKernelGetThreadExitStatus` |
| 0x3BEE00 | 0x52089CA1 | `sceKernelGetThreadStackFreeSize` |
| 0x3BEE20 | 0x5BF4DD27 | `sceKernelLockMutexCB` |
| 0x3BEDD0 | 0x0D81716A | `sceKernelPollMbx` |
| 0x3BEE18 | 0x58B1F937 | `sceKernelPollSema` |
| 0x3BEE80 | 0x18260574 | `sceKernelReceiveMbx` |
| 0x3BEE98 | 0xA66B0120 | `sceKernelReferEventFlagStatus` |
| 0x3BED78 | 0xE9B3061E | `sceKernelSendMbx` |
| 0x3BED68 | 0x1FB15A32 | `sceKernelSetEventFlag` |
| 0x3BEDE0 | 0x3F53E640 | `sceKernelSignalSema` |
| 0x3BED98 | 0xF475845D | `sceKernelStartThread` |
| 0x3BEE38 | 0x6B30100F | `sceKernelUnlockMutex` |
| 0x3BEDE8 | 0x402FCF22 | `sceKernelWaitEventFlag` |
| 0x3BEDB8 | 0x328C546A | `sceKernelWaitEventFlagCB` |
| 0x3BEDF8 | 0x4E3A1105 | `sceKernelWaitSema` |
| 0x3BEE40 | 0x6D212BAC | `sceKernelWaitSemaCB` |
| 0x3BEDA8 | 0x278C0DF5 | `sceKernelWaitThreadEnd` |

### `Kernel_Library` (flags 0x0001, 9 funcs)

| Stub | NID | Function |
|---|---|---|
| 0x3BEF90 | 0x5F10D406 | `sceKernelCpuResumeIntr` |
| 0x3BEF88 | 0x3B84732D | `sceKernelCpuResumeIntrWithSync` |
| 0x3BEF50 | 0x092968F4 | `sceKernelCpuSuspendIntr` |
| 0x3BEF80 | 0x293B45B8 | `sceKernelGetThreadId` |
| 0x3BEF60 | 0xB55249D2 | `sceKernelIsCpuIntrEnable` |
| 0x3BEF68 | 0xBEA46419 | `sceKernelLockLwMutex` |
| 0x3BEF78 | 0x1839852A | `sceKernelMemcpy` |
| 0x3BEF58 | 0xA089ECA4 | `sceKernelMemset` |
| 0x3BEF70 | 0x15B6446B | `sceKernelUnlockLwMutex` |

### `InterruptManager` (flags 0x4000, 3 funcs)

| Stub | NID | Function |
|---|---|---|
| 0x3BF048 | 0xFB8E22EC | `sceKernelEnableSubIntr` |
| 0x3BF038 | 0xCA04A2B9 | `sceKernelRegisterSubIntrHandler` |
| 0x3BF040 | 0xD61E6961 | `sceKernelReleaseSubIntrHandler` |

### `sceSuspendForUser` (flags 0x4000, 3 funcs)

| Stub | NID | Function |
|---|---|---|
| 0x3BED10 | 0xEADB1BD7 | `sceKernelPowerLock` |
| 0x3BED00 | 0x090CCB3F | `sceKernelPowerTick` |
| 0x3BED08 | 0x3AEE7261 | `sceKernelPowerUnlock` |

## Kernel: memory, modules, process

### `SysMemUserForUser` (flags 0x4000, 6 funcs)

| Stub | NID | Function |
|---|---|---|
| 0x3BEED8 | 0x237DBD4F | `sceKernelAllocPartitionMemory` |
| 0x3BEEC8 | 0xB6D61D02 | `sceKernelFreePartitionMemory` |
| 0x3BEEE8 | 0x9D9A5BA1 | `sceKernelGetBlockHeadAddr` |
| 0x3BEEC0 | 0x13A5ABEF | `sceKernelPrintf` |
| 0x3BEEE0 | 0x358CA1BB | `sceKernelSetCompiledSdkVersion606` |
| 0x3BEED0 | 0xF77D77CB | `sceKernelSetCompilerVersion` |

### `ModuleMgrForUser` (flags 0x4001, 7 funcs)

| Stub | NID | Function |
|---|---|---|
| 0x3BEF20 | 0xF0A26395 | `sceKernelGetModuleId` |
| 0x3BEF18 | 0xD8B73127 | `sceKernelGetModuleIdByAddress` |
| 0x3BEF38 | 0x977DE386 | `sceKernelLoadModule` |
| 0x3BEF28 | 0x50F0C1EC | `sceKernelStartModule` |
| 0x3BEF10 | 0xD1FF982A | `sceKernelStopModule` |
| 0x3BEF30 | 0x8F2DF740 | `sceKernelStopUnloadSelfModuleWithStatus` |
| 0x3BEF08 | 0x2E0911AA | `sceKernelUnloadModule` |

### `LoadExecForUser` (flags 0x4001, 2 funcs)

| Stub | NID | Function |
|---|---|---|
| 0x3BEF40 | 0x05572A5F | `sceKernelExitGame` |
| 0x3BEF48 | 0x4AC57943 | `sceKernelRegisterExitCallback` |

### `UtilsForUser` (flags 0x4001, 7 funcs)

| Stub | NID | Function |
|---|---|---|
| 0x3BED20 | 0x79D1C3FA | `sceKernelDcacheWritebackAll` |
| 0x3BED30 | 0xB435DEC5 | `sceKernelDcacheWritebackInvalidateAll` |
| 0x3BED48 | 0x3EE30821 | `sceKernelDcacheWritebackRange` |
| 0x3BED40 | 0x37FB5C42 | `sceKernelGetGPI` |
| 0x3BED28 | 0x91E4F6A7 | `sceKernelLibcClock` |
| 0x3BED18 | 0x71EC4271 | `sceKernelLibcGettimeofday` |
| 0x3BED38 | 0x27CC57F0 | `sceKernelLibcTime` |

### `StdioForUser` (flags 0x4001, 3 funcs)

| Stub | NID | Function |
|---|---|---|
| 0x3BEF00 | 0xF78BA90A | `sceKernelStderr` |
| 0x3BEEF0 | 0x172D316E | `sceKernelStdin` |
| 0x3BEEF8 | 0xA6BAB2E9 | `sceKernelStdout` |

## File I/O and UMD

### `IoFileMgrForUser` (flags 0x4001, 20 funcs)

| Stub | NID | Function |
|---|---|---|
| 0x3BEFF0 | 0xB8A740F4 | `sceIoChstat` |
| 0x3BEFC8 | 0x810C4BC3 | `sceIoClose` |
| 0x3BF010 | 0xFF5940B6 | `sceIoCloseAsync` |
| 0x3BF008 | 0xEB092469 | `sceIoDclose` |
| 0x3BEFA8 | 0x54F5FB11 | `sceIoDevctl` |
| 0x3BEFE8 | 0xB29DDF9C | `sceIoDopen` |
| 0x3BEFF8 | 0xE3EB004C | `sceIoDread` |
| 0x3BEFE0 | 0xACE946E8 | `sceIoGetstat` |
| 0x3BF020 | 0x27EB27B8 | `sceIoLseek` |
| 0x3BEFB8 | 0x71B19E77 | `sceIoLseekAsync` |
| 0x3BEF98 | 0x06A70004 | `sceIoMkdir` |
| 0x3BF018 | 0x109F50BC | `sceIoOpen` |
| 0x3BEFD0 | 0x89AA9906 | `sceIoOpenAsync` |
| 0x3BF028 | 0x3251EA56 | `sceIoPollAsync` |
| 0x3BEFB0 | 0x6A638D83 | `sceIoRead` |
| 0x3BEFD8 | 0xA0B5A7C2 | `sceIoReadAsync` |
| 0x3BEFC0 | 0x779103A0 | `sceIoRename` |
| 0x3BF030 | 0x35DBD746 | `sceIoWaitAsyncCB` |
| 0x3BEFA0 | 0x42EC03AC | `sceIoWrite` |
| 0x3BF000 | 0x0FACAB19 | `sceIoWriteAsync` |

### `sceUmdUser` (flags 0x4001, 6 funcs)

| Stub | NID | Function |
|---|---|---|
| 0x3BF4B0 | 0xC6183D47 | `sceUmdActivate` |
| 0x3BF498 | 0x6AF9B50A | `sceUmdCancelWaitDriveStat` |
| 0x3BF488 | 0x46EBB729 | `sceUmdCheckMedium` |
| 0x3BF4A0 | 0x6B4A146C | `sceUmdGetDriveStat` |
| 0x3BF4A8 | 0x8EF08FCE | `sceUmdWaitDriveStat` |
| 0x3BF490 | 0x4A9E5E29 | `sceUmdWaitDriveStatCB` |

## Graphics

### `sceGe_user` (flags 0x4001, 11 funcs)

| Stub | NID | Function |
|---|---|---|
| 0x3BF448 | 0xB448EC0D | `sceGeBreak` |
| 0x3BF428 | 0x4C06E472 | `sceGeContinue` |
| 0x3BF440 | 0xB287BD61 | `sceGeDrawSync` |
| 0x3BF408 | 0xE47E40E4 | `sceGeEdramGetAddr` |
| 0x3BF400 | 0x1F6752AD | `sceGeEdramGetSize` |
| 0x3BF438 | 0xAB49E76A | `sceGeListEnQueue` |
| 0x3BF420 | 0x1C0D95A6 | `sceGeListEnQueueHead` |
| 0x3BF410 | 0x03444EB4 | `sceGeListSync` |
| 0x3BF450 | 0xE0D68148 | `sceGeListUpdateStallAddr` |
| 0x3BF430 | 0xA4FC06A4 | `sceGeSetCallback` |
| 0x3BF418 | 0x05DB22CE | `sceGeUnsetCallback` |

### `sceDisplay` (flags 0x4001, 6 funcs)

| Stub | NID | Function |
|---|---|---|
| 0x3BF480 | 0xEEDA2E54 | `sceDisplayGetFrameBuf` |
| 0x3BF478 | 0xDBA6C4C4 | `sceDisplayGetFramePerSec` |
| 0x3BF470 | 0x9C6EAAD7 | `sceDisplayGetVcount` |
| 0x3BF460 | 0x289D82FE | `sceDisplaySetFrameBuf` |
| 0x3BF458 | 0x0E20F177 | `sceDisplaySetMode` |
| 0x3BF468 | 0x46F186C3 | `sceDisplayWaitVblankStartCB` |

### `sceDmac` (flags 0x4001, 1 funcs)

| Stub | NID | Function |
|---|---|---|
| 0x3BF318 | 0x617F3FE6 | `sceDmacMemcpy` |

## Input

### `sceCtrl` (flags 0x4001, 2 funcs)

| Stub | NID | Function |
|---|---|---|
| 0x3BF3F8 | 0x3A622550 | `sceCtrlPeekBufferPositive` |
| 0x3BF3F0 | 0x1F4011E6 | `sceCtrlSetSamplingMode` |

## Audio

### `sceAudio` (flags 0x4001, 12 funcs)

| Stub | NID | Function |
|---|---|---|
| 0x3BF170 | 0x6FC46853 | `sceAudioChRelease` |
| 0x3BF168 | 0x5EC81C55 | `sceAudioChReserve` |
| 0x3BF178 | 0x95FD0C2D | `sceAudioChangeChannelConfig` |
| 0x3BF188 | 0xB7E1D8E7 | `sceAudioChangeChannelVolume` |
| 0x3BF180 | 0xB011922F | `sceAudioGetChannelRestLength` |
| 0x3BF148 | 0x2D53F36E | `sceAudioOutput2OutputBlocking` |
| 0x3BF150 | 0x43196845 | `sceAudioOutput2Release` |
| 0x3BF140 | 0x01562BA3 | `sceAudioOutput2Reserve` |
| 0x3BF158 | 0x136CAF51 | `sceAudioOutputBlocking` |
| 0x3BF198 | 0xE2D56B2D | `sceAudioOutputPanned` |
| 0x3BF160 | 0x13F592BC | `sceAudioOutputPannedBlocking` |
| 0x3BF190 | 0xCB2E439E | `sceAudioSetChannelDataLen` |

### `sceSasCore` (flags 0x4009, 27 funcs)

| Stub | NID | Function |
|---|---|---|
| 0x3BECA8 | 0xA3589D81 | `__sceSasCore` |
| 0x3BEC60 | 0x50A14DFC | `__sceSasCoreWithMix` |
| 0x3BEC30 | 0x07F58C24 | `__sceSasGetAllEnvelopeHeights` |
| 0x3BEC70 | 0x68A46B95 | `__sceSasGetEndFlag` |
| 0x3BEC78 | 0x74AE582A | `__sceSasGetEnvelopeHeight` |
| 0x3BECC0 | 0xBD11B7C2 | `__sceSasGetGrain` |
| 0x3BECE0 | 0xE175EF66 | `__sceSasGetOutputmode` |
| 0x3BEC40 | 0x2C8E6AB3 | `__sceSasGetPauseFlag` |
| 0x3BEC50 | 0x42778A9F | `__sceSasInit` |
| 0x3BECD8 | 0xD5A229C9 | `__sceSasRevEVOL` |
| 0x3BEC38 | 0x267A6DD2 | `__sceSasRevParam` |
| 0x3BEC48 | 0x33D4AB37 | `__sceSasRevType` |
| 0x3BECF8 | 0xF983B186 | `__sceSasRevVON` |
| 0x3BEC28 | 0x019B25EB | `__sceSasSetADSR` |
| 0x3BEC98 | 0x9EC3676A | `__sceSasSetADSRmode` |
| 0x3BECD0 | 0xD1E0A01E | `__sceSasSetGrain` |
| 0x3BECA0 | 0xA0CF2FA4 | `__sceSasSetKeyOff` |
| 0x3BEC80 | 0x76F01ACA | `__sceSasSetKeyOn` |
| 0x3BECB8 | 0xB7660A23 | `__sceSasSetNoise` |
| 0x3BECF0 | 0xE855BF76 | `__sceSasSetOutputmode` |
| 0x3BEC88 | 0x787D04D5 | `__sceSasSetPause` |
| 0x3BECB0 | 0xAD84D37F | `__sceSasSetPitch` |
| 0x3BEC68 | 0x5F9529F6 | `__sceSasSetSL` |
| 0x3BECC8 | 0xCBCD4F79 | `__sceSasSetSimpleADSR` |
| 0x3BEC90 | 0x99944089 | `__sceSasSetVoice` |
| 0x3BECE8 | 0xE1CD9561 | `__sceSasSetVoicePCM` |
| 0x3BEC58 | 0x440CA7D8 | `__sceSasSetVolume` |

### `sceAtrac3plus` (flags 0x0009, 11 funcs)

| Stub | NID | Function |
|---|---|---|
| 0x3BF110 | 0x7DB31251 | `sceAtracAddStreamData` |
| 0x3BF100 | 0x6A8C3CD5 | `sceAtracDecodeData` |
| 0x3BF130 | 0xE88F759B | `sceAtracGetInternalErrorInfo` |
| 0x3BF120 | 0x9AE849A7 | `sceAtracGetRemainFrame` |
| 0x3BF128 | 0xA2BBA8BE | `sceAtracGetSoundSample` |
| 0x3BF0F0 | 0x5D268707 | `sceAtracGetStreamDataInfo` |
| 0x3BF138 | 0xECA32A99 | `sceAtracIsSecondBufferNeeded` |
| 0x3BF0E8 | 0x132F1ECA | `sceAtracReinit` |
| 0x3BF0F8 | 0x61EB33F5 | `sceAtracReleaseAtracID` |
| 0x3BF108 | 0x7A20E7AF | `sceAtracSetDataAndGetID` |
| 0x3BF118 | 0x868120B5 | `sceAtracSetLoopNum` |

### `sceMp3` (flags 0x0009, 13 funcs)

| Stub | NID | Function |
|---|---|---|
| 0x3BF0A8 | 0xD0A56296 | `sceMp3CheckStreamDataNeeded` |
| 0x3BF0A0 | 0xD021C0FB | `sceMp3Decode` |
| 0x3BF080 | 0x87677E40 | `sceMp3GetBitRate` |
| 0x3BF098 | 0xA703FE0F | `sceMp3GetInfoToAddStreamData` |
| 0x3BF088 | 0x87C263D1 | `sceMp3GetMaxOutputSample` |
| 0x3BF078 | 0x7F696782 | `sceMp3GetMp3ChannelNum` |
| 0x3BF090 | 0x8F450998 | `sceMp3GetSamplingRate` |
| 0x3BF070 | 0x44E07129 | `sceMp3Init` |
| 0x3BF060 | 0x35750070 | `sceMp3InitResource` |
| 0x3BF058 | 0x0DB149F4 | `sceMp3NotifyAddStreamData` |
| 0x3BF0B0 | 0xF5478233 | `sceMp3ReleaseMp3Handle` |
| 0x3BF050 | 0x07EC321A | `sceMp3ReserveMp3Handle` |
| 0x3BF068 | 0x3CEF484F | `sceMp3SetLoopNum` |

## Video

### `sceMpeg` (flags 0x0009, 25 funcs)

| Stub | NID | Function |
|---|---|---|
| 0x3BF388 | 0x800C44DF | `sceMpegAtracDecode` |
| 0x3BF320 | 0x0E3C2E9D | `sceMpegAvcDecode` |
| 0x3BF350 | 0x4571CC64 | `sceMpegAvcDecodeFlush` |
| 0x3BF380 | 0x740FCCD1 | `sceMpegAvcDecodeStop` |
| 0x3BF3C8 | 0xD8C5F121 | `sceMpegCreate` |
| 0x3BF360 | 0x606A4649 | `sceMpegDelete` |
| 0x3BF390 | 0x874624D6 | `sceMpegFinish` |
| 0x3BF378 | 0x707B7629 | `sceMpegFlushAllStream` |
| 0x3BF3B8 | 0xCEB870B1 | `sceMpegFreeAvcEsBuf` |
| 0x3BF3D0 | 0xE1CE83A7 | `sceMpegGetAtracAu` |
| 0x3BF3E0 | 0xFE246728 | `sceMpegGetAvcAu` |
| 0x3BF370 | 0x682A619B | `sceMpegInit` |
| 0x3BF330 | 0x167AFD9E | `sceMpegInitAu` |
| 0x3BF398 | 0xA780CF7E | `sceMpegMallocAvcEsBuf` |
| 0x3BF3D8 | 0xF8DCB679 | `sceMpegQueryAtracEsSize` |
| 0x3BF3B0 | 0xC132E22F | `sceMpegQueryMemSize` |
| 0x3BF338 | 0x21FF80E4 | `sceMpegQueryStreamOffset` |
| 0x3BF368 | 0x611E9E11 | `sceMpegQueryStreamSize` |
| 0x3BF348 | 0x42560F23 | `sceMpegRegistStream` |
| 0x3BF3A8 | 0xB5F6DC87 | `sceMpegRingbufferAvailableSize` |
| 0x3BF340 | 0x37295ED8 | `sceMpegRingbufferConstruct` |
| 0x3BF328 | 0x13407F13 | `sceMpegRingbufferDestruct` |
| 0x3BF3A0 | 0xB240A59E | `sceMpegRingbufferPut` |
| 0x3BF3C0 | 0xD7A29F46 | `sceMpegRingbufferQueryMemSize` |
| 0x3BF358 | 0x591A4AA2 | `sceMpegUnRegistStream` |

## Networking (ad hoc / WLAN)

### `sceNet` (flags 0x0009, 4 funcs)

| Stub | NID | Function |
|---|---|---|
| 0x3BF2F8 | 0x89360950 | `sceNetEtherNtostr` |
| 0x3BF300 | 0xCC393E48 | `sceNetGetMallocStat` |
| 0x3BF2F0 | 0x39AF39A6 | `sceNetInit` |
| 0x3BF2E8 | 0x281928A9 | `sceNetTerm` |

### `sceNetAdhoc` (flags 0x0009, 16 funcs)

| Stub | NID | Function |
|---|---|---|
| 0x3BF2C0 | 0xB9685118 | `sceNetAdhocGetPtpStat` |
| 0x3BF2D8 | 0xE1D621D7 | `sceNetAdhocInit` |
| 0x3BF278 | 0x6F92741B | `sceNetAdhocPdpCreate` |
| 0x3BF288 | 0x7F27BB5E | `sceNetAdhocPdpDelete` |
| 0x3BF2C8 | 0xDFE53E03 | `sceNetAdhocPdpRecv` |
| 0x3BF2B8 | 0xABED3790 | `sceNetAdhocPdpSend` |
| 0x3BF280 | 0x7A662D6B | `sceNetAdhocPollSocket` |
| 0x3BF2A8 | 0x9DF81198 | `sceNetAdhocPtpAccept` |
| 0x3BF268 | 0x157E6225 | `sceNetAdhocPtpClose` |
| 0x3BF2E0 | 0xFC6FC07B | `sceNetAdhocPtpConnect` |
| 0x3BF2A0 | 0x9AC2EEAC | `sceNetAdhocPtpFlush` |
| 0x3BF2D0 | 0xE08BDAC1 | `sceNetAdhocPtpListen` |
| 0x3BF290 | 0x877F6D66 | `sceNetAdhocPtpOpen` |
| 0x3BF298 | 0x8BEA2B3E | `sceNetAdhocPtpRecv` |
| 0x3BF270 | 0x4DA4C788 | `sceNetAdhocPtpSend` |
| 0x3BF2B0 | 0xA62C6F57 | `sceNetAdhocTerm` |

### `sceNetAdhocctl` (flags 0x0009, 15 funcs)

| Stub | NID | Function |
|---|---|---|
| 0x3BF200 | 0x20B317A0 | `sceNetAdhocctlAddHandler` |
| 0x3BF1F8 | 0x0AD043ED | `sceNetAdhocctlConnect` |
| 0x3BF220 | 0x6402490B | `sceNetAdhocctlDelHandler` |
| 0x3BF208 | 0x34401D65 | `sceNetAdhocctlDisconnect` |
| 0x3BF210 | 0x362CBE8F | `sceNetAdhocctlGetAdhocId` |
| 0x3BF238 | 0x8916C003 | `sceNetAdhocctlGetNameByAddr` |
| 0x3BF250 | 0xDED9D28E | `sceNetAdhocctlGetParameter` |
| 0x3BF240 | 0x8DB83FDC | `sceNetAdhocctlGetPeerInfo` |
| 0x3BF258 | 0xE162CB14 | `sceNetAdhocctlGetPeerList` |
| 0x3BF230 | 0x81AEE1BE | `sceNetAdhocctlGetScanInfo` |
| 0x3BF228 | 0x75ECD386 | `sceNetAdhocctlGetState` |
| 0x3BF260 | 0xE26F226E | `sceNetAdhocctlInit` |
| 0x3BF218 | 0x5E7F79C9 | `sceNetAdhocctlJoin` |
| 0x3BF1F0 | 0x08FFF7A0 | `sceNetAdhocctlScan` |
| 0x3BF248 | 0x9D689E13 | `sceNetAdhocctlTerm` |

### `sceNetAdhocMatching` (flags 0x0009, 10 funcs)

| Stub | NID | Function |
|---|---|---|
| 0x3BF1E0 | 0xEA3C6108 | `sceNetAdhocMatchingCancelTarget` |
| 0x3BF1C0 | 0x8F58BEDF | `sceNetAdhocMatchingCancelTargetWithOpt` |
| 0x3BF1D8 | 0xCA5EDA6F | `sceNetAdhocMatchingCreate` |
| 0x3BF1E8 | 0xF16EAF4F | `sceNetAdhocMatchingDelete` |
| 0x3BF1A0 | 0x2A2A1E07 | `sceNetAdhocMatchingInit` |
| 0x3BF1B0 | 0x5E3D4B79 | `sceNetAdhocMatchingSelectTarget` |
| 0x3BF1D0 | 0xB58E61B7 | `sceNetAdhocMatchingSetHelloOpt` |
| 0x3BF1C8 | 0x93EF3843 | `sceNetAdhocMatchingStart` |
| 0x3BF1A8 | 0x32B156B3 | `sceNetAdhocMatchingStop` |
| 0x3BF1B8 | 0x7945ECDA | `sceNetAdhocMatchingTerm` |

### `sceWlanDrv` (flags 0x4001, 2 funcs)

| Stub | NID | Function |
|---|---|---|
| 0x3BF308 | 0x0C622081 | `sceWlanGetEtherAddr` |
| 0x3BF310 | 0xD7763699 | `sceWlanGetSwitchState` |

## System services

### `sceUtility` (flags 0x4001, 25 funcs)

| Stub | NID | Function |
|---|---|---|
| 0x3BF538 | 0xA5DA2406 | `sceUtilityGetSystemParamInt` |
| 0x3BF4D0 | 0x34B78343 | `sceUtilityGetSystemParamString` |
| 0x3BF4C0 | 0x2A2B3DE0 | `sceUtilityLoadModule` |
| 0x3BF530 | 0x9A1C91D7 | `sceUtilityMsgDialogGetStatus` |
| 0x3BF4C8 | 0x2AD8E239 | `sceUtilityMsgDialogInitStart` |
| 0x3BF500 | 0x67AF3428 | `sceUtilityMsgDialogShutdownStart` |
| 0x3BF520 | 0x95FC253B | `sceUtilityMsgDialogUpdate` |
| 0x3BF4F8 | 0x6332AA39 | `sceUtilityNetconfGetStatus` |
| 0x3BF4E8 | 0x4DB1E739 | `sceUtilityNetconfInitStart` |
| 0x3BF570 | 0xF88155F6 | `sceUtilityNetconfShutdownStart` |
| 0x3BF518 | 0x91E70E35 | `sceUtilityNetconfUpdate` |
| 0x3BF560 | 0xF3F76017 | `sceUtilityOskGetStatus` |
| 0x3BF568 | 0xF6269B82 | `sceUtilityOskInitStart` |
| 0x3BF4D8 | 0x3DFAEBA9 | `sceUtilityOskShutdownStart` |
| 0x3BF4E0 | 0x4B85C861 | `sceUtilityOskUpdate` |
| 0x3BF510 | 0x8874DBE0 | `sceUtilitySavedataGetStatus` |
| 0x3BF4F0 | 0x50C4CD57 | `sceUtilitySavedataInitStart` |
| 0x3BF528 | 0x9790B33C | `sceUtilitySavedataShutdownStart` |
| 0x3BF548 | 0xD4B95FFB | `sceUtilitySavedataUpdate` |
| 0x3BF508 | 0x86A03A27 | `sceUtilityScreenshotContStart` |
| 0x3BF550 | 0xD81957B7 | `sceUtilityScreenshotGetStatus` |
| 0x3BF4B8 | 0x0251B134 | `sceUtilityScreenshotInitStart` |
| 0x3BF578 | 0xF9E0008C | `sceUtilityScreenshotShutdownStart` |
| 0x3BF540 | 0xAB083EA9 | `sceUtilityScreenshotUpdate` |
| 0x3BF558 | 0xE49BFE92 | `sceUtilityUnloadModule` |

### `sceImpose` (flags 0x4001, 2 funcs)

| Stub | NID | Function |
|---|---|---|
| 0x3BF0E0 | 0x967F6D4A | `sceImposeSetBacklightOffTime` |
| 0x3BF0D8 | 0x36AA6E91 | `sceImposeSetLanguageMode` |

### `scePower` (flags 0x4001, 4 funcs)

| Stub | NID | Function |
|---|---|---|
| 0x3BF0C8 | 0xA85880D0 | `scePowerCheckWlanCoexistenceClock` |
| 0x3BF0B8 | 0x04B7766E | `scePowerRegisterCallback` |
| 0x3BF0C0 | 0x469989AD | `scePowerSetClockFrequency630` |
| 0x3BF0D0 | 0xDFA8BAF8 | `scePowerUnregisterCallback` |

### `sceRtc` (flags 0x4001, 1 funcs)

| Stub | NID | Function |
|---|---|---|
| 0x3BF3E8 | 0x4CFA57B0 | `sceRtcGetCurrentClock` |

## Compiler identification

There is **no GCC version string** in the ELF or the encrypted `EBOOT.BIN`: no `GCC: (GNU) x.y.z`, no `.comment` section (all section names are zeroed in the decrypted ELF anyway), and no libstdc++/newlib/pspsdk strings.

The evidence points to **SN Systems SNC** (the ProDG PSP compiler, EDG front end), not GCC:

- 901 `__PRETTY_FUNCTION__`-style strings spell types the EDG way, e.g. `void CBaseRecordDataBase::AddBattleCount(signed int)`. GCC would print `int`, not `signed int`.
- Source paths in assert strings are Windows paths (`c:/project/pspdev/chee/program/src/...`), which fits the official Windows SDK toolchain.
- The import stubs are spread over 29 small `AX` sections (one per library, sections 3-31), not one merged `.sceStub.text` as in typical homebrew pspsdk/GCC output. This fits an official-SDK build, but on its own it does not prove which compiler was used.

So a matching build would need SNC. This project is non-matching, so that doesn't block anything.
