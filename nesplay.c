/*
 * 使い方
 *
 * nesplay [-s] [-l count] comport file
 *
 * 例: nesplay -l 3 com1 song.vgz
 *
 * comportはゲボファミのCOMポート、fileにはVGM又はVGZファイルを指定します。
 * ファミコン向けのファイルに限らずYM2413向けのファイルをVRC7で(不完全に)再生することも出来ます。
 * COMポート名はデバイスマネージャーで確認して下さい。
 *
 * -sオプションは悪魔城伝説(VRC6)向けに作成されたファイルを魍魎戦記MADARAで再生(あるいはその逆)する場合に指定します。
 * -lオプションはループ再生パートの再生回数です。0以上の整数を指定して下さい。
 * -lオプション未指定の場合は1回の再生です。値に0を指定した場合は無限ループ再生となります
 * ループ再生パートの無いファイルの場合はこのオプションは無視されます。
 *
 * ゲボファミの通信プロトコル
 *
 * COMポートに以下のVGMコマンドを書き込むとゲボファミはコマンドに従ってファミコンシステムを制御します。
 *
 * 0xb4, address, data // 本体内蔵APU/FDS書込みコマンド
 * 0x51, address, data // YM2413(VRC7)書込みコマンド
 * 0x67, 0x66, 0xc2, size, size, size, size, address, address, (data) // 本体RAM書込みコマンド
 *
 * 上記3コマンドのみの対応です。それ以外のコマンドは書き込まないで下さい。
 * ゲボファミはコマンド受信中に100ミリ秒以上データが途絶えると受信中のコマンドを破棄して次のコマンドを待ちます。
 *
 * 0x67コマンドは本体RAMにデータを書き込む為のコマンドですがファミコン音源はアドレス空間にマップされているので
 * 音源操作コマンドとしても使用出来ます。MMC5, VRC6, N163はこのコマンドで制御しています。
 *
 * 各コマンドの詳細は以下のページを参考にして下さい。
 * https://vgmrips.net/wiki/VGM_Specification
 *
 * COMポートプログラミングのヒント
 *
 * 通信はUSBで完結しているのでCOMポートの通信速度等の設定は意味を持ちません。デフォルト設定のままで大丈夫です。
 * fwrite等、書込みバッファを持つ関数で書込みを行なう場合はコマンド書込み直後にfflush等でデータを吐き出してください。
 *
 * その他
 *
 * 高精度タイマーを使用して高精度再生しますがCPU負荷高いです💦。
 * このソースコードはwindows上のmingw(gcc)でコンパイルできます。
 * gzipデータの伸張にuzlibを使用しています。
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <wchar.h>
#include <signal.h>
#include <windows.h>

#include "uzlib.h"

static int port;
static uint64_t freq, start;

static int uncompress(const uint8_t *src, size_t srclen, uint8_t **dst, size_t *dstlen)
{
/* produce decompressed output in chunks of this size */
/* default is to decompress byte by byte; can be any other length */
#define OUT_CHUNK_SIZE 1
	size_t dlen;
	int res;

	uzlib_init();

	/* -- get decompressed length -- */
	dlen =            src[srclen - 1];
	dlen = 256*dlen + src[srclen - 2];
	dlen = 256*dlen + src[srclen - 3];
	dlen = 256*dlen + src[srclen - 4];

	*dstlen = dlen;

	/* there can be mismatch between length in the trailer and actual
	   data stream; to avoid buffer overruns on overlong streams, reserve
	   one extra byte */
	dlen++;

	*dst = malloc(dlen);
	if (*dst == NULL) {
		return 1;
	}

	/* -- decompress data -- */
	struct uzlib_uncomp d;
	uzlib_uncompress_init(&d, NULL, 0);

	/* all 3 fields below must be initialized by user */
	d.source = src;
	d.source_limit = src + srclen - 4;
	d.source_read_cb = NULL;

	res = uzlib_gzip_parse_header(&d);
	if (res != TINF_OK) {
		printf("Error parsing header: %d\n", res);
		return 1;
	}

	d.dest_start = d.dest = *dst;

	while (dlen) {
		unsigned int chunk_len = dlen < OUT_CHUNK_SIZE ? dlen : OUT_CHUNK_SIZE;
		d.dest_limit = d.dest + chunk_len;
		res = uzlib_uncompress_chksum(&d);
		dlen -= chunk_len;
		if (res != TINF_OK) {
			break;
		}
	}

	if (res != TINF_DONE) {
		printf("Error during decompression: %d\n", res);
		return 1;
	}

	return 0;
}

static void wait_until(uint64_t time)
{
	uint64_t cnt;

	while (1) {
		QueryPerformanceCounter((LARGE_INTEGER *)&cnt);
		if ((uint64_t)(cnt - start) >= (freq * time / 44100)) {
			break;
		}
	}
}

static void initvgm(void)
{
	/* APU/FDS初期化 */
	uint8_t apu[] = { 0xb4, 0x00, 0x00 };
	for (uint8_t addr = 0x00; addr <= 0x8f; addr++) {
		apu[1] = addr;
		write(port, apu, sizeof(apu));
        }
	apu[1] = 0x2a; /* 0x408a */
	apu[2] = 0xe8; /* FDS BIOSによって設定されるデフォルトのエンベロープ速度 */
	write(port, apu, sizeof(apu));
	apu[1] = 0x3f; /* 0x4023 */
	apu[2] = 0x02; /* FDS音源機能を有効化 */
	write(port, apu, sizeof(apu));

	/* MMC5初期化 */
	uint8_t mmc5[] = { 0x67, 0x66, 0xc2, 0x03, 0x00, 0x00, 0x00, 0x00, 0x50, 0x00 };
	for (uint8_t addr = 0x00; addr <= 0x17; addr++) { /* 0x5000-0x5017を1バイトずつゼロクリア */
		mmc5[7] = addr;
		write(port, mmc5, sizeof(mmc5));
        }

	/* VRC6初期化 */
	uint8_t vrc6[] = { 0x67, 0x66, 0xc2, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	vrc6[8] = 0x90;
	write(port, vrc6, sizeof(vrc6)); /* 0x9000-0x9003を一括ゼロクリア */
	vrc6[8] = 0xa0;
	write(port, vrc6, sizeof(vrc6)); /* 0xa000-0xa003を一括ゼロクリア */
	vrc6[8] = 0xb0;
	write(port, vrc6, sizeof(vrc6)); /* 0xb000-0xb003を一括ゼロクリア */

	/* VRC7初期化 */
	uint8_t vrc7[] = { 0x51, 0x00, 0x00 };
	for (uint8_t addr = 0x00; addr <= 0x35; addr++) {
		vrc7[1] = addr;
		write(port, vrc7, sizeof(vrc7));
	}

	/* N163初期化 */
	uint8_t n163[] = { 0x67, 0x66, 0xc2, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	n163[8] = 0xf8;
	n163[9] = 0x80;
	write(port, n163, sizeof(n163)); /* N163音源アドレスを0x00にする(自動インクリメント) */
	n163[8] = 0x48;
	n163[9] = 0x00;
	for (uint8_t addr = 0x00; addr <= 0x7f; addr++) { /* N163音源アドレス0x00-0x7fを連続ゼロクリア */
		write(port, n163, sizeof(n163));
	}
	n163[8] = 0xe0;
	n163[9] = 0x00;
	write(port, n163, sizeof(n163)); /* 0xe000に0を書いてN163音源機能を有効化 */
}

static void stopvgm(int signum)
{
	initvgm();
	close(port);

	exit(0);
}

static uint8_t swap = 0; /* 悪魔城・マダラ相互変換フラグ */

const uint8_t cmdlen[256] = { /* 各VGMコマンドのデータ長 未対応コマンドのスキップ処理で使用 */
	/* 0     1     2     3     4     5     6     7     8     9     a     b     c     d     e     f */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 2 */
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, /* 3 */
	0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x01, /* 4 */
	0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, /* 5 */
	0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 6 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 7 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 8 */
	0x04, 0x04, 0x05, 0x0a, 0x01, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 9 */
	0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* a */
	0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, /* b */
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, /* c */
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, /* d */
	0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, /* e */
	0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, /* f */
};

static void playvgm(uint8_t *data, uint32_t len)
{
	uint32_t pos;
	static uint64_t time = 0;

	for (pos = 0; pos < len; pos++) {
		switch (data[pos]) {
			case 0xb4: /* NES */
			case 0x51: /* YM2413 */
				//signal(SIGINT, SIG_IGN); 必要なさそう
				write(port, &data[pos], 3);
				//signal(SIGINT, stopvgm);
				pos += 2;
				break;
			case 0x61:
				time += *(uint16_t *)&data[pos + 1]; /* アライメントされていないのでx86以外の場合はここ注意 */
				pos += 2;
				wait_until(time);
				break;
			case 0x62:
				time += 735;
				wait_until(time);
				break;
			case 0x63:
				time += 882;
				wait_until(time);
				break;
			case 0x66: /* end */
				return;
			case 0x67:
				/* アライメントされていないのでx86以外の場合は以下注意 */
				if (data[pos + 2] != 0xc2) {
					pos += (7 + *(uint32_t *)&data[pos + 3] - 1);
					break;
				}
				if (swap && (*(uint32_t *)&data[pos + 3] == 3)) {
					switch (*(uint16_t *)&data[pos + 7]) { /* 悪魔城・マダラ相互変換 */
					case 0x9001:
					case 0x9002:
					case 0xa001:
					case 0xa002:
					case 0xb001:
					case 0xb002:
						*(uint16_t *)&data[pos + 7] = (*(uint16_t *)&data[pos + 7] & 0xfffc) | (~*(uint16_t *)&data[pos + 7] & 0x0003);
						break;
					}
				}
				//signal(SIGINT, SIG_IGN); 必要なさそう
				write(port, &data[pos], 7 + *(uint32_t *)&data[pos + 3]);
				//signal(SIGINT, stopvgm);
				pos += (7 + *(uint32_t *)&data[pos + 3] - 1);
				break;
			case 0x70:
			case 0x71:
			case 0x72:
			case 0x73:
			case 0x74:
			case 0x75:
			case 0x76:
			case 0x77:
			case 0x78:
			case 0x79:
			case 0x7a:
			case 0x7b:
			case 0x7c:
			case 0x7d:
			case 0x7e:
			case 0x7f:
				time += ((data[pos] & 0x0f) + 1);
				wait_until(time);
				break;
			default: /* 未サポートコマンドはスキップ */
				pos += cmdlen[data[pos]];
				break;
		}
	}
}

static void usage(char *name)
{
	fprintf(stderr, "usage: %s [-s] [-l count] comport file", name);
	exit(1);
}

int main(int argc, char *argv[])
{
	int opt;
	int loop = 1;
	char *ptr;
	char pname[16];
	int file;
	struct stat st;
	uint8_t *vgmdat;
	uint32_t doffset;
	uint32_t loffset;
	uint32_t eoffset;
	uint32_t goffset;

	while ((opt = getopt(argc, argv, "sl:")) != -1) {
		switch (opt) {
			case 's':
				swap = 1;
				break;
			case 'l':
				loop = strtol(optarg, &ptr, 10);
				if (*ptr || (loop < 0)) {
					usage(argv[0]);
				}
				break;
			default:
				usage(argv[0]);
		}
	}
	if ((argc - optind) != 2) {
		usage(argv[0]);
	}

	/* シリアルポートを開く */
	snprintf(pname, sizeof(pname), "\\\\.\\%s", argv[optind]);
	if ((port = open(pname, O_WRONLY | O_BINARY)) < 0) {
		perror(argv[optind]);
		return 1;
	}

	/* ファイルを読み込む */
	if ((file = open(argv[optind + 1], O_RDONLY | O_BINARY)) < 0) {
		perror(argv[optind + 1]);
		return 1;
	}
	if (fstat(file, &st) < 0) {
		perror("fstat()");
		return 1;
	}
	if (!(vgmdat = malloc(st.st_size))) {
		perror("malloc()");
		return 1;
	}
	if (read(file, vgmdat, st.st_size) != st.st_size) {
		perror("read()");
		return 1;
	}
	close(file);

	/* データのチェック */
	if ((vgmdat[0] == 0x1f) && (vgmdat[1] == 0x8b)) { /* gzip圧縮データなら伸張 */
		uint8_t *buf;
		size_t buflen;
		if (st.st_size < 6) {
			fprintf(stderr, "invalid file size\n");
			return 1;
		}
		if (uncompress(vgmdat, st.st_size, &buf, &buflen)) {
			fprintf(stderr, "uncompress error\n");
			return 1;
		}
		free(vgmdat);
		vgmdat = buf;
		st.st_size = buflen;
	}
	if (st.st_size < 256) {
		fprintf(stderr, "invalid file size\n");
		return 1;
	}
	if (memcmp(vgmdat, "Vgm", 3)) { /* VGMデータか */
		fprintf(stderr, "this isn't a VGM file\n");
		return 1;
	}

	/* オフセット情報の取得 */
	eoffset = *(uint32_t *)&vgmdat[0x04] + 0x04; /* EOF offset */
	if (st.st_size != eoffset) {
		fprintf(stderr, "invalid file size\n");
		return 1;
	}
	loffset = *(uint32_t *)&vgmdat[0x1c] + 0x1c; /* loop offset */
	doffset = *(uint32_t *)&vgmdat[0x34] + 0x34; /* VGM data offset */
	if (doffset == 0) {
		doffset = 0x40;
	}
	goffset = *(uint32_t *)&vgmdat[0x14] + 0x14; /* GD3 offset */

	/* GD3タグ情報の表示 */
	if ((0x14 < goffset) && (goffset <= (eoffset - (12 + 2)))) { /* +2はgd3tag[0]の分 */
		if (memcmp("Gd3", &vgmdat[goffset], 3) == 0) { /* GD3タグか */
			wchar_t *gd3tag[11] = { NULL };
			uint8_t cnt = 0;
			uint32_t pos = goffset + 12;
			/* アライメントされていないのでx86以外の場合は以下注意 */
			gd3tag[cnt++] = (wchar_t *)&vgmdat[pos];
			for ( ; (pos < eoffset) && (cnt < 11); pos += 2) {
				if (*(wchar_t *)&vgmdat[pos] == 0) {
					gd3tag[cnt++] = (wchar_t *)&vgmdat[pos + 2];
				}
			}
			if (gd3tag[2]) {
				wprintf(L"Game  : %ls", gd3tag[2]);
				if (gd3tag[8]) {
					wprintf(L" [%ls]", gd3tag[8]);
				}
				wprintf(L"\n");
			}
			if (gd3tag[0]) {
				wprintf(L"Track : %ls\n", gd3tag[0]);
			}
			if (gd3tag[4]) {
				wprintf(L"System: %ls\n", gd3tag[4]);
			}
			if (gd3tag[6]) {
				wprintf(L"Artist: %ls\n", gd3tag[6]);
			}
			if (gd3tag[9]) {
				wprintf(L"Dumper: %ls\n", gd3tag[9]);
			}
		}
	}

	/* 再生準備 */
	signal(SIGINT, stopvgm);
	initvgm();
	QueryPerformanceFrequency((LARGE_INTEGER *)&freq);
	QueryPerformanceCounter((LARGE_INTEGER *)&start);

	/* 再生 */
	if ((0x34 < doffset) && (doffset <= eoffset)) {
		playvgm(&vgmdat[doffset], eoffset - doffset);
	}
	for (int i = 0; ((0x1c < loffset) && (loffset <= eoffset)) && (!loop || (i < loop)); i++) {
		playvgm(&vgmdat[loffset], eoffset - loffset);
	}
	stopvgm(0);

	return 0;
}
