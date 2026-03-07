#ifndef __VGMWRITE_H__
#define __VGMWRITE_H__

#ifdef __cplusplus
extern "C" {
#endif

void vgm_setpath(const char* newpath);
void vgm_init(void);
void vgm_start(int songnumber);
void vgm_stop(void);
void vgm_add_delay(Uint32 Delay);
Uint16 vgm_open(Uint8 chip_type, int clock);
void vgm_header_set(Uint16 chip_id, Uint8 attr, Uint32 data);
void vgm_write(Uint16 chip_id, Uint8 port, Uint16 r, Uint8 v);
void vgm_write_large_data(Uint16 chip_id, Uint8 type, Uint32 datasize, Uint32 value1, Uint32 value2, const void* data);

// VGM Chip Constants
// v1.00
#define VGMC_SN76496	0x00
#define VGMC_YM2413		0x01	// OPLL
// v1.51
//#define VGMC_SEGAPCM		0x04
#define VGMC_YM3812		0x09	// OPL2
#define VGMC_YM3526		0x0A	// OPL
#define VGMC_Y8950		0x0B	// MSXAUDIO (OPL + ADPCM)
#define VGMC_AY8910		0x12	// PSG
// v1.61
#define VGMC_GBSOUND	0x13	// DMG
#define VGMC_NESAPU		0x14	// 2A03
#define VGMC_K051649	0x19	// SCC1
#define VGMC_C6280		0x1B

#ifdef __cplusplus
}
#endif

#endif /* __VGMWRITE_H__ */
