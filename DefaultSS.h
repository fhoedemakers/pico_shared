#pragma once
#if !HSTX
extern const unsigned char DefaultSS160_444[]; 
extern const unsigned int DefaultSS160_444_len;
#define DEFAULT_SS DefaultSS160_444
#define DEFAULT_SS_LEN DefaultSS160_444_len
#else
extern const unsigned char DefaultSS160_555[]; 
extern const unsigned int DefaultSS160_555_len;
#define DEFAULT_SS DefaultSS160_555
#define DEFAULT_SS_LEN DefaultSS160_555_len
#endif