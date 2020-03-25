/*  This file is part of Wii (U) NAND Extractor C.
 *  Copyright (C) 2020 GaryOderNichts
 *
 *  This file was ported from Wii NAND Extractor.
 *  Copyright (C) 2009 Ben Wilson
 *  
 *  Wii NAND Extractor is free software: you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as published
 *  by the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Wii NAND Extractor is distributed in the hope that it will be
 *  useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 *  of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "extractor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <io.h>
#include "aes.h"

byte_t* key;
FILE* rom;
int32_t loc_super;
int32_t loc_fat;
int32_t loc_fst;
FileType fileType = Invalid;
NandType nandType;

int8_t initSuccess = 0;

char* stringReplaceAll(const char *search, const char *replace, char *string) 
{
	char* searchStart = strstr(string, search);
	while (searchStart != NULL)
	{
		char* tempString = (char*) malloc(strlen(string) * sizeof(char));
		if(tempString == NULL) {
			return NULL;
		}

		strcpy(tempString, string);

		int len = searchStart - string;
		string[len] = '\0';

		strcat(string, replace);

		len += strlen(search);
		strcat(string, (char*)tempString+len);

		free(tempString);

		searchStart = strstr(string, search);
	}
	
	return string;
}

int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		printf("not enough args\n");
		return 0;
	}
	
	//init nand

	rom = fopen(argv[1], "rb");
	if (rom == NULL)
	{
		printf("error opening %s\n", argv[1]);
		return -1;
	}

	if (!getFileType() || !getNandType() || !getKey())
		return -1;

	loc_super = findSuperblock();
	if (loc_super == -1)
	{
		printf("can't find superblock!");
	}
	
	int32_t fatlen = getClusterSize() * 4;
	loc_fat = loc_super;
	loc_fst = loc_fat + 0x0C + fatlen;

	initSuccess = 1;
	extractNand();

	fclose(rom);
	free(key);

	return 0;
}

int32_t getPageSize(void)
{
	if (fileType == NoECC)
		return PAGE_SIZE;
	else
		return PAGE_SIZE + SPARE_SIZE;
}

int32_t getClusterSize(void)
{
	return getPageSize() * 8;
}

uint8_t getFileType(void)
{
	fseek(rom, 0, SEEK_END);
	uint64_t lenght = ftell(rom);
	switch (lenght)
	{
	case PAGE_SIZE * 8 * CLUSTERS_COUNT:
		fileType = NoECC;
		return 1;
	case (PAGE_SIZE + SPARE_SIZE) * 8 * CLUSTERS_COUNT:
		fileType = ECC;
		return 1;
	case (PAGE_SIZE + SPARE_SIZE) * 8 * CLUSTERS_COUNT + 0x400:
		fileType = BootMii;
		return 1;
	default:
		return 0;
	}
}

uint8_t getNandType(void)
{
	rewind(rom);
	fseek(rom, getClusterSize() * 0x7FF0, SEEK_SET);
	uint32_t cluster;
	fread(&cluster, sizeof(uint32_t), 1, rom);
	switch (bswap32(cluster))
	{
	case 0x53464653:
		nandType = Wii;
		return 1;
	case 0x53465321:
		if (fileType == BootMii) return 0; // Invalid dump type for WiiU
		nandType = WiiU;
		return 1;
	default:
		return 0;
	}
}

uint8_t getKey(void)
{
	//TODO key from text or otp
	
	if (fileType == BootMii)
	{
		/* code */
	}
	else
	{
		if (nandType == Wii)
		{
			key = readKeyfile("keys.bin");
			if (key != NULL)
				return 1;
		}
		
	}
	
	

	printf("Error key not valid");
	return 0;
}

byte_t* readKeyfile(char* path)
{
	byte_t* retval = malloc(sizeof(char) * 16);

	FILE* keyfile = fopen(path, "rb");
	
	fseek(keyfile, 0x158, SEEK_SET);
	fread(retval, sizeof(byte_t), 16, keyfile);
	fclose(keyfile);

	return retval;
}

int32_t findSuperblock(void)
{
	int32_t loc = ((nandType == Wii) ? 0x7F00 : 0x7C00) * getClusterSize();
	int32_t end = CLUSTERS_COUNT * getClusterSize();
	int32_t len = getClusterSize() * 0x10;
	int32_t current, last = 0;

	rewind(rom);
	fseek(rom, loc + 4, SEEK_SET);

	for (; loc < end; loc += len)
	{
		fread(&current, sizeof(uint32_t), 1, rom);
		current = bswap32(current);

		if (current > last)
			last = current;
		else
			return loc - len;

		fseek(rom, len - 4, SEEK_CUR);
	}

	return -1;
}

fst_t getFST(uint16_t entry)
{
	fst_t fst;

	// compensate for 64 bytes of ecc data every 64 fst entries
	int32_t n_fst = (fileType == NoECC) ? 0 : 2;
	int32_t loc_entry = (((entry / 0x40) * n_fst) + entry) * 0x20;

	rewind(rom);
	fseek(rom, loc_fst + loc_entry, SEEK_SET);

	fread(&fst.filename, sizeof(byte_t), 0x0C, rom);
	fread(&fst.mode, sizeof(byte_t), 1, rom);
	fread(&fst.attr, sizeof(byte_t), 1, rom);

	uint16_t sub;
	fread(&sub, sizeof(uint16_t), 1, rom);
	sub = bswap16(sub);
	fst.sub = sub;

	uint16_t sib;
	fread(&sib, sizeof(uint16_t), 1, rom);
	sib = bswap16(sib);
	fst.sib = sib;

	uint32_t size;
	fread(&size, sizeof(uint32_t), 1, rom);
	size = bswap32(size);
	fst.size = size;

	uint32_t uid;
	fread(&uid, sizeof(uint32_t), 1, rom);
	uid = bswap32(uid);
	fst.uid = uid;

	uint16_t gid;
	fread(&gid, sizeof(uint16_t), 1, rom);
	gid = bswap16(gid);
	fst.gid = gid;

	uint32_t x3;
	fread(&x3, sizeof(uint32_t), 1, rom);
	x3 = bswap32(x3);
	fst.x3 = x3;

	fst.mode &= 1;

	return fst;
}

void extractNand(void)
{
	if (initSuccess != 1)
	{
		printf("NAND has not been initialized successfully!");
		return;
	}
	
	mkdir("slccmpt");
	extractFST(0, "");
}

byte_t* getCluster(uint16_t cluster_entry)
{
	byte_t* cluster = calloc(0x4000, sizeof(byte_t));
	byte_t* page = calloc(getPageSize(), sizeof(byte_t));

	rewind(rom);
	fseek(rom, cluster_entry * getClusterSize(), SEEK_SET);

	for (int i = 0; i < 8; i++)
	{
		fread(page, sizeof(byte_t), getPageSize(), rom);
		memcpy((byte_t*) cluster + (i * 0x800), page, 0x800);
	}

	free(page);

	return aesDecrypt(key, cluster, 0x4000);
}

uint16_t getFAT(uint16_t fat_entry)
{
	/*
	* compensate for "off-16" storage at beginning of superblock
	* 53 46 46 53   XX XX XX XX   00 00 00 00
	* S  F  F  S     "version"     padding?
	*   1     2       3     4       5     6
	*/
	fat_entry += 6;

	// location in fat of cluster chain
	int32_t n_fat = (fileType == NoECC) ? 0 : 0x20;
	int32_t loc = loc_fat + ((((fat_entry / 0x400) * n_fat) + fat_entry) * 2);

	rewind(rom);
	fseek(rom, loc, SEEK_SET);

	uint16_t fat;
	fread(&fat, sizeof(uint16_t), 1, rom);
	return bswap16(fat);
}

void extractFST(uint16_t entry, char* parent)
{
	fst_t fst = getFST(entry);

	if (fst.sib != 0xffff)
		extractFST(fst.sib, parent);

	switch (fst.mode)
	{
	case 0:
		extractDir(fst, entry, parent);
		break;
	case 1:
		extractFile(fst, entry, parent);
		break;
	default:
	printf("ignoring unsupported mode");
		break;
	}
}

void extractDir(fst_t fst, uint16_t entry, char* parent)
{
	char* filename = malloc(_MAX_PATH);
	snprintf(filename, 13, "%s", fst.filename);
	//strncpy(filename, (char*) fst.filename, 12);

	char* newfilename = malloc(_MAX_PATH);
	newfilename[0] = '\0';

	char* path = malloc(_MAX_PATH);
	path[0] = '\0';


	if (strcmp(filename, "/") != 0)
	{
		if (strcmp(parent, "/") != 0 && strcmp(parent, "") != 0)
		{
			strcat(newfilename, parent);
			strcat(newfilename, "/");
			strcat(newfilename, filename);
		}
		else
		{
			strcpy(newfilename, filename);
		}

		strcat(path, "slccmpt");
		strcat(path, "/");
		strcat(path, newfilename);

		printf("dir: %s\n", path);

		mkdir(path);
		
	}
	else
	{
		strcpy(newfilename, filename);
	}

	free(filename);
	free(path);

	if (fst.sub != 0xffff)
		extractFST(fst.sub, newfilename); 

	free(newfilename);
}

void extractFile(fst_t fst, uint16_t entry, char* parent)
{
	uint16_t fat;
	uint32_t cluster_span = (uint32_t) (fst.size / 0x4000) + 1;
	byte_t* data = calloc(cluster_span * 0x4000, sizeof(byte_t));

	char* filename = malloc(_MAX_PATH);
	snprintf(filename, 13, "%s", fst.filename);
	//strlcpy(filename, (char*) fst.filename, 12);
	stringReplaceAll(":", "-", filename);

	char* newfilename = malloc(_MAX_PATH);
	newfilename[0] = '\0';
	strcat(newfilename, "/");
	strcat(newfilename, parent);
	strcat(newfilename, "/");
	strcat(newfilename, filename);

	char* path = malloc(_MAX_PATH);
	path[0] = '\0';

	strcat(path, "slccmpt");
	strcat(path, newfilename);

	FILE* bf = fopen(path, "wb");
	if(bf == NULL)
	{
		printf("Error opening %s: %d\n", path, errno);
	}

	fat = fst.sub;
	for (int i = 0; fat < 0xFFF0; i++)
	{
		//extracting...
		printf("extracting %s cluster %i\n", fst.filename, i);
		byte_t* cluster = getCluster(fat);
		memcpy((byte_t*) (data + (i * 0x4000)), cluster, 0x4000);
		fat = getFAT(fat);
		free(cluster);
	}

	fwrite(data, fst.size, 1, bf);
	fclose(bf);

	free(data);
	free(filename);
	free(path);

	printf("extracted file: %s\n", newfilename);

	free(newfilename);
}

byte_t* aesDecrypt(byte_t* key, byte_t* enc_data, size_t data_size)
{
	byte_t* dec_data = calloc(data_size, sizeof(byte_t));
	memcpy(dec_data, enc_data, data_size);

	const byte_t* iv = calloc(16, sizeof(byte_t));

	struct AES_ctx ctx;
	AES_init_ctx_iv(&ctx, key, iv);
	
	AES_CBC_decrypt_buffer(&ctx, dec_data, data_size);

	return dec_data;
}

uint16_t bswap16(uint16_t value)
{
	return (uint16_t) ((0x00FF & (value >> 8)) | (0xFF00 & (value << 8)));
}

uint32_t bswap32(uint32_t value)
{
	uint32_t swapped = (((0x000000FF) & (value >> 24))
						| ((0x0000FF00) & (value >> 8))
						| ((0x00FF0000) & (value << 8))
						| ((0xFF000000) & (value << 24)));
	return swapped;
}