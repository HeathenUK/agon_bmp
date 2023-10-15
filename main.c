//Copyright HeathenUK 2023, others' copyrights (Envenomator, Dean Belfield, etc.) unaffected.

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdint.h>
#include <string.h>
#include <eZ80.h>
#include <defines.h>
#include "mos-interface.h"
#include "vdp.h"

typedef struct {
	
	uint16_t bmp_width;
	uint16_t bmp_height;
	uint8_t  bmp_bitdepth;
	
} bmp_info;

extern void write16bit(uint16_t w);
extern void write24bit(uint24_t w);
extern void write32bit(uint32_t w);

void delay_secs(UINT16 ticks_end) { //1 sec ticks
	
	UINT32 ticks = 0;
	ticks_end *= 60;
	while(true) {
		
		waitvblank();
		ticks++;
		if(ticks >= ticks_end) break;
		
	}
	
}

int min(int a, int b) {
    if (a > b)
        return b;
    return a;
}

int max(int a, int b) {
    if (a > b)
        return a;
    return b;
}

void flip(uint32_t * framebuffer, int width, int height) {
    uint16_t y;
    uint32_t * row_buffer = (uint32_t * ) malloc(sizeof(uint32_t) * width);
    int row_size = width * sizeof(uint32_t);

    for (y = 0; y < height / 2; y++) {
        uint32_t * top_row = framebuffer + y * width;
        uint32_t * bottom_row = framebuffer + (height - y - 1) * width;

        memcpy(row_buffer, top_row, row_size);
        memcpy(top_row, bottom_row, row_size);
        memcpy(bottom_row, row_buffer, row_size);
    }

    free(row_buffer);
}

void twiddle_buffer(char* buffer, int width, int height) {
    int row, col;
    char* rowPtr;
	char* oppositeRowPtr;
	char* tempRow = (char*)malloc(width * 4);

    //Iterate over each row
    for (row = 0; row < height / 2; row++) {
        rowPtr = buffer + row * width * 4;
        oppositeRowPtr = buffer + (height - row - 1) * width * 4;

        //Swap bytes within each row (BGRA to RGBA)
        for (col = 0; col < width; col++) {
            tempRow[col * 4] = oppositeRowPtr[col * 4 + 2]; // R
            tempRow[col * 4 + 1] = oppositeRowPtr[col * 4 + 1]; // G
            tempRow[col * 4 + 2] = oppositeRowPtr[col * 4]; // B
            tempRow[col * 4 + 3] = oppositeRowPtr[col * 4 + 3]; // A

            oppositeRowPtr[col * 4] = rowPtr[col * 4 + 2]; // R
            oppositeRowPtr[col * 4 + 1] = rowPtr[col * 4 + 1]; // G
            oppositeRowPtr[col * 4 + 2] = rowPtr[col * 4]; // B
            oppositeRowPtr[col * 4 + 3] = rowPtr[col * 4 + 3]; // A

            rowPtr[col * 4] = tempRow[col * 4]; // R
            rowPtr[col * 4 + 1] = tempRow[col * 4 + 1]; // G
            rowPtr[col * 4 + 2] = tempRow[col * 4 + 2]; // B
            rowPtr[col * 4 + 3] = tempRow[col * 4 + 3]; // A
        }
    }
	free(tempRow);
}

void reorder(char *arr, uint16_t length, BOOL alpha) {
    uint16_t i;
	for (i = 0; i < length; i += 4) {
        if (i + 2 < length) {
            uint8_t temp = arr[i];
            arr[i] = arr[i + 2];
            arr[i + 2] = temp;
			if (alpha == false) arr[i + 3] = 0xFF;
        }
    }
}

void reorder_and_insert(char *arr, uint16_t length, char **new_arr, uint16_t *new_length, char insert_value) {

	uint16_t i, j = 0;
    *new_length = (length / 3) * 4 + (length % 3);
    *new_arr = (char *) malloc(*new_length * sizeof(char));

    for (i = 0; i < length; i += 3) {
        
        (*new_arr)[j] = (i + 2 < length) ? arr[i + 2] : 0;
        (*new_arr)[j + 1] = (i + 1 < length) ? arr[i + 1] : 0;
        (*new_arr)[j + 2] = arr[i];
        
        (*new_arr)[j + 3] = 0xFF;

        j += 4;
    }
	free(new_arr);
}

bmp_info load_bmp_big(const char * filename, UINT8 slot) { //Uses 64x64x4 chunks

    int32_t image_start, width, height, bit_depth, row_padding = 0, y, x, i;
	char* row_24bpp;
    uint8_t pixel[4], file, r, g, b, index;
    char header[128], color_table[1024];
	char small_header[18];
    uint32_t pixel_value, color_table_size, bytes_per_row, alphamask;
    uint32_t biSize;
    FIL * fo;
	bmp_info return_info;

    char * src;
    char * image_buffer;
	
	//if (game.vgm_file != NULL) parse_vgm_file(game.vgm_file);
	
	return_info.bmp_width = 0;
	return_info.bmp_height = 0;
	return_info.bmp_bitdepth = 0;	

    file = mos_fopen(filename, fa_read);
    if (!file) {
        printf("Error: could not open %s.\r\n", filename);
        return return_info;
    }
    fo = (FIL * ) mos_getfil(file);

    mos_fread(file, small_header, 18);
	biSize = * (uint32_t * ) & small_header[14];
	mos_flseek(file,0);
	mos_fread(file, header, biSize);

	image_start = * (uint32_t * ) & header[10];
    biSize = * (uint32_t * ) & header[14];
    width = * (INT32 * ) & header[18];
    height = * (INT32 * ) & header[22];
    bit_depth = * (uint16_t * ) & header[28];
    color_table_size = * (uint32_t * ) & header[46];
	alphamask =  * (uint32_t * ) & header[52];
	
    image_buffer = (char * ) malloc(width * bit_depth / 8);

    if (color_table_size == 0 && bit_depth == 8) {
        color_table_size = 256;
    }

    if (color_table_size > 0) mos_fread(file, color_table, color_table_size * 4);
	
    if ((bit_depth != 32) && (bit_depth != 24) && (bit_depth != 8)) {
        printf("Error: unsupported bit depth (not 8, 24 or 32-bit).\n");
        mos_fclose(file);
        return return_info;
    }
	
    row_padding = (4 - (width * (bit_depth / 8)) % 4) % 4;

	//clear_buffer(slot);
	
    vdp_bitmapSelect(slot);
    putch(23); // vdu_sys
    putch(27); // sprite command
    putch(1); // send data to selected bitmap

    write16bit(width);
    write16bit(height);
	
    if (bit_depth == 8) {
		uint8_t a = 0xFF;
		int non_pad_row = width * bit_depth / 8;
		mos_flseek(file, image_start + ((height - 1) * (non_pad_row + row_padding)));
		
        for (y = height - 1; y >= 0; y--) {
            for (x = 0; x < width; x++) {

                index = (char) mos_fgetc(file);
                b = color_table[index * 4];
                g = color_table[index * 4 + 1];
                r = color_table[index * 4 + 2];
				
				putch(r);
				putch(g);
				putch(b);
				putch(0xFF);
				

            }
			
			//add_stream_to_buffer(slot,row_rgba2222,width);
			mos_flseek(file, fo -> fptr - ((non_pad_row * 2) + row_padding));
            // for (i = 0; i < row_padding; i++) {
                // mos_fgetc(file);
            // }

        }

    } else if (bit_depth == 32) {
        
		int non_pad_row = width * bit_depth / 8;
        src = (char * ) malloc(width * bit_depth / 8);
		mos_flseek(file, image_start + ((height - 1) * (non_pad_row + row_padding)));

        for (y = height - 1; y >= 0; y--) {

            mos_fread(file, src, non_pad_row);
			if (alphamask == 0) reorder(src, non_pad_row, false);
			else reorder(src, non_pad_row, true);
            mos_puts(src, non_pad_row, 0);
			//add_stream_to_buffer(slot,src,non_pad_row);
            mos_flseek(file, fo -> fptr - ((non_pad_row * 2) + row_padding));
			free(src);			

        }

    } else if (bit_depth == 24) {
		
		uint16_t new_row_size;
		int non_pad_row = width * bit_depth / 8;
		
        src = (char * ) malloc(width * bit_depth / 8);
		mos_flseek(file, image_start + ((height - 1) * (non_pad_row + row_padding)));
		
        for (y = height - 1; y >= 0; y--) {

            mos_fread(file, src, non_pad_row);
			reorder_and_insert(src, non_pad_row, &row_24bpp, &new_row_size, 0xFF);
            mos_puts(row_24bpp, new_row_size, 0);
			//add_stream_to_buffer(slot,row_24bpp,new_row_size);
            mos_flseek(file, fo -> fptr - ((non_pad_row * 2) + row_padding));
			free(row_24bpp);
			free(src);

        }		
		
	}

	//assign_buffer_to_bitmap(slot,0,width,height);
	
    mos_fclose(file);
    free(image_buffer);
    //return width * height;
	return_info.bmp_width = width;
	return_info.bmp_height = height;
	return_info.bmp_bitdepth = bit_depth;
	return return_info;

}

uint16_t strtou16(const char *str) {
    uint16_t result = 0;
    const uint16_t maxDiv10 = 6553;  // 65535 / 10
    const uint16_t maxMod10 = 5;     // 65535 % 10

    while (*str == ' ' || *str == '\t' || *str == '\n') {
        str++;
    }

    while (*str >= '0' && *str <= '9') {
        uint16_t digit = *str - '0';
        if (result > maxDiv10 || (result == maxDiv10 && digit > maxMod10)) {
            return 65535;
        }
        result = result * 10 + digit;
        str++;
    }

    return result;
}

uint8_t strtou8(const char *str) {
    uint8_t result = 0;
    const uint8_t maxDiv10 = 255 / 10;
    const uint8_t maxMod10 = 255 % 10;

    while (*str == ' ' || *str == '\t' || *str == '\n') {
        str++;
    }

    while (*str >= '0' && *str <= '9') {
        uint8_t digit = *str - '0';
        if (result > maxDiv10 || (result == maxDiv10 && digit > maxMod10)) {
            return 255;
        }
        result = result * 10 + digit;
        str++;
    }

    return result;
}

uint24_t strtou24(const char *str) {
    uint32_t result = 0;
    const uint32_t maxDiv10 = 1677721;
    const uint32_t maxMod10 = 5;

    while (*str == ' ' || *str == '\t' || *str == '\n') {
        str++;
    }
	
    while (*str >= '0' && *str <= '9') {
        uint32_t digit = *str - '0';
        if (result > maxDiv10 || (result == maxDiv10 && digit > maxMod10)) {
            return 16777215;
        }
        result = result * 10 + digit;
        str++;
    }

    return result;
}

int main(int argc, char * argv[]) {

    uint24_t x, y;
	uint8_t bitmap_slot = 0;
	bmp_info bmp;
	
	//Args = 0:binary name, 1:filname, 2:slot, 3:topleft, 3:topright
	
	if ((argc < 2) || (argc == 4) || (argc > 5)) {
        printf("Usage is %s <filename> [bitmap slot] [top-left x] [top-left y]\r\n", argv[0]);
        return 0;
    }
	
	if (argc > 2) bitmap_slot = strtou8(argv[2]);
	
    //vdp_mode(8);
	
	if (argc == 2) {

		bmp = load_bmp_big(argv[1], 0);
		
	} else if (argc == 3) {
		
		bmp = load_bmp_big(argv[1], bitmap_slot);
		
	} else if (argc == 5) {
	
		bmp = load_bmp_big(argv[1], bitmap_slot);
		
		if (argv[3][0] == 'C' || argv[3][0] == 'c') x = (getsysvar_scrwidth() - bmp.bmp_width) / 2;
		else x = strtou16(argv[4]);
		
		if (argv[4][0] == 'C' || argv[4][0] == 'c') y = (getsysvar_scrheight() - bmp.bmp_height) / 2;
		else y = strtou16(argv[4]);
		
		vdp_bitmapDraw(bitmap_slot,x,y);
		
	}

    return 0;
}