/*
    vgmwrite.c

    VGM output module

    Written by Valley Bell

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/nestypes.h"
#include "src/nezplug.h"
#include "src/ui/version.h"

#include "vgmwrite.h"


typedef Uint8	UINT8;
typedef Int8	INT8;
typedef Uint16	UINT16;
typedef Int16	INT16;
typedef Uint32	UINT32;
typedef Int32	INT32;
#define INLINE	static __inline


//#define DYNAMIC_HEADER_SIZE

int vgmlog_enable = 0;
static char LOG_IS_INIT = 0x00;
static char LOG_VGM_FILE = 0x02;	// 2 = not initialized
static char LOG_IS_RUNNING = 0x00;

typedef struct _vgm_file_header VGM_HEADER;
struct _vgm_file_header
{
	UINT32 fccVGM;
	UINT32 lngEOFOffset;
	UINT32 lngVersion;
	UINT32 lngHzPSG;
	UINT32 lngHz2413;
	UINT32 lngGD3Offset;
	UINT32 lngTotalSamples;
	UINT32 lngLoopOffset;
	UINT32 lngLoopSamples;
	UINT32 lngRate;
	UINT16 shtPSG_Feedback;
	UINT8 bytPSG_SRWidth;
	UINT8 bytPSG_Flags;
	UINT32 lngHz2612;
	UINT32 lngHz2151;
	UINT32 lngDataOffset;
	UINT32 lngHzSPCM;
	UINT32 lngSPCMIntf;
	UINT32 lngHzRF5C68;
	UINT32 lngHz2203;
	UINT32 lngHz2608;
	UINT32 lngHz2610;
	UINT32 lngHz3812;
	UINT32 lngHz3526;
	UINT32 lngHz8950;
	UINT32 lngHz262;
	UINT32 lngHz278B;
	UINT32 lngHz271;
	UINT32 lngHz280B;
	UINT32 lngHzRF5C164;
	UINT32 lngHzPWM;
	UINT32 lngHzAY8910;
	UINT8 lngAYType;
	UINT8 lngAYFlags;
	UINT8 lngAYFlagsYM2203;
	UINT8 lngAYFlagsYM2608;
	UINT8 bytModifiers[0x04];
	UINT32 lngHzGBDMG;	// part of the LR35902 (GB Main CPU)
	UINT32 lngHzNESAPU;	// part of the N2A03 (NES Main CPU)
	UINT32 lngHzMultiPCM;
	UINT32 lngHzUPD7759;
	UINT32 lngHzOKIM6258;
	UINT8 bytOKI6258Flags;
	UINT8 bytK054539Flags;
	UINT8 bytC140Type;
	UINT8 bytReservedFlags;
	UINT32 lngHzOKIM6295;
	UINT32 lngHzK051649;
	UINT32 lngHzK054539;
	UINT32 lngHzHuC6280;
	UINT32 lngHzC140;
	UINT32 lngHzK053260;
	UINT32 lngHzPokey;
	UINT32 lngHzQSound;
	UINT8 bytReserved[0x08];
};	// -> 0xC0 Bytes
typedef struct _vgm_gd3_tag GD3_TAG;
struct _vgm_gd3_tag
{
	UINT32 fccGD3;
	UINT32 lngVersion;
	UINT32 lngTagLength;
	wchar_t strTrackNameE[0x7F];
	wchar_t strTrackNameJ[0x01];	// Japanese Names are not used
	wchar_t strGameNameE[0x7F];
	wchar_t strGameNameJ[0x01];
	wchar_t strSystemNameE[0x3F];
	wchar_t strSystemNameJ[0x01];
	wchar_t strAuthorNameE[0x3F];
	wchar_t strAuthorNameJ[0x01];
	wchar_t strReleaseDate[0x10];
	wchar_t strCreator[0x20];
	wchar_t strNotes[0x50];
};	// -> 0x200 Bytes

typedef struct _vgm_rom_data_block VGM_ROM_DATA;
struct _vgm_rom_data_block
{
	UINT8 Type;
	UINT32 DataSize;
	UINT32 Offset;
	UINT32 ROMSize;
	const void* Data;
};
typedef struct _vgm_rom_init_command VGM_INIT_CMD;
struct _vgm_rom_init_command
{
	UINT8 CmdLen;
	UINT8 Data[0x08];
};
typedef struct _vgm_file_inf VGM_INF;
struct _vgm_file_inf
{
	UINT8 InUse;
	FILE* hFile;
	VGM_HEADER Header;
	UINT32 HeaderBytes;
	UINT32 BytesWrt;
	UINT32 SmplsWrt;
	UINT32 EvtDelay;
	UINT32 DataCount;
	VGM_ROM_DATA* DataBlk;
	UINT32 CmdAlloc;
	UINT32 CmdCount;
	VGM_INIT_CMD* Commands;
};
typedef struct _vgm_chip VGM_CHIP;
struct _vgm_chip
{
	UINT8 ChipType;
	UINT16 VgmID;
	UINT8 HadWrite;
};


#define MAX_VGM_FILES	0x01
#define MAX_VGM_CHIPS	0x10
static char vgm_basepath[0x400];
static VGM_INF VgmFile[MAX_VGM_FILES];
static VGM_CHIP VgmChip[MAX_VGM_CHIPS];
static GD3_TAG VgmTag;
static UINT8 VgmFilesUsed;



// Function Prototypes
INLINE int atwcpy(wchar_t* dststr, const char* srcstr);
static void vgm_header_sizecheck(UINT16 vgm_id, UINT32 MinVer, UINT32 MinSize);
static void vgm_header_clear(UINT16 vgm_id);
void vgm_close(UINT16 vgm_id);
static void vgm_write_delay(UINT16 vgm_id);

//#define logerror	printf
#define logerror

// ASCII to Wide-Char String Copy
INLINE int atwcpy(wchar_t* dststr, const char* srcstr)
{
	return mbstowcs(dststr, srcstr, strlen(srcstr) + 0x01);
}

void vgm_setpath(const char* newpath)
{
	char* ExtPos;
	char* PathPos;
	
	strcpy(vgm_basepath, newpath);
	
	// find last \ or / (file's folder)
	PathPos = strrchr(vgm_basepath, '\\');
	ExtPos = strrchr(PathPos, '/');
	if (ExtPos != NULL)
		PathPos = ExtPos;
	
	// find dot of extention
	ExtPos = strrchr(PathPos, '.');
	if (ExtPos)
		*ExtPos = '\0';
	
	return;
}

void vgm_init(void)
{
	UINT16 curvgm;
	
	if (LOG_IS_INIT)
		return;
	
	// Reset all files
	for (curvgm = 0x00; curvgm < MAX_VGM_FILES; curvgm ++)
	{
		VgmFile[curvgm].hFile = NULL;
		VgmFile[curvgm].InUse = 0x00;
		VgmFile[curvgm].DataCount = 0x00;
		VgmFile[curvgm].DataBlk = NULL;
		VgmFile[curvgm].CmdAlloc = 0x00;
		VgmFile[curvgm].CmdCount = 0x00;
		VgmFile[curvgm].Commands = NULL;
	}
	for (curvgm = 0x00; curvgm < MAX_VGM_CHIPS; curvgm ++)
	{
		VgmChip[curvgm].ChipType = 0xFF;
	}
	
	LOG_IS_INIT = 0x01;
	
	return;
}

void vgm_start(int songnumber)
{
	char* NezInfo[4];
	UINT16 curvgm;
	char vgm_name[0x400];
	VGM_INF* VI;
	UINT32 curblk;
	VGM_ROM_DATA* VR;
	VGM_INIT_CMD* VC;
	UINT32 finalsize;
	UINT32 templng;
	
	LOG_VGM_FILE = vgmlog_enable ? 1 : 0;
	
	if (! LOG_VGM_FILE || LOG_IS_RUNNING)
		return;
	
	if (LOG_IS_INIT == 0x01)
		LOG_IS_INIT = 0x02;
	
	// Get the Game Information and write the GD3 Tag
	//sprintf(vgm_namebase, "%s_%02lX", m1snd_get_info_str(M1_SINF_ROMNAME, gamenum), number);
	NEZGetFileInfo(&NezInfo[0], &NezInfo[1], &NezInfo[2], &NezInfo[3]);
	for (curvgm = 0; curvgm < 4; curvgm ++)
	{
		if (NezInfo[curvgm] == NULL)
			NezInfo[curvgm] = "";
	}
	finalsize = strlen(NezInfo[2]);
	if (finalsize >= 0x10)
		finalsize = 0x0F;
	for (templng = 0x00; templng < finalsize; templng ++)
	{
		if (NezInfo[2][templng] != ' ')
			vgm_name[templng] = NezInfo[2][templng];
		else
			break;
	}
	vgm_name[templng] = '\0';
	NezInfo[2] = vgm_name;
	
	VgmTag.fccGD3 = 0x20336447;	// 'Gd3 '
	VgmTag.lngVersion = 0x00000100;
	wcscpy(VgmTag.strTrackNameE, L"");
	wcscpy(VgmTag.strTrackNameJ, L"");
	atwcpy(VgmTag.strGameNameE, NezInfo[0]);
	wcscpy(VgmTag.strGameNameJ, L"");
	atwcpy(VgmTag.strSystemNameE, "");
	wcscpy(VgmTag.strSystemNameJ, L"");
	atwcpy(VgmTag.strAuthorNameE, NezInfo[1]);
	wcscpy(VgmTag.strAuthorNameJ, L"");
	atwcpy(VgmTag.strReleaseDate, NezInfo[2]);
	wcscpy(VgmTag.strCreator, L"");
	
	swprintf(VgmTag.strNotes, 0x50, L"Song No: %02hu\nGenerated by %hs",
			songnumber, PLUGIN_NAME_BASE);
	VgmTag.lngTagLength = wcslen(VgmTag.strTrackNameE) + 0x01 +
				wcslen(VgmTag.strTrackNameJ) + 0x01 +
				wcslen(VgmTag.strGameNameE) + 0x01 +
				wcslen(VgmTag.strGameNameJ) + 0x01 +
				wcslen(VgmTag.strSystemNameE) + 0x01 +
				wcslen(VgmTag.strSystemNameJ) + 0x01 +
				wcslen(VgmTag.strAuthorNameE) + 0x01 +
				wcslen(VgmTag.strAuthorNameJ) + 0x01 +
				wcslen(VgmTag.strReleaseDate) + 0x01 +
				wcslen(VgmTag.strCreator) + 0x01 +
				wcslen(VgmTag.strNotes) + 0x01;
	VgmTag.lngTagLength *= sizeof(wchar_t);	// String Length -> Byte Length
	
	VgmFilesUsed = 0x00;
	for (curvgm = 0x00; curvgm < MAX_VGM_FILES; curvgm ++)
	{
		if (VgmFile[curvgm].InUse)
			VgmFilesUsed ++;
	}
	for (curvgm = 0x00; curvgm < MAX_VGM_FILES; curvgm ++)
	{
		VI = &VgmFile[curvgm];
		if (! VI->InUse || VI->hFile != NULL)
			continue;
		
		sprintf(vgm_name, "%s_%02hu.vgm", vgm_basepath, songnumber);
		logerror("Opening %s ...\t", vgm_name);
		VI->hFile = fopen(vgm_name, "wb");
		if (VI->hFile != NULL)
		{
			logerror("OK\n");
			VI->BytesWrt = 0x00;
			VI->SmplsWrt = 0;
			VI->EvtDelay = 0;
			fwrite(&VI->Header, 0x01, VI->HeaderBytes, VI->hFile);
			VI->BytesWrt += VI->HeaderBytes;
			
			for (curblk = 0x00; curblk < VI->DataCount; curblk ++)
			{
				VR = &VI->DataBlk[curblk];
				fputc(0x67, VI->hFile);
				fputc(0x66, VI->hFile);
				fputc(VR->Type, VI->hFile);
				if (VR->Type & 0x40)
				{
					finalsize = 0x02 + VR->DataSize;
					fwrite(&finalsize, 0x04, 0x01, VI->hFile);	// Data Block Size
					fwrite(&VR->Offset, 0x02, 0x01, VI->hFile);	// Data Base Address
				}
				else
				{
					finalsize = 0x08 + VR->DataSize;
					fwrite(&finalsize, 0x04, 0x01, VI->hFile);	// Data Block Size
					fwrite(&VR->ROMSize, 0x04, 0x01, VI->hFile);	// ROM Size
					fwrite(&VR->Offset, 0x04, 0x01, VI->hFile);	// Data Base Address
				}
				fwrite(VR->Data, 0x01, VR->DataSize & 0x7FFFFFFF, VI->hFile);
				free(VR->Data);
				VI->BytesWrt += 0x07 + (finalsize & 0x7FFFFFFF);
			}
			if (VI->DataCount) {
				VI->DataCount = 0x00;
				free(VI->DataBlk);
				VI->DataBlk = NULL;
			}
			for (curblk = 0x00; curblk < VI->CmdCount; curblk ++)
			{
				VC = &VI->Commands[curblk];
				fwrite(VC->Data, 0x01, VC->CmdLen, VI->hFile);
				VI->BytesWrt += VC->CmdLen;
			}
			if (VI->CmdCount) {
				VI->CmdAlloc = 0x00;
				VI->CmdCount = 0x00;
				free(VI->Commands);
				VI->Commands = NULL;
			}
		}
		else
		{
			logerror("Failed to create the file!\n");
		}
	}
	
	logerror("VGM logging started ...\n");
	LOG_IS_RUNNING = 0x01;
	
	return;
}

void vgm_stop(void)
{
	UINT16 curchip;
	UINT16 chip_unused;
	UINT16 curvgm;
	UINT32 clock_mask;
	VGM_HEADER* VH;
	
	if (LOG_VGM_FILE != 0x01)
	{
		LOG_VGM_FILE = 0x02;
		LOG_IS_INIT = 0x00;
		return;
	}
	
	chip_unused = 0x00;
	for (curchip = 0x00; curchip < MAX_VGM_CHIPS; curchip ++)
	{
		if (VgmChip[curchip].ChipType == 0xFF)
			break;
		
		if (! VgmChip[curchip].HadWrite)
		{
			chip_unused ++;
			curvgm = VgmChip[curchip].VgmID;
			VH = &VgmFile[curvgm].Header;
			// clock_mask - remove either the dual-chip bit or the entire clock
			clock_mask = (VgmChip[curchip].ChipType & 0x80) ? ~0x40000000 : 0x00000000;
			
			switch(VgmChip[curchip].ChipType & 0x7F)
			{
			case VGMC_SN76496:
				VH->lngHzPSG &= clock_mask;
				if (! clock_mask)
				{
					VH->shtPSG_Feedback = 0x0000;
					VH->bytPSG_SRWidth = 0x00;
					VH->bytPSG_Flags = 0x00;
				}
				break;
			case VGMC_YM2413:
				VH->lngHz2413 &= clock_mask;
				break;
			case VGMC_YM3812:
				VH->lngHz3812 &= clock_mask;
				break;
			case VGMC_YM3526:
				VH->lngHz3526 &= clock_mask;
				break;
			case VGMC_Y8950:
				VH->lngHz8950 &= clock_mask;
				break;
			case VGMC_AY8910:
				VH->lngHzAY8910 &= clock_mask;
				if (! clock_mask)
				{
					VH->lngAYFlags = 0x00;
					VH->lngAYType = 0x00;
				}
				break;
			case VGMC_GBSOUND:
				VH->lngHzGBDMG &= clock_mask;
				break;
			case VGMC_NESAPU:
				VH->lngHzNESAPU &= clock_mask;
				break;
			case VGMC_K051649:
				VH->lngHzK051649 &= clock_mask;
				break;
			case VGMC_C6280:
				VH->lngHzHuC6280 &= clock_mask;
				break;
			}
		}
	}
	if (chip_unused)
		logerror("Header Data of %hu unused Chips removed.\n", chip_unused);
	
	for (curvgm = 0x00; curvgm < MAX_VGM_FILES; curvgm ++)
	{
		if (VgmFile[curvgm].hFile != NULL)
			vgm_close(curvgm);
	}
	logerror("VGM stopped.\n");
	
	LOG_IS_INIT = 0x00;
	LOG_VGM_FILE = 0x02;
	LOG_IS_RUNNING = 0x00;
	
	return;
}

void vgm_add_delay(Uint32 Delay)
{
	UINT16 curvgm;
	
	if (! LOG_VGM_FILE)
		return;
	
	for (curvgm = 0x00; curvgm < MAX_VGM_FILES; curvgm ++)
	{
		if (VgmFile[curvgm].hFile != NULL)
			VgmFile[curvgm].EvtDelay += Delay;
	}
	
	return;
}

static void vgm_header_sizecheck(UINT16 vgm_id, UINT32 MinVer, UINT32 MinSize)
{
	VGM_INF* VI;
	VGM_HEADER* Header;
	
//	if (VgmFile[vgm_id].hFile == NULL)
//		return;
	
	VI = &VgmFile[vgm_id];
	Header = &VI->Header;
	
	if (Header->lngVersion < MinVer)
		Header->lngVersion = MinVer;
	if (VI->HeaderBytes < MinSize)
		VI->HeaderBytes = MinSize;
	
	return;
}

static void vgm_header_clear(UINT16 vgm_id)
{
	VGM_INF* VI;
	VGM_HEADER* Header;
	
	if (! VgmFile[vgm_id].InUse)
		return;
	
	VI = &VgmFile[vgm_id];
	Header = &VI->Header;
	memset(Header, 0x00, sizeof(VGM_HEADER));
	Header->fccVGM = 0x206D6756;	// 'Vgm '
	Header->lngEOFOffset = 0x00000000;
	Header->lngVersion = 0x00000151;
	//Header->lngGD3Offset = 0x00000000;
	//Header->lngTotalSamples = 0;
	//Header->lngLoopOffset = 0x00000000;
	//Header->lngLoopSamples = 0;
#ifdef DYNAMIC_HEADER_SIZE
	VI->HeaderBytes = 0x38;
	//VI->WroteHeader = 0x00;
#else
	VI->HeaderBytes = sizeof(VGM_HEADER);
	//VI->WroteHeader = 0x01;
#endif
	Header->lngDataOffset = VI->HeaderBytes - 0x34;
	
	//fseek(VI->hFile, 0x00, SEEK_SET);
	//fwrite(Header, 0x01, sizeof(VGM_HEADER), VI->hFile);
	//VI->BytesWrt += sizeof(VGM_HEADER);
	VI->BytesWrt = 0x00;
	
	return;
}

UINT16 vgm_open(UINT8 chip_type, int clock)
{
	UINT16 chip_id;
	UINT16 chip_file;
	UINT16 curvgm;
	UINT32 chip_val;
	UINT8 use_two;
	
//	if (! LOG_VGM_FILE)
//		return 0xFFFF;
	vgm_init();
	
	chip_id = 0xFFFF;
	for (curvgm = 0x00; curvgm < MAX_VGM_CHIPS; curvgm ++)
	{
		if (VgmChip[curvgm].ChipType == 0xFF)
		{
			chip_id = curvgm;
			break;
		}
	}
	if (chip_id == 0xFFFF)
		return 0xFFFF;
	
	chip_file = 0xFFFF;
	use_two = 0x00;
	for (curvgm = 0x00; curvgm < MAX_VGM_FILES; curvgm ++)
	{
		if (VgmFile[curvgm].InUse)
		{
			use_two = 0x01;
			switch(chip_type)
			{
			case VGMC_SN76496:
				chip_val = VgmFile[curvgm].Header.lngHzPSG;
				break;
			case VGMC_YM2413:
				chip_val = VgmFile[curvgm].Header.lngHz2413;
				break;
			case VGMC_YM3812:
				chip_val = VgmFile[curvgm].Header.lngHz3812;
				break;
			case VGMC_YM3526:
				chip_val = VgmFile[curvgm].Header.lngHz3526;
				break;
			case VGMC_Y8950:
				chip_val = VgmFile[curvgm].Header.lngHz8950;
				break;
			case VGMC_AY8910:
				chip_val = VgmFile[curvgm].Header.lngHzAY8910;
				break;
			case VGMC_GBSOUND:
				chip_val = VgmFile[curvgm].Header.lngHzGBDMG;
				break;
			case VGMC_NESAPU:
				chip_val = VgmFile[curvgm].Header.lngHzNESAPU;
				break;
			case VGMC_K051649:
				chip_val = VgmFile[curvgm].Header.lngHzK051649;
				break;
			case VGMC_C6280:
				chip_val = VgmFile[curvgm].Header.lngHzHuC6280;
				break;
			default:
				chip_val = 0x00000001;
				use_two = 0x00;
				break;
			}
			if (! chip_val)
			{
				chip_file = curvgm;
				break;
			}
			else if (use_two)
			{
				use_two = ! (chip_val & 0x40000000);
				if (use_two)
				{
					if (clock != chip_val)
						logerror("VGM Log: Warning - 2-chip mode, but chip clocks different!\n");
					chip_file = curvgm;
					clock = 0x40000000 | chip_val;
					chip_type |= 0x80;
					break;
				}
			}
		}
	}
	if (chip_file == 0xFFFF)
	{
		for (curvgm = 0x00; curvgm < MAX_VGM_FILES; curvgm ++)
		{
			if (! VgmFile[curvgm].InUse)
			{
				chip_file = curvgm;
				VgmFile[curvgm].InUse = 0x01;
				vgm_header_clear(curvgm);
				break;
			}
		}
	}
	if (chip_file == 0xFFFF)
		return 0xFFFF;
	
	VgmChip[chip_id].VgmID = chip_file;
	VgmChip[chip_id].ChipType = chip_type;
	VgmChip[chip_id].HadWrite = 0x00;
	
	switch(chip_type & 0x7F)
	{
	case VGMC_SN76496:
		VgmFile[chip_file].Header.lngHzPSG = clock;
		break;
	case VGMC_YM2413:
		VgmFile[chip_file].Header.lngHz2413 = clock;
		break;
	case VGMC_YM3812:
		VgmFile[chip_file].Header.lngHz3812 = clock;
		break;
	case VGMC_YM3526:
		VgmFile[chip_file].Header.lngHz3526 = clock;
		break;
	case VGMC_Y8950:
		VgmFile[chip_file].Header.lngHz8950 = clock;
		break;
	case VGMC_AY8910:
		VgmFile[chip_file].Header.lngHzAY8910 = clock;
		break;
	case VGMC_GBSOUND:
		VgmFile[chip_file].Header.lngHzGBDMG = clock;
		break;
	case VGMC_NESAPU:
		VgmFile[chip_file].Header.lngHzNESAPU = clock;
		break;
	case VGMC_K051649:
		VgmFile[chip_file].Header.lngHzK051649 = clock;
		break;
	case VGMC_C6280:
		VgmFile[chip_file].Header.lngHzHuC6280 = clock;
		break;
	}
	
	switch(chip_type & 0x7F)
	{
	case VGMC_SN76496:
	case VGMC_YM2413:
		vgm_header_sizecheck(chip_file, 0x00000151, 0x40);
		break;
	case VGMC_YM3812:
	case VGMC_YM3526:
	case VGMC_Y8950:
	case VGMC_AY8910:
		vgm_header_sizecheck(chip_file, 0x00000151, 0x80);
		break;
	case VGMC_GBSOUND:
	case VGMC_NESAPU:
	case VGMC_K051649:
	case VGMC_C6280:
		vgm_header_sizecheck(chip_file, 0x00000161, 0xC0);
		break;
	}
	
	return chip_id;
}

void vgm_header_set(UINT16 chip_id, UINT8 attr, UINT32 data)
{
	VGM_HEADER* VH;
	UINT8 bitcnt;
	
	if (! LOG_VGM_FILE || chip_id == 0xFFFF)
		return;
	if (VgmChip[chip_id].ChipType == 0xFF)
		return;
	
	VH = &VgmFile[VgmChip[chip_id].VgmID].Header;
	switch(VgmChip[chip_id].ChipType & 0x7F)	// Write the Header data
	{
	case VGMC_SN76496:
		switch(attr)
		{
		case 0x00:	// Reserved
			break;
		case 0x01:	// Shift Register Width (Feedback Mask)
			bitcnt = 0x00;	// Convert the BitMask to BitCount
			while(data)
			{
				data >>= 1;
				bitcnt ++;
			}
			VH->bytPSG_SRWidth = bitcnt;
			break;
		case 0x02:	// Feedback Pattern (White Noise Tap #1)
			VH->shtPSG_Feedback = (UINT16)data;
			break;
		case 0x03:	// Feedback Pattern (White Noise Tap #2)
			// must be called after #1
			VH->shtPSG_Feedback |= (UINT16)data;
			break;
		case 0x04:	// Negate Channels Flag
			VH->bytPSG_Flags &= ~(0x01 << 1);
			VH->bytPSG_Flags |= (data & 0x01) << 1;
			break;
		case 0x05:	// Stereo Flag (On/Off)
			// 0 is Stereo and 1 is mono
			VH->bytPSG_Flags &= ~(0x01 << 2);
			VH->bytPSG_Flags |= (~data & 0x01) << 2;
			break;
		case 0x06:	// Clock Divider (On/Off)
			VH->bytPSG_Flags &= ~(0x01 << 3);
			bitcnt = (data == 1) ? 0x01 : 0x00;
			VH->bytPSG_Flags |= (bitcnt & 0x01) << 3;
			break;
		case 0x07:	// Freq 0 is Max
			VH->bytPSG_Flags &= ~(0x01 << 0);
			VH->bytPSG_Flags |= (data & 0x01) << 0;
			break;
		}
		break;
	case VGMC_YM2413:
		switch(attr)
		{
		case 0x00:	// VRC7 Mode Enable
			VH->lngHz2413 = (VH->lngHz2413 & 0x7FFFFFFF) | (data << 31);
			break;
		}
		break;
	case VGMC_YM3812:
		break;
	case VGMC_YM3526:
		break;
	case VGMC_Y8950:
		break;
	case VGMC_AY8910:
		switch(attr)
		{
		case 0x00:	// Device Type
			VH->lngAYType = data & 0xFF;
			break;
		case 0x01:	// Flags
			VH->lngAYFlags = data & 0xFF;
			break;
		case 0x10:	// Resistor Loads
		case 0x11:
		case 0x12:
			logerror("AY8910: Resistor Load %hu = %u\n", attr & 0x0F, data);
			break;
		}
		break;
	case VGMC_GBSOUND:
		break;
	case VGMC_NESAPU:
		switch(attr)
		{
		case 0x00:	// FDS Enable
			VH->lngHzNESAPU = (VH->lngHzNESAPU & 0x7FFFFFFF) | (data << 31);
			break;
		}
		break;
	case VGMC_K051649:
		break;
	case VGMC_C6280:
		break;
	}
	
	return;
}

void vgm_close(UINT16 vgm_id)
{
	VGM_INF* VI;
	VGM_HEADER* Header;
	
	if (! LOG_VGM_FILE || vgm_id == 0xFFFF)
		return;
	
	VI = &VgmFile[vgm_id];
	Header = &VI->Header;
	
	// I don't think I need this in M1.
	/*if (! VI->WroteHeader)
	{
		fclose(VI->hFile);
		VI->hFile = NULL;
		return;
	}*/
	
	vgm_write_delay(vgm_id);
	fputc(0x66, VI->hFile);	// Write EOF Command
	VI->BytesWrt ++;
	
	// GD3 Tag
	Header->lngGD3Offset = VI->BytesWrt - 0x00000014;
	fwrite(&VgmTag.fccGD3, 0x04, 0x01, VI->hFile);
	fwrite(&VgmTag.lngVersion, 0x04, 0x01, VI->hFile);
	fwrite(&VgmTag.lngTagLength, 0x04, 0x01, VI->hFile);
	fwrite(VgmTag.strTrackNameE, sizeof(wchar_t), wcslen(VgmTag.strTrackNameE) + 0x01, VI->hFile);
	fwrite(VgmTag.strTrackNameJ, sizeof(wchar_t), wcslen(VgmTag.strTrackNameJ) + 0x01, VI->hFile);
	fwrite(VgmTag.strGameNameE, sizeof(wchar_t), wcslen(VgmTag.strGameNameE) + 0x01, VI->hFile);
	fwrite(VgmTag.strGameNameJ, sizeof(wchar_t), wcslen(VgmTag.strGameNameJ) + 0x01, VI->hFile);
	fwrite(VgmTag.strSystemNameE, sizeof(wchar_t), wcslen(VgmTag.strSystemNameE) + 0x01, VI->hFile);
	fwrite(VgmTag.strSystemNameJ, sizeof(wchar_t), wcslen(VgmTag.strSystemNameJ) + 0x01, VI->hFile);
	fwrite(VgmTag.strAuthorNameE, sizeof(wchar_t), wcslen(VgmTag.strAuthorNameE) + 0x01, VI->hFile);
	fwrite(VgmTag.strAuthorNameJ, sizeof(wchar_t), wcslen(VgmTag.strAuthorNameJ) + 0x01, VI->hFile);
	fwrite(VgmTag.strReleaseDate, sizeof(wchar_t), wcslen(VgmTag.strReleaseDate) + 0x01, VI->hFile);
	fwrite(VgmTag.strCreator, sizeof(wchar_t), wcslen(VgmTag.strCreator) + 0x01, VI->hFile);
	fwrite(VgmTag.strNotes, sizeof(wchar_t), wcslen(VgmTag.strNotes) + 0x01, VI->hFile);
	VI->BytesWrt += 0x0C + VgmTag.lngTagLength;
	
	// Rewrite Header
	Header->lngTotalSamples = VI->SmplsWrt;
	Header->lngEOFOffset = VI->BytesWrt - 0x00000004;
	Header->lngDataOffset = VI->HeaderBytes - 0x34;
	
	fseek(VI->hFile, 0x00, SEEK_SET);
	fwrite(Header, 0x01, VI->HeaderBytes, VI->hFile);
	
	fclose(VI->hFile);
	VI->hFile = NULL;
	
	logerror("VGM %02hX closed.\t%u Bytes, %u Samples written\n", vgm_id, VI->BytesWrt, VI->SmplsWrt);
	
	return;
}

static void vgm_write_delay(UINT16 vgm_id)
{
	VGM_INF* VI;
	UINT16 delaywrite;
	
	VI = &VgmFile[vgm_id];
//	if (! VI->WroteHeader && VI->EvtDelay)
//		vgm_header_postwrite(vgm_id);	// write post-header data
	
	while(VI->EvtDelay)
	{
		if (VI->EvtDelay > 0x0000FFFF)
			delaywrite = 0xFFFF;
		else
			delaywrite = (UINT16)VI->EvtDelay;
		
		if (delaywrite <= 0x0010)
		{
			fputc(0x6F + delaywrite, VI->hFile);
			VI->BytesWrt += 0x01;
		}
		else
		{
			fputc(0x61, VI->hFile);
			fwrite(&delaywrite, 0x02, 0x01, VI->hFile);
			VI->BytesWrt += 0x03;
		}
		VI->SmplsWrt += delaywrite;
		
		VI->EvtDelay -= delaywrite;
	}
	
	return;
}

void vgm_write(UINT16 chip_id, UINT8 port, UINT16 r, UINT8 v)
{
	VGM_CHIP* VC;
	VGM_INF* VI;
	INT8 cm;	// "Cheat Mode" to support 2 instances of 1 chip within 1 file
	UINT16 curchip;
	VGM_INIT_CMD WriteCmd;
	
	if (! LOG_VGM_FILE || chip_id == 0xFFFF)
		return;
	if (VgmChip[chip_id].ChipType == 0xFF)
		return;
	
	VC = &VgmChip[chip_id];
	VI = &VgmFile[VC->VgmID];
	// Can't use this because of LOG_IS_INIT
//	if (! VI->hFile)
//		return;
	
	if (! VC->HadWrite)
	{
		VC->HadWrite = 0x01;
		if (VC->ChipType & 0x80)
		{
			for (curchip = 0x00; curchip < chip_id; curchip ++)
			{
				if (VgmChip[curchip].ChipType == (VC->ChipType & 0x7F))
					VC->HadWrite = 0x01;
			}
		}
	}
	
	cm = (VC->ChipType & 0x80) ? 0x50 : 0x00;
	
	switch(VC->ChipType & 0x7F)	// Write the data
	{
	case VGMC_SN76496:
		switch(port)
		{
		case 0x00:
			cm = cm ? -0x20 : 0x00;
			WriteCmd.Data[0x00] = 0x50 + cm;
			WriteCmd.Data[0x01] = r;
			WriteCmd.CmdLen = 0x02;
			break;
		case 0x01:
			cm = cm ? -0x10 : 0x00;
			WriteCmd.Data[0x00] = 0x4F + cm;
			WriteCmd.Data[0x01] = r;
			WriteCmd.CmdLen = 0x02;
			break;
		}
		break;
	case VGMC_YM2413:
		WriteCmd.Data[0x00] = 0x51 + cm;
		WriteCmd.Data[0x01] = r;
		WriteCmd.Data[0x02] = v;
		WriteCmd.CmdLen = 0x03;
		break;
	case VGMC_YM3812:
		WriteCmd.Data[0x00] = 0x5A + cm;
		WriteCmd.Data[0x01] = r;
		WriteCmd.Data[0x02] = v;
		WriteCmd.CmdLen = 0x03;
		break;
	case VGMC_YM3526:
		WriteCmd.Data[0x00] = 0x5B + cm;
		WriteCmd.Data[0x01] = r;
		WriteCmd.Data[0x02] = v;
		WriteCmd.CmdLen = 0x03;
		break;
	case VGMC_Y8950:
		WriteCmd.Data[0x00] = 0x5C + cm;
		WriteCmd.Data[0x01] = r;
		WriteCmd.Data[0x02] = v;
		WriteCmd.CmdLen = 0x03;
		break;
	case VGMC_AY8910:
		WriteCmd.Data[0x00] = 0xA0;
		WriteCmd.Data[0x01] = r | (VC->ChipType & 0x80);
		WriteCmd.Data[0x02] = v;
		WriteCmd.CmdLen = 0x03;
		break;
	case VGMC_GBSOUND:
		WriteCmd.Data[0x00] = 0xB3;
		WriteCmd.Data[0x01] = r | (VC->ChipType & 0x80);
		WriteCmd.Data[0x02] = v;
		WriteCmd.CmdLen = 0x03;
		break;
	case VGMC_NESAPU:
		if (port == 0x01)
		{
			// FDS Register patch
			if (r >= 0x80)
				r -= 0x60;
			else if (r == 0x23)
				r = 0x3F;
		}
		WriteCmd.Data[0x00] = 0xB4;
		WriteCmd.Data[0x01] = r | (VC->ChipType & 0x80);
		WriteCmd.Data[0x02] = v;
		WriteCmd.CmdLen = 0x03;
		break;
	case VGMC_K051649:
		WriteCmd.Data[0x00] = 0xD2;
		WriteCmd.Data[0x01] = port | (VC->ChipType & 0x80);
		WriteCmd.Data[0x02] = r;
		WriteCmd.Data[0x03] = v;
		WriteCmd.CmdLen = 0x04;
		break;
	case VGMC_C6280:
		WriteCmd.Data[0x00] = 0xB9;
		WriteCmd.Data[0x01] = r | (VC->ChipType & 0x80);
		WriteCmd.Data[0x02] = v;
		WriteCmd.CmdLen = 0x03;
		break;
	default:
		return;
	}
	
	if (LOG_IS_INIT == 0x01)
	{
		if (VI->CmdCount >= VI->CmdAlloc)
		{
			VI->CmdAlloc += 0x100;
			VI->Commands = (VGM_INIT_CMD*)realloc(VI->Commands, sizeof(VGM_INIT_CMD) * VI->CmdAlloc);
		}
		VI->Commands[VI->CmdCount] = WriteCmd;
		VI->CmdCount ++;
	}
	
	if (VI->hFile != NULL)
	{
		vgm_write_delay(VC->VgmID);
		
		fwrite(WriteCmd.Data, 0x01, WriteCmd.CmdLen, VI->hFile);
		VI->BytesWrt += WriteCmd.CmdLen;
	}
	
	return;
}

void vgm_write_large_data(UINT16 chip_id, UINT8 type, UINT32 datasize, UINT32 value1, UINT32 value2, const void* data)
{
	// datasize - ROM/RAM size
	// value1 - Start Address
	// value2 - Bytes to Write (0 -> write from Start Address to end of ROM/RAM)
	
	VGM_INF* VI;
	VGM_ROM_DATA* VR;
	UINT32 finalsize;
	UINT8 blk_type;
	
	if (! LOG_VGM_FILE || chip_id == 0xFFFF)
		return;
	if (VgmChip[chip_id].ChipType == 0xFF)
		return;
	
	VI = &VgmFile[VgmChip[chip_id].VgmID];
//	if (VI->hFile == NULL)
//		return;
	
	blk_type = 0x00;
	switch(VgmChip[chip_id].ChipType & 0x7F)	// Write the data
	{
	case VGMC_SN76496:
		break;
	case VGMC_YM2413:
		break;
	case VGMC_YM3812:
		break;
	case VGMC_YM3526:
		break;
	case VGMC_Y8950:
		switch(type)
		{
		case 0x00:
			break;
		case 0x01:	// DELTA-T Memory
			blk_type = 0x88;	// Type: Y8950 DELTA-T ROM Data
			break;
		}
		break;
	case VGMC_AY8910:
		break;
	case VGMC_GBSOUND:
		break;
	case VGMC_NESAPU:
		switch(type)
		{
		case 0x00:
			break;
		case 0x01:	// RAM Data
			//if (vgm_nes_ram_check(VI, datasize, &value1, &value2, (UINT8*)data))
				blk_type = 0xC2;
			break;
		}
		break;
	case VGMC_K051649:
		break;
	case VGMC_C6280:
		break;
	}
	
	if (! blk_type)
		return;
	
	// Value 1 & 2 are used to write parts of the image (and save space)
	if (! value2)
		value2 = datasize - value1;
	if (data == NULL)
	{
		value1 = 0x00;
		value2 = 0x00;
	}
	
	if (LOG_IS_INIT == 0x01)
	{
		VI->DataCount ++;
		VI->DataBlk = (VGM_ROM_DATA*)realloc(VI->DataBlk, sizeof(VGM_ROM_DATA) * VI->DataCount);
		
		VR = &VI->DataBlk[VI->DataCount - 0x01];
		VR->Type = blk_type;
		VR->DataSize = value2; // | ((VgmChip[chip_id].ChipType & 0x80) << 24);
		VR->Offset = value1;
		VR->ROMSize = datasize;
		VR->Data = malloc(VR->DataSize);
		memcpy(VR->Data, data, VR->DataSize);
	}
	if (! LOG_VGM_FILE || VI->hFile == NULL)
		return;
	
	vgm_write_delay(VgmChip[chip_id].VgmID);
	
	fputc(0x67, VI->hFile);
	fputc(0x66, VI->hFile);
	fputc(blk_type, VI->hFile);
	
	switch(blk_type & 0xC0)
	{
	case 0x80:	// ROM Image
		finalsize = 0x08 + value2;
		//finalsize |= (VgmChip[chip_id].ChipType & 0x80) << 24;
		
		fwrite(&finalsize, 0x04, 0x01, VI->hFile);	// Data Block Size
		fwrite(&datasize, 0x04, 0x01, VI->hFile);	// ROM Size
		fwrite(&value1, 0x04, 0x01, VI->hFile);		// Data Base Address
		fwrite(data, 0x01, value2, VI->hFile);
		VI->BytesWrt += 0x07 + (finalsize & 0x7FFFFFFF);
		break;
	case 0xC0:	// RAM Writes
		finalsize = 0x02 + value2;
		//finalsize |= (VgmChip[chip_id].ChipType & 0x80) << 24;
		
		fwrite(&finalsize, 0x04, 0x01, VI->hFile);	// Data Block Size
		fwrite(&value1, 0x02, 0x01, VI->hFile);		// Data Address
		fwrite(data, 0x01, value2, VI->hFile);
		VI->BytesWrt += 0x07 + (finalsize & 0x7FFFFFFF);
		break;
	}
	
	return;
}
