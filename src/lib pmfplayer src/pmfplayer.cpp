/*
 *	PMF Player Module
 *	Copyright (c) 2006 by Sorin P. C. <magik@hypermagik.com>
 *	Modified by Human-Behind
 */
#include <pspkernel.h>
#include <pspsdk.h>
#include "pspmpeg.h"

#include <stdio.h>
#include <malloc.h>
#include <string.h>

#include "ctrl_video.h"

#define DEBUG_PRINTF(fmt, ...)

extern int stop;

CPMFPlayer::CPMFPlayer(void)
{
	m_RingbufferData	= NULL;
	m_MpegMemData		= NULL;

	m_FileHandle		= -1;

	m_MpegStreamAVC		= NULL;
	m_MpegStreamAtrac	= NULL;

	m_pEsBufferAVC		= NULL;
	m_pEsBufferAtrac	= NULL;

	stop = 0;
}

CPMFPlayer::~CPMFPlayer(void)
{
}

char *CPMFPlayer::GetLastError()
{
	return m_LastError;
}

SceInt32 RingbufferCallback(ScePVoid pData, SceInt32 iNumPackets, ScePVoid pParam)
{
	int retVal, iPackets;
	SceUID hFile = *(SceUID*)pParam;

	retVal = sceIoRead(hFile, pData, iNumPackets * 2048);
	if(retVal < 0)
		return -1;

	iPackets = retVal / 2048;

	return iPackets;
}

SceInt32 CPMFPlayer::Initialize(SceInt32 nPackets)
{
	int retVal = -1;

	stop = 0;

	m_RingbufferPackets = nPackets;

	retVal = sceMpegInit();
	if(retVal != 0)
	{
		sprintf(m_LastError, "sceMpegInit() failed: 0x%08X", retVal);
		goto error;
	}

	retVal = sceMpegRingbufferQueryMemSize(m_RingbufferPackets);
	if(retVal < 0)
	{
		sprintf(m_LastError, "sceMpegRingbufferQueryMemSize(%d) failed: 0x%08X", (int)nPackets, retVal);
		goto finish;
	}

	m_RingbufferSize = retVal;

	retVal = sceMpegQueryMemSize(0);
	if(retVal < 0)
	{
		sprintf(m_LastError, "sceMpegQueryMemSize() failed: 0x%08X", retVal);
		goto finish;
	}

	m_MpegMemSize = retVal;

	m_RingbufferData = malloc(m_RingbufferSize);
	if(m_RingbufferData == NULL)
	{
		sprintf(m_LastError, "malloc() failed!");
		goto finish;
	}

	m_MpegMemData = malloc(m_MpegMemSize);
	if(m_MpegMemData == NULL)
	{
		sprintf(m_LastError, "malloc() failed!");
		goto freeringbuffer;
	}

	retVal = sceMpegRingbufferConstruct(&m_Ringbuffer, m_RingbufferPackets, m_RingbufferData, m_RingbufferSize, &RingbufferCallback, &m_FileHandle);
	if(retVal != 0)
	{
		sprintf(m_LastError, "sceMpegRingbufferConstruct() failed: 0x%08X", retVal);
		goto freempeg;
	}

	retVal = sceMpegCreate(&m_Mpeg, m_MpegMemData, m_MpegMemSize, &m_Ringbuffer, BUFFER_WIDTH, 0, 0);
	if(retVal != 0)
	{
		sprintf(m_LastError, "sceMpegCreate() failed: 0x%08X", retVal);
		goto destroyringbuffer;
	}

	SceMpegAvcMode m_MpegAvcMode;
	m_MpegAvcMode.iUnk0 = -1;
	m_MpegAvcMode.iUnk1 = 3;

	sceMpegAvcDecodeMode(&m_Mpeg, &m_MpegAvcMode);

	return 0;

destroyringbuffer:
	sceMpegRingbufferDestruct(&m_Ringbuffer);

freempeg:
	free(m_MpegMemData);

freeringbuffer:
	free(m_RingbufferData);

finish:
	sceMpegFinish();

error:
	return -1;
}

SceInt32 CPMFPlayer::Load(const char* pFileName)
{
	int retVal = 0;
	int i = 0;	// AHMAN

	// AHMAN
	if (pFileName[0] == '1')
		m_PmfScaling = 1;
	else
		m_PmfScaling = 0;
	m_FileHandle = sceIoOpen(pFileName+1, PSP_O_RDONLY, 0777);
	if(m_FileHandle < 0)
	{
		sprintf(m_LastError, "sceIoOpen() failed!");
		return -1;
	}

	if(ParseHeader() < 0)
		return -1;

	m_MpegStreamAVC = sceMpegRegistStream(&m_Mpeg, 0, 0);
	if(m_MpegStreamAVC == NULL)
	{
		sprintf(m_LastError, "sceMpegRegistStream() failed!");
		return -1;
	}

	// AHMAN
	if (m_AudioStreamExist) {
		m_MpegStreamAtrac = sceMpegRegistStream(&m_Mpeg, 1, 0);
		if(m_MpegStreamAtrac == NULL)
		{
			sprintf(m_LastError, "sceMpegRegistStream() failed!");
			return -1;
		}
	} else {
		m_MpegStreamAtrac = NULL;
		m_MpegAtracEsSize = 0;
		m_MpegAtracOutSize = 0;
		m_pEsBufferAtrac = NULL;
		Audio.m_AudioChannel = 0;
		Audio.m_ThreadID = 0;
		Audio.m_SemaphoreStart = 0;
		Audio.m_SemaphoreLock = 0;
		Audio.m_iNumBuffers = 0;
		Audio.m_iFullBuffers = 0;
		Audio.m_iPlayBuffer = 0;
		Audio.m_iDecodeBuffer = 0;
		Audio.m_iAbort = 0;
		Audio.m_LastError = 0;
		for(i = 0; i < 4; i++)
		{
			Audio.m_pAudioBuffer[i] = NULL;
			Audio.m_iBufferTimeStamp[i] = 0;
		}
	}

	m_pEsBufferAVC = sceMpegMallocAvcEsBuf(&m_Mpeg);
	if(m_pEsBufferAVC == NULL)
	{
		sprintf(m_LastError, "sceMpegMallocAvcEsBuf() failed!");
		return -1;
	}

	retVal = sceMpegInitAu(&m_Mpeg, m_pEsBufferAVC, &m_MpegAuAVC);
	if(retVal != 0)
	{
		sprintf(m_LastError, "sceMpegInitAu() failed: 0x%08X", retVal);
		return -1;
	}

	// AHMAN
	if (m_AudioStreamExist) {
		retVal = sceMpegQueryAtracEsSize(&m_Mpeg, &m_MpegAtracEsSize, &m_MpegAtracOutSize);
		if(retVal != 0)
		{
			sprintf(m_LastError, "sceMpegQueryAtracEsSize() failed: 0x%08X", retVal);
			return -1;
		}

		m_pEsBufferAtrac = memalign(64, m_MpegAtracEsSize);
		if(m_pEsBufferAtrac == NULL)
		{
			sprintf(m_LastError, "malloc() failed!");
			return -1;
		}

		retVal = sceMpegInitAu(&m_Mpeg, m_pEsBufferAtrac, &m_MpegAuAtrac);
		if(retVal != 0)
		{
			sprintf(m_LastError, "sceMpegInitAu() failed: 0x%08X", retVal);
			return -1;
		}
	}
	return 0;
}

SceInt32 CPMFPlayer::ParseHeader()
{
	int retVal = 0;
	char * pHeader = new char[2048];

	sceIoLseek(m_FileHandle, 0, SEEK_SET);

	retVal = sceIoRead(m_FileHandle, pHeader, 2048);
	if(retVal < 2048)
	{
		sprintf(m_LastError, "sceIoRead() failed!");
		goto error;
	}

	// AHMAN
	if (pHeader[0x81] == 0x01)
		m_AudioStreamExist = 0;
	else
		m_AudioStreamExist = 1;
	m_MovieWidth = ((unsigned int) pHeader[0x8E]) * 0x10;
	m_MovieHeight = ((unsigned int) pHeader[0x8F]) * 0x10;

	retVal = sceMpegQueryStreamOffset(&m_Mpeg, pHeader, &m_MpegStreamOffset);
	if(retVal != 0)
	{
		sprintf(m_LastError, "sceMpegQueryStreamOffset() failed: 0x%08X", retVal);
		goto error;
	}

	retVal = sceMpegQueryStreamSize(pHeader, &m_MpegStreamSize);
	if(retVal != 0)
	{
		sprintf(m_LastError, "sceMpegQueryStreamSize() failed: 0x%08X", retVal);
		goto error;
	}

	m_iLastTimeStamp = *(int*)(pHeader + 80 + 12);
	m_iLastTimeStamp = SWAPINT(m_iLastTimeStamp);

	delete[] pHeader;

	sceIoLseek(m_FileHandle, m_MpegStreamOffset, SEEK_SET);

	return 0;

error:
	delete[] pHeader;
	return -1;
}

SceVoid CPMFPlayer::Shutdown()
{
	/*Human-Behind*/
	ShutdownReader();
	ShutdownVideo();
	ShutdownAudio();
	ShutdownDecoder();

	free(m_pEsBufferAtrac);

	sceMpegFreeAvcEsBuf(&m_Mpeg, m_pEsBufferAVC);

	sceMpegUnRegistStream(&m_Mpeg, m_MpegStreamAVC);

	sceMpegUnRegistStream(&m_Mpeg, m_MpegStreamAtrac);

	sceIoClose(m_FileHandle);

	sceMpegDelete(&m_Mpeg);

	sceMpegRingbufferDestruct(&m_Ringbuffer);

	free(m_MpegMemData);
	free(m_RingbufferData);
	sceMpegFinish();
}

SceInt32 CPMFPlayer::Play()
{
	int retVal = 0, fail = 0;


	retVal = InitReader();
	if(retVal < 0)
	{
	fail++;
	ShutdownReader();
	}
	

	if (fail == 0)
	{
	retVal = InitVideo();
		if(retVal < 0)
		{

		fail++;
		ShutdownVideo();
		}
	}

	if (fail == 0)
	{// AHMAN

		if (m_AudioStreamExist)
		{
		retVal = InitAudio();
			if(retVal < 0)
			{

			fail++;
			ShutdownAudio();
			}
		}
	}


	if (fail == 0)
	{

	retVal = InitDecoder();
		if(retVal < 0)
		{
		fail++;
		ShutdownDecoder();
		}
	}

	if (fail == 0)
	{
	ReaderThreadData* TDR = &Reader;
	DecoderThreadData* TDD = &Decoder;

	sceKernelStartThread(Reader.m_ThreadID,  sizeof(void*), &TDR);
	sceKernelStartThread(Audio.m_ThreadID,   sizeof(void*), &TDD);
	sceKernelStartThread(Video.m_ThreadID,   sizeof(void*), &TDD);
	sceKernelStartThread(Decoder.m_ThreadID, sizeof(void*), &TDD);

	sceKernelWaitThreadEnd(Decoder.m_ThreadID, 0);
	sceKernelWaitThreadEnd(Video.m_ThreadID, 0);
	sceKernelWaitThreadEnd(Audio.m_ThreadID, 0);
	sceKernelWaitThreadEnd(Reader.m_ThreadID, 0);
	//Human-Behind
	sceKernelTerminateThread(Reader.m_ThreadID);
	sceKernelTerminateThread(Audio.m_ThreadID);
	sceKernelTerminateThread(Video.m_ThreadID);
	sceKernelTerminateThread(Decoder.m_ThreadID);
	}
	if(fail > 0) return -1;

	return 0;
}

void CPMFPlayer::play_pmf(const char *pathfile)
{
Initialize();
Load(pathfile);
Play();
Shutdown();
}
