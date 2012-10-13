/* Convert binary SID to text SID
 * By Daniel Collins - 2009
 *
 * This code is hereby released to public domain, you may use or redistribute
 * it under any license with no restrictions. I accept no responsibility for the
 * use or effects of this code.
*/

#define __STDC_FORMAT_MACROS

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <string>

/* Convert data to local endianness
 * fe is source endianness (0 = little, 1 = big)
*/
static void from_endian(char *dest, char const *src, int len, int fe) {
	union {
		uint16_t u16;
		uint8_t u8[2];
	} endian;
	
	endian.u16 = 1;
	if(endian.u8[fe]) {
		memcpy(dest, src, len);
	}else{
		for(int pos = 0; pos < (len / 2); pos++) {
			char byte = src[pos];
			
			dest[pos] = src[len-pos-1];
			dest[len-pos-1] = byte;
		}
	}
}

#define RET_PRINTF(...) \
	snprintf(sbuf, 64, __VA_ARGS__); \
	ret.append(sbuf);

#define MINLEN(x) \
	if(len < x) { \
		return ""; \
	}

/* Convert binary SID to text SID
 * Returns an empty string if an invalid SID is supplied
*/
std::string sid2string(char const *bin, unsigned int len) {
	std::string ret;
	char sbuf[64];
	
	MINLEN(8);
	
	unsigned int revision = bin[0];
	unsigned int dashes = bin[1];
	uint32_t i, n;
	
	MINLEN(8 + (4*dashes));
	
	from_endian((char*)&i, bin+4, 4, 1);
	if(revision != 1) {
		return "";
	}
	
	RET_PRINTF("S-%u-%"PRIu32, revision, i);
	
	for(n = 0; n < dashes; n++) {
		from_endian((char*)&i, (bin+8) + (n*4), 4, 0);
		RET_PRINTF("-%"PRIu32, i);
	}
	
	return ret;
}
