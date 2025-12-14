
#ifndef LP_LODEPNG_H
#define LP_LODEPNG_H
#include <string.h> 
extern const char* LP_LODEPNG_VERSION_STRING;
#ifndef LODEPNG_NO_COMPILE_ZLIB
#define LODEPNG_COMPILE_ZLIB
#endif
#ifndef LODEPNG_NO_COMPILE_PNG
#define LODEPNG_COMPILE_PNG
#endif
#ifndef LODEPNG_NO_COMPILE_DECODER
#define LODEPNG_COMPILE_DECODER
#endif
#ifndef LODEPNG_NO_COMPILE_ENCODER
#define LODEPNG_COMPILE_ENCODER
#endif
#ifndef LODEPNG_NO_COMPILE_DISK
#define LODEPNG_COMPILE_DISK
#endif
#ifndef LODEPNG_NO_COMPILE_ANCILLARY_CHUNKS
#define LODEPNG_COMPILE_ANCILLARY_CHUNKS
#endif
#ifndef LODEPNG_NO_COMPILE_ERROR_TEXT
#define LODEPNG_COMPILE_ERROR_TEXT
#endif
#ifndef LODEPNG_NO_COMPILE_ALLOCATORS
#define LODEPNG_COMPILE_ALLOCATORS
#endif
#ifndef LODEPNG_NO_COMPILE_CRC
#define LODEPNG_COMPILE_CRC
#endif
#ifdef __cplusplus
#ifndef LODEPNG_NO_COMPILE_CPP
#define LODEPNG_COMPILE_CPP
#endif
#endif
#ifdef LODEPNG_COMPILE_CPP
#include <vector>
#include <string>
#endif 
#ifdef LODEPNG_COMPILE_PNG
typedef enum LP_LodePNGColorType {
  LP_LCT_GREY = 0, 
  LP_LCT_RGB = 2, 
  LP_LCT_PALETTE = 3, 
  LP_LCT_GREY_ALPHA = 4, 
  LP_LCT_RGBA = 6, 
  LP_LCT_MAX_OCTET_VALUE = 255
} LP_LodePNGColorType;
#ifdef LODEPNG_COMPILE_DECODER
unsigned LP_lodepng_decode_memory(unsigned char** out, unsigned* w, unsigned* h,
                               const unsigned char* in, size_t insize,
                               LP_LodePNGColorType colortype, unsigned bitdepth);
unsigned LP_lodepng_decode32(unsigned char** out, unsigned* w, unsigned* h,
                          const unsigned char* in, size_t insize);
unsigned LP_lodepng_decode24(unsigned char** out, unsigned* w, unsigned* h,
                          const unsigned char* in, size_t insize);
#ifdef LODEPNG_COMPILE_DISK
unsigned LP_lodepng_decode_file(unsigned char** out, unsigned* w, unsigned* h,
                             const char* filename,
                             LP_LodePNGColorType colortype, unsigned bitdepth);
unsigned LP_lodepng_decode32_file(unsigned char** out, unsigned* w, unsigned* h,
                               const char* filename);
unsigned LP_lodepng_decode24_file(unsigned char** out, unsigned* w, unsigned* h,
                               const char* filename);
#endif 
#endif 
#ifdef LODEPNG_COMPILE_ENCODER
unsigned LP_lodepng_encode_memory(unsigned char** out, size_t* outsize,
                               const unsigned char* image, unsigned w, unsigned h,
                               LP_LodePNGColorType colortype, unsigned bitdepth);
unsigned LP_lodepng_encode32(unsigned char** out, size_t* outsize,
                          const unsigned char* image, unsigned w, unsigned h);
unsigned LP_lodepng_encode24(unsigned char** out, size_t* outsize,
                          const unsigned char* image, unsigned w, unsigned h);
#ifdef LODEPNG_COMPILE_DISK
unsigned LP_lodepng_encode_file(const char* filename,
                             const unsigned char* image, unsigned w, unsigned h,
                             LP_LodePNGColorType colortype, unsigned bitdepth);
unsigned LP_lodepng_encode32_file(const char* filename,
                               const unsigned char* image, unsigned w, unsigned h);
unsigned LP_lodepng_encode24_file(const char* filename,
                               const unsigned char* image, unsigned w, unsigned h);
#endif 
#endif 
#ifdef LODEPNG_COMPILE_CPP
namespace lodepng {
#ifdef LODEPNG_COMPILE_DECODER
unsigned decode(std::vector<unsigned char>& out, unsigned& w, unsigned& h,
                const unsigned char* in, size_t insize,
                LP_LodePNGColorType colortype = LP_LCT_RGBA, unsigned bitdepth = 8);
unsigned decode(std::vector<unsigned char>& out, unsigned& w, unsigned& h,
                const std::vector<unsigned char>& in,
                LP_LodePNGColorType colortype = LP_LCT_RGBA, unsigned bitdepth = 8);
#ifdef LODEPNG_COMPILE_DISK
unsigned decode(std::vector<unsigned char>& out, unsigned& w, unsigned& h,
                const std::string& filename,
                LP_LodePNGColorType colortype = LP_LCT_RGBA, unsigned bitdepth = 8);
#endif 
#endif 
#ifdef LODEPNG_COMPILE_ENCODER
unsigned encode(std::vector<unsigned char>& out,
                const unsigned char* in, unsigned w, unsigned h,
                LP_LodePNGColorType colortype = LP_LCT_RGBA, unsigned bitdepth = 8);
unsigned encode(std::vector<unsigned char>& out,
                const std::vector<unsigned char>& in, unsigned w, unsigned h,
                LP_LodePNGColorType colortype = LP_LCT_RGBA, unsigned bitdepth = 8);
#ifdef LODEPNG_COMPILE_DISK
unsigned encode(const std::string& filename,
                const unsigned char* in, unsigned w, unsigned h,
                LP_LodePNGColorType colortype = LP_LCT_RGBA, unsigned bitdepth = 8);
unsigned encode(const std::string& filename,
                const std::vector<unsigned char>& in, unsigned w, unsigned h,
                LP_LodePNGColorType colortype = LP_LCT_RGBA, unsigned bitdepth = 8);
#endif 
#endif 
} 
#endif 
#endif 
#ifdef LODEPNG_COMPILE_ERROR_TEXT
const char* LP_lodepng_error_text(unsigned code);
#endif 
#ifdef LODEPNG_COMPILE_DECODER
typedef struct LP_LodePNGDecompressSettings LP_LodePNGDecompressSettings;
struct LP_LodePNGDecompressSettings {
  unsigned ignore_adler32; 
  unsigned ignore_nlen; 
  size_t max_output_size;
  unsigned (*custom_zlib)(unsigned char**, size_t*,
                          const unsigned char*, size_t,
                          const LP_LodePNGDecompressSettings*);
  unsigned (*custom_inflate)(unsigned char**, size_t*,
                             const unsigned char*, size_t,
                             const LP_LodePNGDecompressSettings*);
  const void* custom_context; 
};
extern const LP_LodePNGDecompressSettings LP_lodepng_default_decompress_settings;
void LP_lodepng_decompress_settings_init(LP_LodePNGDecompressSettings* settings);
#endif 
#ifdef LODEPNG_COMPILE_ENCODER
typedef struct LP_LodePNGCompressSettings LP_LodePNGCompressSettings;
struct LP_LodePNGCompressSettings  {
  unsigned btype; 
  unsigned use_lz77; 
  unsigned windowsize; 
  unsigned minmatch; 
  unsigned nicematch; 
  unsigned lazymatching; 
  unsigned (*custom_zlib)(unsigned char**, size_t*,
                          const unsigned char*, size_t,
                          const LP_LodePNGCompressSettings*);
  unsigned (*custom_deflate)(unsigned char**, size_t*,
                             const unsigned char*, size_t,
                             const LP_LodePNGCompressSettings*);
  const void* custom_context; 
};
extern const LP_LodePNGCompressSettings LP_lodepng_default_compress_settings;
void LP_lodepng_compress_settings_init(LP_LodePNGCompressSettings* settings);
#endif 
#ifdef LODEPNG_COMPILE_PNG
typedef struct LP_LodePNGColorMode {
  LP_LodePNGColorType colortype; 
  unsigned bitdepth;  
  unsigned char* palette; 
  size_t palettesize; 
  unsigned key_defined; 
  unsigned key_r;       
  unsigned key_g;       
  unsigned key_b;       
} LP_LodePNGColorMode;
void LP_lodepng_color_mode_init(LP_LodePNGColorMode* info);
void LP_lodepng_color_mode_cleanup(LP_LodePNGColorMode* info);
unsigned LP_lodepng_color_mode_copy(LP_LodePNGColorMode* dest, const LP_LodePNGColorMode* source);
LP_LodePNGColorMode LP_lodepng_color_mode_make(LP_LodePNGColorType colortype, unsigned bitdepth);
void LP_lodepng_palette_clear(LP_LodePNGColorMode* info);
unsigned LP_lodepng_palette_add(LP_LodePNGColorMode* info,
                             unsigned char r, unsigned char g, unsigned char b, unsigned char a);
unsigned LP_lodepng_get_bpp(const LP_LodePNGColorMode* info);
unsigned LP_lodepng_get_channels(const LP_LodePNGColorMode* info);
unsigned LP_lodepng_is_greyscale_type(const LP_LodePNGColorMode* info);
unsigned LP_lodepng_is_alpha_type(const LP_LodePNGColorMode* info);
unsigned LP_lodepng_is_palette_type(const LP_LodePNGColorMode* info);
unsigned LP_lodepng_has_palette_alpha(const LP_LodePNGColorMode* info);
unsigned LP_lodepng_can_have_alpha(const LP_LodePNGColorMode* info);
size_t LP_lodepng_get_raw_size(unsigned w, unsigned h, const LP_LodePNGColorMode* color);
#ifdef LODEPNG_COMPILE_ANCILLARY_CHUNKS
typedef struct LP_LodePNGTime {
  unsigned year;    
  unsigned month;   
  unsigned day;     
  unsigned hour;    
  unsigned minute;  
  unsigned second;  
} LP_LodePNGTime;
#endif 
typedef struct LP_LodePNGInfo {
  unsigned compression_method;
  unsigned filter_method;     
  unsigned interlace_method;  
  LP_LodePNGColorMode color;     
#ifdef LODEPNG_COMPILE_ANCILLARY_CHUNKS
  unsigned background_defined; 
  unsigned background_r;       
  unsigned background_g;       
  unsigned background_b;       
  size_t text_num; 
  char** text_keys; 
  char** text_strings; 
  size_t itext_num; 
  char** itext_keys; 
  char** itext_langtags; 
  char** itext_transkeys; 
  char** itext_strings; 
  unsigned exif_defined; 
  unsigned char* exif; 
  unsigned exif_size; 
  unsigned time_defined; 
  LP_LodePNGTime time;
  unsigned phys_defined; 
  unsigned phys_x; 
  unsigned phys_y; 
  unsigned phys_unit; 
  unsigned gama_defined; 
  unsigned gama_gamma;   
  unsigned chrm_defined; 
  unsigned chrm_white_x; 
  unsigned chrm_white_y; 
  unsigned chrm_red_x;   
  unsigned chrm_red_y;   
  unsigned chrm_green_x; 
  unsigned chrm_green_y; 
  unsigned chrm_blue_x;  
  unsigned chrm_blue_y;  
  unsigned srgb_defined; 
  unsigned srgb_intent;  
  unsigned iccp_defined;      
  char* iccp_name;            
  unsigned char* iccp_profile;
  unsigned iccp_profile_size; 
  unsigned cicp_defined; 
  unsigned cicp_color_primaries; 
  unsigned cicp_transfer_function; 
  unsigned cicp_matrix_coefficients; 
  unsigned cicp_video_full_range_flag; 
  unsigned mdcv_defined; 
  unsigned mdcv_red_x;   
  unsigned mdcv_red_y;   
  unsigned mdcv_green_x; 
  unsigned mdcv_green_y; 
  unsigned mdcv_blue_x;  
  unsigned mdcv_blue_y;  
  unsigned mdcv_white_x; 
  unsigned mdcv_white_y; 
  unsigned mdcv_max_luminance; 
  unsigned mdcv_min_luminance; 
  unsigned clli_defined; 
  unsigned clli_max_cll; 
  unsigned clli_max_fall; 
  unsigned sbit_defined; 
  unsigned sbit_r;       
  unsigned sbit_g;       
  unsigned sbit_b;       
  unsigned sbit_a;       
  unsigned char* unknown_chunks_data[3];
  size_t unknown_chunks_size[3]; 
#endif 
} LP_LodePNGInfo;
void LP_lodepng_info_init(LP_LodePNGInfo* info);
void LP_lodepng_info_cleanup(LP_LodePNGInfo* info);
unsigned LP_lodepng_info_copy(LP_LodePNGInfo* dest, const LP_LodePNGInfo* source);
#ifdef LODEPNG_COMPILE_ANCILLARY_CHUNKS
unsigned LP_lodepng_add_text(LP_LodePNGInfo* info, const char* key, const char* str); 
void LP_lodepng_clear_text(LP_LodePNGInfo* info); 
unsigned LP_lodepng_add_itext(LP_LodePNGInfo* info, const char* key, const char* langtag,
                           const char* transkey, const char* str); 
void LP_lodepng_clear_itext(LP_LodePNGInfo* info); 
unsigned LP_lodepng_set_icc(LP_LodePNGInfo* info, const char* name, const unsigned char* profile, unsigned profile_size);
void LP_lodepng_clear_icc(LP_LodePNGInfo* info); 
unsigned LP_lodepng_set_exif(LP_LodePNGInfo* info, const unsigned char* exif, unsigned exif_size);
void LP_lodepng_clear_exif(LP_LodePNGInfo* info); 
#endif 
unsigned LP_lodepng_convert(unsigned char* out, const unsigned char* in,
                         const LP_LodePNGColorMode* mode_out, const LP_LodePNGColorMode* mode_in,
                         unsigned w, unsigned h);
#ifdef LODEPNG_COMPILE_DECODER
typedef struct LP_LodePNGDecoderSettings {
  LP_LodePNGDecompressSettings zlibsettings; 
  unsigned ignore_crc; 
  unsigned ignore_critical; 
  unsigned ignore_end; 
  unsigned color_convert; 
#ifdef LODEPNG_COMPILE_ANCILLARY_CHUNKS
  unsigned read_text_chunks; 
  unsigned remember_unknown_chunks;
  size_t max_text_size;
  size_t max_icc_size;
#endif 
} LP_LodePNGDecoderSettings;
void LP_lodepng_decoder_settings_init(LP_LodePNGDecoderSettings* settings);
#endif 
#ifdef LODEPNG_COMPILE_ENCODER
typedef enum LP_LodePNGFilterStrategy {
  LP_LFS_ZERO = 0,
  LP_LFS_ONE = 1,
  LP_LFS_TWO = 2,
  LP_LFS_THREE = 3,
  LP_LFS_FOUR = 4,
  LP_LFS_MINSUM,
  LP_LFS_ENTROPY,
  LP_LFS_BRUTE_FORCE,
  LP_LFS_PREDEFINED
} LP_LodePNGFilterStrategy;
typedef struct LP_LodePNGColorStats {
  unsigned colored; 
  unsigned key; 
  unsigned short key_r; 
  unsigned short key_g;
  unsigned short key_b;
  unsigned alpha; 
  unsigned numcolors; 
  unsigned char palette[1024]; 
  unsigned bits; 
  size_t numpixels;
  unsigned allow_palette; 
  unsigned allow_greyscale; 
} LP_LodePNGColorStats;
void LP_lodepng_color_stats_init(LP_LodePNGColorStats* stats);
unsigned LP_lodepng_compute_color_stats(LP_LodePNGColorStats* stats,
                                     const unsigned char* image, unsigned w, unsigned h,
                                     const LP_LodePNGColorMode* mode_in);
typedef struct LP_LodePNGEncoderSettings {
  LP_LodePNGCompressSettings zlibsettings; 
  unsigned auto_convert;
  unsigned filter_palette_zero;
  LP_LodePNGFilterStrategy filter_strategy;
  const unsigned char* predefined_filters;
  unsigned force_palette;
#ifdef LODEPNG_COMPILE_ANCILLARY_CHUNKS
  unsigned add_id;
  unsigned text_compression;
#endif 
} LP_LodePNGEncoderSettings;
void LP_lodepng_encoder_settings_init(LP_LodePNGEncoderSettings* settings);
#endif 
#if defined(LODEPNG_COMPILE_DECODER) || defined(LODEPNG_COMPILE_ENCODER)
typedef struct LP_LodePNGState {
#ifdef LODEPNG_COMPILE_DECODER
  LP_LodePNGDecoderSettings decoder; 
#endif 
#ifdef LODEPNG_COMPILE_ENCODER
  LP_LodePNGEncoderSettings encoder; 
#endif 
  LP_LodePNGColorMode info_raw; 
  LP_LodePNGInfo info_png; 
  unsigned error;
} LP_LodePNGState;
void LP_lodepng_state_init(LP_LodePNGState* state);
void LP_lodepng_state_cleanup(LP_LodePNGState* state);
void LP_lodepng_state_copy(LP_LodePNGState* dest, const LP_LodePNGState* source);
#endif 
#ifdef LODEPNG_COMPILE_DECODER
unsigned LP_lodepng_decode(unsigned char** out, unsigned* w, unsigned* h,
                        LP_LodePNGState* state,
                        const unsigned char* in, size_t insize);
unsigned LP_lodepng_inspect(unsigned* w, unsigned* h,
                         LP_LodePNGState* state,
                         const unsigned char* in, size_t insize);
#endif 
unsigned LP_lodepng_inspect_chunk(LP_LodePNGState* state, size_t pos,
                               const unsigned char* in, size_t insize);
#ifdef LODEPNG_COMPILE_ENCODER
unsigned LP_lodepng_encode(unsigned char** out, size_t* outsize,
                        const unsigned char* image, unsigned w, unsigned h,
                        LP_LodePNGState* state);
#endif 
unsigned LP_lodepng_chunk_length(const unsigned char* chunk);
void LP_lodepng_chunk_type(char type[5], const unsigned char* chunk);
unsigned char LP_lodepng_chunk_type_equals(const unsigned char* chunk, const char* type);
unsigned char LP_lodepng_chunk_ancillary(const unsigned char* chunk);
unsigned char LP_lodepng_chunk_private(const unsigned char* chunk);
unsigned char LP_lodepng_chunk_safetocopy(const unsigned char* chunk);
unsigned char* LP_lodepng_chunk_data(unsigned char* chunk);
const unsigned char* LP_lodepng_chunk_data_const(const unsigned char* chunk);
unsigned LP_lodepng_chunk_check_crc(const unsigned char* chunk);
void LP_lodepng_chunk_generate_crc(unsigned char* chunk);
unsigned char* LP_lodepng_chunk_next(unsigned char* chunk, unsigned char* end);
const unsigned char* LP_lodepng_chunk_next_const(const unsigned char* chunk, const unsigned char* end);
unsigned char* LP_lodepng_chunk_find(unsigned char* chunk, unsigned char* end, const char type[5]);
const unsigned char* LP_lodepng_chunk_find_const(const unsigned char* chunk, const unsigned char* end, const char type[5]);
unsigned LP_lodepng_chunk_append(unsigned char** out, size_t* outsize, const unsigned char* chunk);
unsigned LP_lodepng_chunk_create(unsigned char** out, size_t* outsize, size_t length,
                              const char* type, const unsigned char* data);
unsigned LP_lodepng_crc32(const unsigned char* buf, size_t len);
#endif 
#ifdef LODEPNG_COMPILE_ZLIB
#ifdef LODEPNG_COMPILE_DECODER
unsigned LP_lodepng_inflate(unsigned char** out, size_t* outsize,
                         const unsigned char* in, size_t insize,
                         const LP_LodePNGDecompressSettings* settings);
unsigned LP_lodepng_zlib_decompress(unsigned char** out, size_t* outsize,
                                 const unsigned char* in, size_t insize,
                                 const LP_LodePNGDecompressSettings* settings);
#endif 
#ifdef LODEPNG_COMPILE_ENCODER
unsigned LP_lodepng_zlib_compress(unsigned char** out, size_t* outsize,
                               const unsigned char* in, size_t insize,
                               const LP_LodePNGCompressSettings* settings);
unsigned LP_lodepng_huffman_code_lengths(unsigned* lengths, const unsigned* frequencies,
                                      size_t numcodes, unsigned maxbitlen);
unsigned LP_lodepng_deflate(unsigned char** out, size_t* outsize,
                         const unsigned char* in, size_t insize,
                         const LP_LodePNGCompressSettings* settings);
#endif 
#endif 
#ifdef LODEPNG_COMPILE_DISK
unsigned LP_lodepng_load_file(unsigned char** out, size_t* outsize, const char* filename);
unsigned LP_lodepng_save_file(const unsigned char* buffer, size_t buffersize, const char* filename);
#endif 
#ifdef LODEPNG_COMPILE_CPP
namespace lodepng {
#ifdef LODEPNG_COMPILE_PNG
class State : public LP_LodePNGState {
  public:
    State();
    State(const State& other);
    ~State();
    State& operator=(const State& other);
};
#ifdef LODEPNG_COMPILE_DECODER
unsigned decode(std::vector<unsigned char>& out, unsigned& w, unsigned& h,
                State& state,
                const unsigned char* in, size_t insize);
unsigned decode(std::vector<unsigned char>& out, unsigned& w, unsigned& h,
                State& state,
                const std::vector<unsigned char>& in);
#endif 
#ifdef LODEPNG_COMPILE_ENCODER
unsigned encode(std::vector<unsigned char>& out,
                const unsigned char* in, unsigned w, unsigned h,
                State& state);
unsigned encode(std::vector<unsigned char>& out,
                const std::vector<unsigned char>& in, unsigned w, unsigned h,
                State& state);
#endif 
#ifdef LODEPNG_COMPILE_DISK
unsigned load_file(std::vector<unsigned char>& buffer, const std::string& filename);
unsigned save_file(const std::vector<unsigned char>& buffer, const std::string& filename);
#endif 
#endif 
#ifdef LODEPNG_COMPILE_ZLIB
#ifdef LODEPNG_COMPILE_DECODER
unsigned decompress(std::vector<unsigned char>& out, const unsigned char* in, size_t insize,
                    const LP_LodePNGDecompressSettings& settings = LP_lodepng_default_decompress_settings);
unsigned decompress(std::vector<unsigned char>& out, const std::vector<unsigned char>& in,
                    const LP_LodePNGDecompressSettings& settings = LP_lodepng_default_decompress_settings);
#endif 
#ifdef LODEPNG_COMPILE_ENCODER
unsigned compress(std::vector<unsigned char>& out, const unsigned char* in, size_t insize,
                  const LP_LodePNGCompressSettings& settings = LP_lodepng_default_compress_settings);
unsigned compress(std::vector<unsigned char>& out, const std::vector<unsigned char>& in,
                  const LP_LodePNGCompressSettings& settings = LP_lodepng_default_compress_settings);
#endif 
#endif 
} 
#endif 
#endif 
