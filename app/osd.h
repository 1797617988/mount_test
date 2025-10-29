#ifndef OSD_H
#define OSD_H

#include <SDL2/SDL.h>        // SDL 图形库
#include <SDL2/SDL_ttf.h>    // SDL 字体渲染库
#include <sys/types.h>  // 系统类型定义
#include <stdlib.h>     // 标准库函数
#include <string.h>     // 字符串操作函数

// 定义像素格式为 ARGB1555（16位色深，1位透明度，5位红、绿、蓝）
#define OT_PIXEL_FORMAT_ARGB_1555 SDL_PIXELFORMAT_ARGB1555
// 定义像素格式为 ARGB8888（32位色深，8位透明度，红、绿、蓝）
#define OT_PIXEL_FORMAT_ARGB_8888 SDL_PIXELFORMAT_ARGB8888 

// 默认字体路径配置
#define DEFAULT_FONT_PATH "/root/app/SimplifiedChinese/SourceHanSansSC-Regular.otf"
#define FALLBACK_FONT_PATH "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
#define FONT_CACHE_TIMEOUT 300 // 5分钟超时

#ifndef TD_FAILURE
#define TD_FAILURE -1
#endif

#ifndef TD_SUCCESS
#define TD_SUCCESS 0
#endif

// 文本对齐方式
typedef enum {
    ALIGN_LEFT = 0,    // 居左对齐
    ALIGN_CENTER,      // 居中对齐  
    ALIGN_RIGHT        // 居右对齐
} TextAlignment;

// ot_bmpp结构体定义
typedef struct {
    void *data;            // 指向位图像素数据的指针
    int width;             // 位图的宽度（像素）
    int height;            // 位图的高度（像素）
    int pitch;             // 保存表面的步长信息
    int pixel_format;      // 位图的像素格式（如 OT_PIXEL_FORMAT_ARGB_1555）
    SDL_Color *palette;    // 颜色表指针（仅用于带调色板的BMP）
    int palette_size;      // 颜色表大小
    uint8_t *stroke_mask;  // 描边掩码（标记描边像素）
    
    // 渲染参数存储（用于保存文本渲染时的颜色和描边信息）
    SDL_Color text_color;     // 文本颜色
    SDL_Color stroke_color;   // 描边颜色
    int stroke_width;         // 描边宽度
    int font_size;            // 字体大小
    TextAlignment alignment;  // 对齐方式
    int line_spacing;         // 行间距
    int has_stroke;           // 是否有描边（0=无，1=有）
} ot_bmpp;

/**
 * @brief BMP文件头结构（Windows BITMAPFILEHEADER）
 */
#pragma pack(push, 1) // 确保结构体紧凑对齐
typedef struct {
    uint16_t bfType;      // 文件类型，必须是"BM"（0x4D42）
    uint32_t bfSize;      // 文件大小（字节）
    uint16_t bfReserved1; // 保留，必须为0
    uint16_t bfReserved2; // 保留，必须为0
    uint32_t bfOffBits;   // 图像数据偏移量（字节）
} BMPFileHeader;

/**
 * @brief BMP信息头结构（Windows BITMAPINFOHEADER）
 */
typedef struct {
    uint32_t biSize;          // 本结构大小（40字节）
    int32_t  biWidth;         // 图像宽度（像素）
    int32_t  biHeight;        // 图像高度（像素）
    uint16_t biPlanes;        // 颜色平面数，必须为1
    uint16_t biBitCount;      // 每像素位数（1,4,8,16,24,32）
    uint32_t biCompression;   // 压缩类型（0=不压缩）
    uint32_t biSizeImage;     // 图像数据大小（字节）
    int32_t  biXPelsPerMeter; // 水平分辨率（像素/米）
    int32_t  biYPelsPerMeter; // 垂直分辨率（像素/米）
    uint32_t biClrUsed;       // 使用的颜色索引数
    uint32_t biClrImportant;  // 重要颜色索引数
} BMPInfoHeader;
#pragma pack(pop)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 从BMP文件加载图像数据到ot_bmpp结构
 * @param filename BMP文件路径
 * @param osd_bmp 存储图像数据的结构体指针
 * @return 成功返回TD_SUCCESS，失败返回TD_FAILURE
 */
int bmp_load_file(const char *filename, ot_bmpp *osd_bmp);

/**
 * @brief 释放ot_bmpp结构体内存
 * @param osd_bmp 要释放的结构体指针
 */

// 释放ot_bmpp结构体内存
void bmp_free(ot_bmpp *osd_bmp);

// 获取渲染参数
int bmp_get_render_params(ot_bmpp *osd_bmp, SDL_Color *text_color, SDL_Color *stroke_color, 
                         int *stroke_width, int *font_size, TextAlignment *alignment, int *line_spacing);


// 多行文本渲染函数（支持对齐）
int ttf_get_multiline_bmp(ot_bmpp *osd_bmp,const char *osd_buf, SDL_Color color, 
                         int32_t fontSize, SDL_Color *stroke_color, int stroke_width,
                         TextAlignment alignment, int line_spacing);

// 单行文本渲染函数（保持兼容性）
int ttf_get_bmp(ot_bmpp *osd_bmp,  char *osd_buf, SDL_Color color, 
                   int32_t fontSize, SDL_Color *stroke_color, int stroke_width);

// 基础文本渲染函数
int32_t ttf_get_text(TTF_Font *font, SDL_Color fg, const char *data, ot_bmpp *osd_bmp);

// 保存BMP文件函数
int save_bmp_file(ot_bmpp *osd_bmp, const char *filename);

//描边函数
int32_t ttf_get_text_with_stroke(TTF_Font *font, SDL_Color fg_color,                                
                                const char *data, ot_bmpp *osd_bmp,
                                SDL_Color stroke_color, int32_t stroke_width);
//格式转换函数
int32_t convert_32bit_to_16bit(SDL_Surface *src_surface, SDL_Surface *dest_surface);

// 垂直拼接多个BMP文件（纯拼接，不修改内容）
int bmp_concatenate_vertically(const char **filenames, int count, TextAlignment alignment, const char *output_filename);

// 垂直拼接多个BMP文件并重新渲染文本内容
int bmp_concatenate_with_rendering(const char **filenames, int count, TextAlignment alignment, const char *output_filename, ot_bmpp **out_merged_bmp);


// 字体缓存管理
void cleanup_font_cache(void);


// 文本处理函数
// static int split_text_lines(const char *text, char ***lines, int *line_count);
// static int calculate_multiline_size(TTF_Font *font, char **lines, int line_count, 
//                                    int *max_width, int *total_height, int line_spacing);
// static int render_multiline_text(TTF_Font *font, SDL_Color fg_color, char **lines, 
//                                 int line_count, SDL_Surface *target_surface,
//                                 TextAlignment alignment, int line_spacing);
// static int render_multiline_text_with_stroke(TTF_Font *font, SDL_Color fg_color, 
//                                            char **lines, int line_count, 
//                                            SDL_Surface *target_surface,
//                                            SDL_Color stroke_color, int stroke_width,
//                                            TextAlignment alignment, int line_spacing);

// // 字体管理函数
// static int ensure_font_style_normal(TTF_Font *font);
// static int verify_font_state(TTF_Font *font);
// static TTF_Font *get_font_with_cache(int fontSize);
// static void release_font_from_cache(int fontSize);

// 测试函数
void test_font_style_consistency(void);
void test_concatenate_font_style_bmps(void);
void test_minimal_case(void);
void test_single_case(const char *text, int size, TextAlignment align, int spacing, const char *filename);



// 像素格式转换
int32_t convert_32bit_to_16bit(SDL_Surface *src_surface, SDL_Surface *dest_surface);

// 文本渲染函数
int32_t ttf_get_text(TTF_Font *font, SDL_Color fg, const char *data, ot_bmpp *osd_bmp);
int32_t ttf_get_text_with_stroke(TTF_Font *font, SDL_Color fg_color, const char *data, ot_bmpp *osd_bmp, SDL_Color stroke_color, int stroke_width);

// BMP文件操作
int bmp_load_file(const char *filename, ot_bmpp *osd_bmp);
void bmp_free(ot_bmpp *osd_bmp);
int bmp_get_render_params(ot_bmpp *osd_bmp, SDL_Color *text_color, SDL_Color *stroke_color, int *stroke_width, int *font_size, TextAlignment *alignment, int *line_spacing);
int save_bmp_file(ot_bmpp *osd_bmp, const char *filename);

// BMP拼接函数
int bmp_concatenate_vertically(const char **filenames, int count, TextAlignment alignment, const char *output_filename);
int bmp_concatenate_with_rendering(const char **filenames, int count, TextAlignment alignment, const char *output_filename, ot_bmpp **out_merged_bmp);

// 全局资源清理
void osd_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif // OSD_H