#include "AES.h"
#include <assert.h>
#include <time.h>

AES::AES(uint8_t *key, size_t n, int type = 128):AES_TYPE(type),Nb(type/32) {
	assert(AES_TYPE == 128 || AES_TYPE == 192 || AES_TYPE == 256);
	assert(n == 16 || n == 24 || n == 32);
	switch(n) {
		default:
		case 16: Nk = 4; Nr = 10; break;
		case 24: Nk = 6; Nr = 12; break;
		case 32: Nk = 8; Nr = 14; break;
	}

	kep = new uint8_t[Nb*(Nr+1)*4];
	key_expansion(key);
}

AES::~AES() {
	delete [] kep;
}

void AES::cipher(uint8_t *in, uint8_t *out) {
	uint8_t *state = new uint8_t[4*Nb];

	for (int i = 0; i < 4; ++i)
		for (int j = 0; j < Nb; ++j)
			state[Nb*i+j] = in[i+4*j];
	add_round_key(state,0);

	for (int r = 1; r < Nr; ++r) {
		sub_bytes(state);
		shift_rows(state);
		mix_columns(state);
		add_round_key(state,r);
	}

	// last round
	sub_bytes(state);
	shift_rows(state);
	add_round_key(state, Nr);

	for (int i = 0; i < 4; ++i)
		for (int j = 0; j < Nb; ++j)
			out[i+4*j] = state[Nb*i+j];

	delete [] state;
}

void AES::decipher(uint8_t *in, uint8_t *out) {
	uint8_t *state = new uint8_t[4*Nb];

	for (int i = 0; i < 4; ++i)
		for (int j = 0; j < Nb; ++j)
			state[Nb*i+j] = in[i+4*j];
	add_round_key(state,Nr);

	for (int r = Nr-1; r >= 1; --r) {
		inv_shift_rows(state);
		inv_sub_bytes(state);
		add_round_key(state,r);
		inv_mix_columns(state);
	}

	// last round
	inv_shift_rows(state);
	inv_sub_bytes(state);
	add_round_key(state, 0);

	for (int i = 0; i < 4; ++i)
		for (int j = 0; j < Nb; ++j)
			out[i+4*j] = state[Nb*i+j];

	delete [] state;
}

/* REF: http://en.wikipedia.org/wiki/Finite_field_arithmetic */
uint8_t AES::gf_mul(uint8_t a, uint8_t b) {
	uint8_t ret = 0, carry;
	for (int i=0; i<8; ++i) {
		if (b&1) ret ^= a;
		carry = a & 0x80;
		a <<= 1;
		if (carry) a ^= 0x1b; //m(x) = x8 + x4 + x3 + x + 1
		b >>= 1;
	}
	return (uint8_t) ret;
}

void AES::gf_addword(uint8_t *a, uint8_t *b, uint8_t *ret) {
	for (int i=0; i<4; ++i) ret[i] = a[i]^b[i];
}

void AES::gf_mulword(uint8_t *a, uint8_t *b, uint8_t *ret) {
	const uint8_t a_index[] = {0,3,2,1};
	for (int i=0; i<4; ++i) {
		ret[i] = 0;
		for (int j=0; j<4; ++j) ret[i] ^= gf_mul(a[a_index[(j-i+4)%4]],b[j]);
	}
}

void AES::sub_word(uint8_t *word) {
	for (int i=0; i<4; ++i) {
		word[i] = sbox[16*((word[i] & 0xf0) >> 4) + (word[i] & 0x0f)];
	}
}

void AES::sub_byte(uint8_t &byte, uint8_t *box) {
	byte = box[16*(((byte) & 0xf0) >> 4) + ((byte) & 0x0f)];
}

void AES::lrotate_word(uint8_t *word) {
	uint8_t tmp = word[0];
	for (int i=0; i<3; ++i) word[i] = word[i+1];
	word[3] = tmp;
}

uint8_t AES::round_con(int i) {
	assert (i > 0);
	if (i == 1) return 0x01;
	else {
		uint8_t R = 0x02;
		while(--i>1) {
			R = gf_mul(R,0x02);
		}
		return R;
	}
}

void AES::key_expansion(uint8_t *key) {
	int ttl_len = Nb*(Nr+1);
	uint8_t tmp[4];
	for (int i=0; i<Nk; ++i)
		for (int j=0; j<4; ++j)
			kep[4*i+j] = key[4*i+j];
	for (int i=Nk; i<ttl_len; ++i) {
		for (int j=0; j<4; ++j) tmp[j] = kep[4*(i-1)+j];
		if (i%Nk == 0){
			lrotate_word(tmp);
			sub_word(tmp);
			tmp[0] ^= round_con(i/Nk);
		} else if (Nk > 6 && i%Nk == 4){ // Nk == 8 and i%8 == 4
			sub_word(tmp);
		}
		for (int j=0; j<4; ++j) kep[4*i+j] = kep[4*(i-Nk)+j]^tmp[j];
	}
	
}

void AES::add_round_key(uint8_t *state, int rnd) {
	for (int i=0; i<Nb; ++i)
		for (int j=0; j<4; ++j) {
			state[Nb*j+i] ^= kep[4*Nb*rnd+4*i+j];
		}
}

void AES::_sub_bytes(uint8_t *state, uint8_t *box) {
	for (int i=0; i<4; ++i)
		for (int j=0; j<Nb; ++j) {
			sub_byte(state[Nb*i+j],box);
		}
}

void AES::_mix_columns(uint8_t *state, bool isinv) {
	static uint8_t A[]={0x02, 0x01, 0x01, 0x03};
	static uint8_t INVA[]={0x0e, 0x09, 0x0d, 0x0b};
	uint8_t *a = isinv?INVA:A;
	uint8_t tmp1[4], tmp2[4];
	for (int i=0; i<Nb; ++i) {
		for (int j=0; j<4; ++j)
			tmp1[j] = state[Nb*j+i];
		gf_mulword(a,tmp1,tmp2);
		for (int j=0; j<4; ++j)
			state[Nb*j+i] = tmp2[j];
	}
}

void AES::shift_rows(uint8_t *state) {
	static int LARGE[]={0,1,2,4};
	static int SMALL[]={0,1,2,3};

	int *dist = Nb>6?LARGE:SMALL;
	uint8_t tmp;
	for (int i=1; i<4; ++i) {
		int t = 0;
		while (t<dist[i]){
			tmp = state[Nb*i+0];
			for (int j = 1; j < Nb; j++) {
				state[Nb*i+j-1] = state[Nb*i+j];
			}
			state[Nb*i+Nb-1] = tmp;
			++t;
		}
	}
}

void AES::inv_shift_rows(uint8_t *state) {
	static int LARGE[]={0,1,2,4};
	static int SMALL[]={0,1,2,3};

	int *dist = Nb>6?LARGE:SMALL;
	uint8_t tmp;
	for (int i=1; i<4; ++i) {
		int t = 0;
		while (t<dist[i]){
			tmp = state[Nb*i+Nb-1];
			for (int j = Nb-1; j > 0; j--) {
				state[Nb*i+j] = state[Nb*i+j-1];
			}
			state[Nb*i+0] = tmp;
			++t;
		}
	}
}

uint8_t AES::sbox[256] = {
	// 0     1     2     3     4     5     6     7     8     9     a     b     c     d     e     f
	0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76, // 0
	0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0, // 1
	0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15, // 2
	0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75, // 3
	0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84, // 4
	0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf, // 5
	0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8, // 6
	0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2, // 7
	0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73, // 8
	0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb, // 9
	0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79, // a
	0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08, // b
	0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a, // c
	0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e, // d
	0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf, // e
	0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16 	// f
};

uint8_t AES::inv_sbox[256] = {
	// 0     1     2     3     4     5     6     7     8     9     a     b     c     d     e     f
	0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb, // 0
	0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb, // 1
	0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e, // 2
	0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25, // 3
	0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92, // 4
	0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84, // 5
	0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06, // 6
	0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b, // 7
	0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73, // 8
	0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e, // 9
	0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b, // a
	0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4, // b
	0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f, // c
	0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef, // d
	0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61, // e
	0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d  // f
};


void demoAES() {
	uint8_t key[] = {
		0x00, 0x01, 0x02, 0x03,
		0x04, 0x05, 0x06, 0x07,
		0x08, 0x09, 0x0a, 0x0b,
		0x0c, 0x0d, 0x0e, 0x0f,
		0x10, 0x11, 0x12, 0x13,
		0x14, 0x15, 0x16, 0x17,
		0x18, 0x19, 0x1a, 0x1b,
		0x1c, 0x1d, 0x1e, 0x1f};
	 // uint8_t key[] = {
		// 0x00, 0x01, 0x02, 0x03, 
		// 0x04, 0x05, 0x06, 0x07, 
		// 0x08, 0x09, 0x0a, 0x0b, 
		// 0x0c, 0x0d, 0x0e, 0x0f}; 

	uint8_t in[] = {
		0x00, 0x11, 0x22, 0x33,
		0x44, 0x55, 0x66, 0x77,
		0x88, 0x99, 0xaa, 0xbb,
		0x88, 0x99, 0xaa, 0xbb,
		0xcc, 0xdd, 0xee, 0xff,
		0xcc, 0xdd, 0xee, 0xff,
		0x88, 0x99, 0xaa, 0xbb,
		0xcc, 0xdd, 0xee, 0xff};
	
	uint8_t out[sizeof(in)]; // 128
	uint8_t din[sizeof(in)];
	printf("original msg:\n");
	int i;
	for (i = 0; i < sizeof(in)/4; ++i) {
		printf("%x %x %x %x ", in[4*i+0], in[4*i+1], in[4*i+2], in[4*i+3]);
	}
	printf("\n");
	AES aes(key,sizeof(key),sizeof(in)*BITS_PER_BYTES);
	// aes.show_kep();
	aes.cipher(in,out);

	printf("out:\n");
	
	for (i = 0; i < sizeof(in)/4; ++i) {
		printf("%x %x %x %x ", out[4*i+0], out[4*i+1], out[4*i+2], out[4*i+3]);
	}
	printf("\n");

	AES daes(key,sizeof(key),sizeof(in)*BITS_PER_BYTES);
	daes.decipher(out,din);
	// daes.show_kep();
	printf("decipher msg:\n");
	for (i = 0; i < sizeof(in)/4; ++i) {
		printf("%x %x %x %x ", din[4*i+0], din[4*i+1], din[4*i+2], din[4*i+3]);
	}
	printf("\n");
}

void testAES(int aestype=128, int keylen=192, int repeat_time=1000) {
	assert(keylen == 192 || keylen == 128 || keylen == 256);
	assert(aestype == 192 || aestype == 128 || aestype == 256);
	printf("Testing AES%d with keylen %d ...\n", aestype,keylen);
	fflush(stdout);
	srand(time(0));
	
	uint8_t *key = new uint8_t[keylen/BITS_PER_BYTES];
	uint8_t *in = new uint8_t[aestype/BITS_PER_BYTES];
	uint8_t *out = new uint8_t[aestype/BITS_PER_BYTES];
	uint8_t *din = new uint8_t[aestype/BITS_PER_BYTES];
	
	// first generate key;
	for (int i=0; i<keylen/BITS_PER_BYTES; ++i) key[i] = rand()%MAX_VALUE;
	AES aes(key,keylen/BITS_PER_BYTES,aestype), daes(key,keylen/BITS_PER_BYTES,aestype);
	// second generate input
	for (int t=0; t<repeat_time; ++t) {
		for (int i=0; i<aestype/BITS_PER_BYTES; ++i) in[i] = rand()%MAX_VALUE;
		aes.cipher(in,out);
		// aes.show_kep();
		daes.decipher(out,din);
		for (int i=0; i<aestype/BITS_PER_BYTES; ++i) {
			if (in[i] != din[i]) {
				printf("[Test Result] Failed !!!\n");
				goto TEST_FIN;
				
			}
		}
	}
	printf("[Test Result] Passed all %d tests !!!\n",repeat_time );
	fflush(stdout);

	TEST_FIN:
	delete [] key;
	delete [] in;
	delete [] out;
	delete [] din;
}

int main(int argc, char const *argv[])
{
	for (int type=128;type<=256;type+=64)
		for (int klen=128;klen<=256;klen+=64)
			testAES(type,klen);
	// demoAES();
	return 0;
}
