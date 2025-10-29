#include "osd.h"
#include <pthread.h>

#ifndef PTHREAD_MUTEX_RECURSIVE
#define PTHREAD_MUTEX_RECURSIVE PTHREAD_MUTEX_RECURSIVE_NP
#endif


#include <pthread.h> // for pthread_mutex_t
#include <unistd.h>  // for usleep
#include <time.h>    // for time()
#include <errno.h>   // for errno
#include <sys/stat.h> // for file access checking
#include "ss_log.h" // for ss_log_e



// 1. 全局互斥锁 g_mux
static pthread_mutex_t g_mux;
static int g_mux_initialized = 0;

// 递归锁初始化函数
// 递归锁销毁函数声明
static void cleanup_recursive_mutex(void);

static void init_recursive_mutex(void) {
    if (!g_mux_initialized) {
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&g_mux, &attr);
        pthread_mutexattr_destroy(&attr);
        g_mux_initialized = 1;
        atexit(cleanup_recursive_mutex); // 注册退出时销毁锁
    }
}

// 递归锁销毁函数定义
static void cleanup_recursive_mutex(void) {
    if (g_mux_initialized) {
        pthread_mutex_destroy(&g_mux);
        g_mux_initialized = 0;
    }
}



// 函数声明
void test_single_case(const char *text, int size, TextAlignment align, int spacing, const char *filename);
static int split_text_lines(const char *text, char ***lines, int *line_count);
static int calculate_multiline_size(TTF_Font *font, char **lines, int line_count, 
                                   int *max_width, int *total_height, int line_spacing);
static int render_multiline_text(TTF_Font *font, SDL_Color fg_color, char **lines, 
                                int line_count, SDL_Surface *target_surface,
                                TextAlignment alignment, int line_spacing);
static int render_multiline_text_with_stroke(TTF_Font *font, SDL_Color fg_color, 
                                           char **lines, int line_count, 
                                           SDL_Surface *target_surface,
                                           SDL_Color stroke_color, int stroke_width,
                                           TextAlignment alignment, int line_spacing);

static int ensure_font_style_normal(TTF_Font *font);
static int split_text_lines(const char *text, char ***lines, int *line_count);
static int verify_font_state(TTF_Font *font);




// 像素格式转换
int32_t convert_32bit_to_16bit(SDL_Surface *src_surface, SDL_Surface *dest_surface);

// 文本渲染函数
int32_t ttf_get_text(TTF_Font *font, SDL_Color fg, const char *data, ot_bmpp *osd_bmp);
int32_t ttf_get_text_with_stroke(TTF_Font *font, SDL_Color fg_color, 
                                const char *data, ot_bmpp *osd_bmp,
                                SDL_Color stroke_color, int stroke_width);

// BMP文件操作
int bmp_load_file(const char *filename, ot_bmpp *osd_bmp);
void bmp_free(ot_bmpp *osd_bmp);
int bmp_get_render_params(ot_bmpp *osd_bmp, SDL_Color *text_color, SDL_Color *stroke_color, 
                         int *stroke_width, int *font_size, TextAlignment *alignment, int *line_spacing);
int save_bmp_file(ot_bmpp *osd_bmp, const char *filename);

// BMP拼接函数
int bmp_concatenate_vertically(const char **filenames, int count, TextAlignment alignment, const char *output_filename);
int bmp_concatenate_with_rendering(const char **filenames, int count, TextAlignment alignment, 
                                  const char *output_filename, ot_bmpp **out_merged_bmp);

// 全局资源清理
void osd_cleanup(void);




/**
 * @brief 将32位ARGB8888表面转换为16位ARGB1555格式
 * @param src_surface 32位源表面
 * @param dest_surface 16位目标表面
 * @return 成功返回TD_SUCCESS，失败返回TD_FAILURE
 */
int32_t convert_32bit_to_16bit(SDL_Surface *src_surface, SDL_Surface *dest_surface) {
    if (!src_surface || !dest_surface) {
        return TD_FAILURE;
    }
    
    // 锁定表面（如果需要）
    if (SDL_MUSTLOCK(src_surface)) SDL_LockSurface(src_surface);
    if (SDL_MUSTLOCK(dest_surface)) SDL_LockSurface(dest_surface);
    
    Uint32 *src_pixels = (Uint32 *)src_surface->pixels;
    Uint16 *dest_pixels = (Uint16 *)dest_surface->pixels;
    
    int width = src_surface->w;
    int height = src_surface->h;
    
    // 转换每个像素：ARGB8888 -> ARGB1555
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            Uint32 pixel32 = src_pixels[y * width + x];
            
            // 提取32位像素的各个分量
            Uint8 a = (pixel32 >> 24) & 0xFF;  // Alpha (8位)
            Uint8 r = (pixel32 >> 16) & 0xFF;  // Red (8位)
            Uint8 g = (pixel32 >> 8) & 0xFF;   // Green (8位)
            Uint8 b = pixel32 & 0xFF;          // Blue (8位)
            
            // 转换为16位ARGB1555格式
            Uint16 pixel16 = 0;
            
            // Alpha: 8位 -> 1位 (阈值128)
            if (a > 128) pixel16 |= 0x8000;
            
            // Red: 8位 -> 5位
            pixel16 |= ((r >> 3) << 10) & 0x7C00;
            
            // Green: 8位 -> 5位
            pixel16 |= ((g >> 3) << 5) & 0x03E0;
            
            // Blue: 8位 -> 5位
            pixel16 |= (b >> 3) & 0x001F;
            
            dest_pixels[y * width + x] = pixel16;
        }
    }
    
    // 解锁表面
    if (SDL_MUSTLOCK(src_surface)) SDL_UnlockSurface(src_surface);
    if (SDL_MUSTLOCK(dest_surface)) SDL_UnlockSurface(dest_surface);
    
    return TD_SUCCESS;
}


/**
 * @brief 将 UTF-8 文本渲染为位图并存储到 ot_bmpp 结构体中（带抗锯齿）
 * @param font     字体对象指针
 * @param fg       文本颜色（SDL_Color 结构体）
 * @param data     要渲染的文本字符串
 * @param osd_bmp  存储位图数据的结构体指针
 * @return 成功返回 TD_SUCCESS，失败返回 TD_FAILURE
 */
int32_t ttf_get_text(TTF_Font *font, SDL_Color fg,const char *data, ot_bmpp *osd_bmp) {
    if (TTF_Init() == -1) {
        ss_log_e("TTF_Init failed: %s", TTF_GetError());
        return TD_FAILURE;
    }

    
    // //使用 TTF_RenderUTF8_Solid将文本渲染成 SDL_Surface
    // SDL_Surface *text = TTF_RenderUTF8_Solid(font, data, fg);


    //  使用样式管理函数
    if (ensure_font_style_normal(font) != TD_SUCCESS) {
        ss_log_e("无法设置字体样式为正常");
        return TD_FAILURE;
    }

    // 额外验证
    verify_font_state(font);

    
    // 使用抗锯齿渲染函数 TTF_RenderUTF8_Blended（生成32位表面）
    SDL_Surface *text = TTF_RenderUTF8_Blended(font, data, fg);
    if (!text) {
        ss_log_e("TTF_RenderUTF8_Solid failed: %s", TTF_GetError());
        SDL_FreeSurface(text);
        return TD_FAILURE;
    }

    // 定义 ARGB8888 格式的掩码
    Uint32 rmask32, gmask32, bmask32, amask32;

    rmask32 = 0x00FF0000;  // 红色掩码（8位）
    gmask32 = 0x0000FF00;  // 绿色掩码（8位）
    bmask32 = 0x000000FF;  // 蓝色掩码（8位）
    amask32 = 0xFF000000;  // 透明度掩码（8位）

    // 定义 ARGB1555 格式的掩码
    Uint32 rmask16, gmask16, bmask16, amask16;
    rmask16 = 0x7C00;
    gmask16 = 0x03E0;
    bmask16 = 0x001F;
    amask16 = 0x8000;

    //创建目标格式的 SDL_Surface (32位色深，ARGB8888格式)
    SDL_Surface *temp_32bit_surface = SDL_CreateRGBSurface(0, text->w, text->h, 32, 
                                                          rmask32, gmask32, bmask32, amask32);
    if (!temp_32bit_surface) {
        SDL_FreeSurface(text);
        return TD_FAILURE;
    }

    //使用 SDL_BlitSurface将文本表面复制到目标表面
    if (SDL_BlitSurface(text, NULL, temp_32bit_surface, NULL) < 0) {
        ss_log_e("BlitSurface failed: %s", SDL_GetError());
        SDL_FreeSurface(text);
        SDL_FreeSurface(temp_32bit_surface);
        return TD_FAILURE;
    }

    // 创建16位目标表面（最终输出）
    SDL_Surface *target_16bit_surface = SDL_CreateRGBSurface(0, text->w, text->h, 16, 
                                                           rmask16, gmask16, bmask16, amask16);
    if (!target_16bit_surface) {
        SDL_FreeSurface(text);
        SDL_FreeSurface(temp_32bit_surface);
        return TD_FAILURE;
    }

    // 将32位转换为16位
    if (convert_32bit_to_16bit(temp_32bit_surface, target_16bit_surface) != TD_SUCCESS) {
        ss_log_e("32位到16位转换失败");
        SDL_FreeSurface(text);
        SDL_FreeSurface(temp_32bit_surface);
        SDL_FreeSurface(target_16bit_surface);
        return TD_FAILURE;
    }

    //分配内存
    //osd_bmp->data = malloc(2 * target_format_surface->w * target_format_surface->h);
    size_t buffer_size = target_16bit_surface->pitch * target_16bit_surface->h;
    osd_bmp->data = malloc(buffer_size);
    if (!osd_bmp->data) {
        SDL_FreeSurface(text);
        SDL_FreeSurface(temp_32bit_surface);
        SDL_FreeSurface(target_16bit_surface);           
        return TD_FAILURE;
    }
    // 复制16位像素数据到 osd_bmp->data
    memcpy(osd_bmp->data, target_16bit_surface->pixels, buffer_size);
    //memcpy(osd_bmp->data, target_format_surface->pixels, 2 * target_format_surface->w * target_format_surface->h);

    // 设置16位位图属性
    osd_bmp->pixel_format = OT_PIXEL_FORMAT_ARGB_1555;
    osd_bmp->width = target_16bit_surface->w;
    osd_bmp->height = target_16bit_surface->h;
    osd_bmp->pitch = target_16bit_surface->pitch;
    
    // 🔧 优化：存储渲染参数（单行文本，无描边）
    osd_bmp->text_color = fg;
    osd_bmp->font_size = TTF_FontHeight(font); // 获取字体高度作为字号
    osd_bmp->alignment = ALIGN_LEFT; // 单行文本默认左对齐
    osd_bmp->line_spacing = 0; // 单行文本无行距
    osd_bmp->stroke_width = 0;
    osd_bmp->has_stroke = 0;
    osd_bmp->stroke_color = (SDL_Color){0, 0, 0, 255}; // 默认黑色描边
    
    // 初始化调色板和描边掩码
    osd_bmp->palette = NULL;
    osd_bmp->palette_size = 0;
    osd_bmp->stroke_mask = NULL;
    
    ss_log_d("单行文本渲染参数已存储: 颜色(%d,%d,%d,%d), 字号=%d", 
            osd_bmp->text_color.r, osd_bmp->text_color.g, osd_bmp->text_color.b, osd_bmp->text_color.a,
            osd_bmp->font_size);
    
    //释放临时表面资源
    SDL_FreeSurface(text);
    SDL_FreeSurface(temp_32bit_surface);
    SDL_FreeSurface(target_16bit_surface);
    
    TTF_Quit();
    return TD_SUCCESS;
}

/**
 * @brief 保持兼容性的单行文本渲染函数
 */
int ttf_get_bmp(ot_bmpp *osd_bmp, char *osd_buf, SDL_Color color, 
               int32_t fontSize, SDL_Color *stroke_color, int stroke_width) {
    // 默认使用居左对齐，行距为0
    return ttf_get_multiline_bmp(osd_bmp, osd_buf, color, fontSize, 
                               stroke_color, stroke_width, ALIGN_LEFT, 0);
}

/**
 * @brief 生成带描边效果的16位位图
 * @param font 字体对象指针
 * @param fg_color 文字颜色
 * @param data 要渲染的文本字符串
 * @param osd_bmp 存储位图数据的结构体指针
 * @param stroke_color 描边颜色
 * @param stroke_width 描边宽度（像素）
 * @return 成功返回TD_SUCCESS，失败返回TD_FAILURE
 */
int32_t ttf_get_text_with_stroke(TTF_Font *font, SDL_Color fg_color, 
                                const char *data, ot_bmpp *osd_bmp,
                                SDL_Color stroke_color, int stroke_width) {
    if (TTF_Init() == -1) {
        ss_log_e("TTF_Init failed: %s", TTF_GetError());
        return TD_FAILURE;
    }

    pthread_mutex_lock(&g_mux);



    // 重置字体样式
    TTF_SetFontStyle(font, TTF_STYLE_NORMAL);
    TTF_SetFontOutline(font, 0);
    TTF_SetFontKerning(font, 1);

    // 修复：使用样式管理函数
    if (ensure_font_style_normal(font) != TD_SUCCESS) {
        ss_log_e("无法设置字体样式为正常");
        TTF_CloseFont(font);
        pthread_mutex_unlock(&g_mux);
        return TD_FAILURE;
    }

    // 额外验证
    verify_font_state(font);
    
    // 先渲染描边（在多个方向偏移渲染）
    SDL_Surface *stroke_surface = NULL;
    SDL_Surface *final_surface = NULL;
    
    // 计算临时表面大小（考虑描边宽度）
    //SDL_Surface *sample = TTF_RenderUTF8_Solid(font, data, fg_color);//不抗锯齿，简化版
    SDL_Surface *sample = TTF_RenderUTF8_Blended(font, data, fg_color);
    if (!sample) {
        ss_log_e("TTF_RenderUTF8_Blended failed: %s", TTF_GetError());
        return TD_FAILURE;
    }

    int actual_width = sample->w + stroke_width * 2;
    int actual_height = sample->h + stroke_width * 2;
    SDL_FreeSurface(sample);
    
    // 定义32位格式掩码（用于中间合成）
    Uint32 rmask32 = 0x00FF0000, gmask32 = 0x0000FF00, bmask32 = 0x000000FF, amask32 = 0xFF000000;
    
    // 创建32位临时表面用于合成
    final_surface = SDL_CreateRGBSurface(0, actual_width, actual_height, 32,
                                       rmask32, gmask32, bmask32, amask32);
    

    if (!final_surface) {
        ss_log_e("创建临时表面失败: %s", SDL_GetError());
        return TD_FAILURE;
    }
    
    // 设置表面混合模式为透明
    SDL_SetSurfaceBlendMode(final_surface, SDL_BLENDMODE_BLEND);
    
    // 在8个方向渲染描边
    for (int x = -stroke_width; x <= stroke_width; x++) {
        for (int y = -stroke_width; y <= stroke_width; y++) {
            if (x == 0 && y == 0) continue;
            
            stroke_surface = TTF_RenderUTF8_Solid(font, data, stroke_color);
            if (stroke_surface) {
                SDL_Rect dst_rect = {stroke_width + x, stroke_width + y, 
                                   stroke_surface->w, stroke_surface->h};
                SDL_BlitSurface(stroke_surface, NULL, final_surface, &dst_rect);
                SDL_FreeSurface(stroke_surface);
            }
        }
    }
    
    // 渲染主体文字（覆盖在描边之上）
    //SDL_Surface *text_surface = TTF_RenderUTF8_Solid(font, data, fg_color);
    SDL_Surface *text_surface = TTF_RenderUTF8_Blended(font, data, fg_color);
    if (text_surface) {
        SDL_Rect text_rect = {stroke_width, stroke_width, 
                             text_surface->w, text_surface->h};
        SDL_BlitSurface(text_surface, NULL, final_surface, &text_rect);
        SDL_FreeSurface(text_surface);
    }
    
    // 定义16位格式掩码（最终输出）
    Uint32 rmask16 = 0x7C00, gmask16 = 0x03E0, bmask16 = 0x001F, amask16 = 0x8000;
    
    // 创建16位目标表面
    SDL_Surface *target_16bit_surface = SDL_CreateRGBSurface(0, final_surface->w, 
                                                             final_surface->h, 16,
                                                             rmask16, gmask16, bmask16, amask16);
    if (!target_16bit_surface) {
        SDL_FreeSurface(final_surface);
        return TD_FAILURE;
    }
    
    // 将32位转换为16位
    if (convert_32bit_to_16bit(final_surface, target_16bit_surface) != TD_SUCCESS) {
        ss_log_e("32位到16位转换失败");
        SDL_FreeSurface(final_surface);
        SDL_FreeSurface(target_16bit_surface);
        return TD_FAILURE;
    }
    
    // 复制数据到osd_bmp
    size_t buffer_size = target_16bit_surface->pitch * target_16bit_surface->h;
    osd_bmp->data = malloc(buffer_size);
    if (!osd_bmp->data) {
        SDL_FreeSurface(final_surface);
        SDL_FreeSurface(target_16bit_surface);
        return TD_FAILURE;
    }
    
    memcpy(osd_bmp->data, target_16bit_surface->pixels, buffer_size);
    osd_bmp->pixel_format = OT_PIXEL_FORMAT_ARGB_1555;
    osd_bmp->width = target_16bit_surface->w;
    osd_bmp->height = target_16bit_surface->h;
    osd_bmp->pitch = target_16bit_surface->pitch;
    
    // 🔧 优化：存储渲染参数（带描边的单行文本）
    osd_bmp->text_color = fg_color;
    osd_bmp->stroke_color = stroke_color;
    osd_bmp->stroke_width = stroke_width;
    osd_bmp->font_size = TTF_FontHeight(font); // 获取字体高度作为字号
    osd_bmp->alignment = ALIGN_LEFT; // 单行文本默认左对齐
    osd_bmp->line_spacing = 0; // 单行文本无行距
    osd_bmp->has_stroke = 1;
    
    // 初始化调色板和描边掩码
    osd_bmp->palette = NULL;
    osd_bmp->palette_size = 0;
    osd_bmp->stroke_mask = NULL;
    
    ss_log_d("描边文本渲染参数已存储: 文字颜色(%d,%d,%d,%d), 描边颜色(%d,%d,%d,%d), 描边宽度=%d, 字号=%d", 
            osd_bmp->text_color.r, osd_bmp->text_color.g, osd_bmp->text_color.b, osd_bmp->text_color.a,
            osd_bmp->stroke_color.r, osd_bmp->stroke_color.g, osd_bmp->stroke_color.b, osd_bmp->stroke_color.a,
            osd_bmp->stroke_width, osd_bmp->font_size);

    
    // 释放资源
    SDL_FreeSurface(final_surface);
    SDL_FreeSurface(target_16bit_surface);

    pthread_mutex_unlock(&g_mux);
    
    TTF_Quit();
    return TD_SUCCESS;
}


/**
 * @brief 分割文本为多行
 */
static int split_text_lines(const char *text, char ***lines, int *line_count) {
    if (!text || !lines || !line_count) return TD_FAILURE;
    
    // 计算行数
    *line_count = 1;
    for (const char *p = text; *p; p++) {
        if (*p == '\n') (*line_count)++;
    }
    

    // 添加日志：记录行数
    ss_log_d("分割文本行数: %d", *line_count);
    
    // 添加内存分配检查
    *lines = malloc(sizeof(char*) * (*line_count));
    if (!*lines) {
        ss_log_e("分配行指针数组失败");
        return TD_FAILURE;
    }

    // 复制文本并分割
    char *text_copy = strdup(text);
    if (!text_copy) {
        ss_log_e("复制文本失败");
        free(*lines);
        *lines = NULL;
        return TD_FAILURE;
    }

    int i = 0;
    char *token = strtok(text_copy, "\n");
    while (token != NULL && i < *line_count) {
        // 添加日志：记录每行内容
        ss_log_d("分割行 %d: %s", i, token);
        (*lines)[i] = strdup(token);
        if (!(*lines)[i]) {
            ss_log_e("分配行 %d 内存失败", i);
            // 清理已分配的内存
            for (int j = 0; j < i; j++) free((*lines)[j]);
            free(*lines);
            free(text_copy);
            return TD_FAILURE;
        }
        i++;
        token = strtok(NULL, "\n");
    }
    
    free(text_copy);
    return TD_SUCCESS;
}

/**
 * @brief 计算多行文本的尺寸
 */
static int calculate_multiline_size(TTF_Font *font, char **lines, int line_count, 
                                   int *max_width, int *total_height, int line_spacing) {
    *max_width = 0;
    *total_height = 0;

    // 🔧 修复：在循环前确保字体样式正常
    if (ensure_font_style_normal(font) != TD_SUCCESS) {
        ss_log_e("计算尺寸时无法设置字体样式正常");
        return TD_FAILURE;
    }
    
    for (int i = 0; i < line_count; i++) {
        TTF_SetFontStyle(font, TTF_STYLE_NORMAL);

        // 🔧 添加长度限制
        if (strlen(lines[i]) > 256) {
            ss_log_e("行 %d 过长: %zu 字符", i, strlen(lines[i]));
            return TD_FAILURE;
        }

        if (!lines[i]) {
            ss_log_e("第 %d 行为空", i);
            return TD_FAILURE;
        }

        // 🔧 修复：每次渲染前确保字体样式正常
        if (ensure_font_style_normal(font) != TD_SUCCESS) {
            ss_log_e("渲染行 %d 前无法设置字体样式正常", i);
            return TD_FAILURE;
        }
        // 额外验证
        verify_font_state(font);

        SDL_Color sample_color = {255, 255, 255, 255};
        SDL_Surface *sample = TTF_RenderUTF8_Blended(font, lines[i], sample_color);
        if (sample) {

            // 🔧 添加最大尺寸限制
            if (sample->w > 4096 || sample->h > 4096) {
                ss_log_e("行 %d 尺寸过大: %dx%d", i, sample->w, sample->h);
                SDL_FreeSurface(sample);
                return TD_FAILURE;
            }

            ss_log_d("行 %d 尺寸: %dx%d", i, sample->w, sample->h);
            if (sample->w > *max_width) *max_width = sample->w;
            *total_height += sample->h;
            SDL_FreeSurface(sample);
        }else {
            ss_log_e("渲染行 %d 失败: %s", i, TTF_GetError());
            return TD_FAILURE;
        }
    }
    
    // 添加行间距
    if (line_count > 1) {
        *total_height += line_spacing * (line_count - 1);
    }
    
    // 🔧 修复：添加安全边距（防止裁剪）
    *total_height += 2; // 上下各加1像素安全边距
    
    ss_log_d("计算尺寸: %dx%d, 行数: %d, 行距: %d", *max_width, *total_height, line_count, line_spacing);
    return TD_SUCCESS;
}

/**
 * @brief 渲染多行文本（无描边）
 */
static int render_multiline_text(TTF_Font *font, SDL_Color fg_color, char **lines, 
                                int line_count, SDL_Surface *target_surface,
                                TextAlignment alignment, int line_spacing) {

    // 🔧 确保字体样式正常
    if (ensure_font_style_normal(font) != TD_SUCCESS) {
        return TD_FAILURE;
    }

    // 🔧 修复：考虑安全边距
    int current_y = 1; // 顶部安全边距
    
    for (int i = 0; i < line_count; i++) {
        // 修复：每次渲染前强制设置字体样式
        TTF_SetFontStyle(font, TTF_STYLE_NORMAL);
        if (strlen(lines[i]) > 256) {
            ss_log_e("行 %d 过长: %zu 字符", i, strlen(lines[i]));
            return TD_FAILURE;
        }
        SDL_Surface *line_surface = TTF_RenderUTF8_Blended(font, lines[i], fg_color);
        if (line_surface) {
            int x = 0;
            switch (alignment) {
                case ALIGN_CENTER:
                    x = (target_surface->w - line_surface->w) / 2;
                    break;
                case ALIGN_RIGHT:
                    x = target_surface->w - line_surface->w;
                    break;
                case ALIGN_LEFT:
                default:
                    x = 0;
                    break;
            }

            // 🔧 修复：检查Y坐标是否超出边界
            if (current_y + line_surface->h > target_surface->h) {
                ss_log_e("行 %d 超出边界: Y=%d, 高度=%d, 表面高度=%d", 
                        i, current_y, line_surface->h, target_surface->h);
                SDL_FreeSurface(line_surface);
                return TD_FAILURE;
            }
            
            SDL_Rect dst_rect = {x, current_y, line_surface->w, line_surface->h};
            SDL_BlitSurface(line_surface, NULL, target_surface, &dst_rect);
            SDL_FreeSurface(line_surface);
            
            current_y += line_surface->h;
            if (i < line_count - 1) { // 不是最后一行才加行间距
                current_y += line_spacing;
            }
        }
    }
    
    return TD_SUCCESS;
}

/**
 * @brief 渲染带描边的多行文本
 */
static int render_multiline_text_with_stroke(TTF_Font *font, SDL_Color fg_color, 
                                           char **lines, int line_count, 
                                           SDL_Surface *target_surface,
                                           SDL_Color stroke_color, int stroke_width,
                                           TextAlignment alignment, int line_spacing) {
    // 🔧 确保字体样式正常
    if (ensure_font_style_normal(font) != TD_SUCCESS) {
        return TD_FAILURE;
    }
    int current_y = stroke_width; // 🔧 修复：起始位置考虑描边宽度
    
    for (int i = 0; i < line_count; i++) {
        if (strlen(lines[i]) > 256) {
            ss_log_e("行 %d 过长: %zu 字符", i, strlen(lines[i]));
            return TD_FAILURE;
        }
        // 先渲染描边
        for (int offset = 1; offset <= stroke_width; offset++) {
            // 只渲染外圈描边，减少渲染次数
            for (int dir = 0; dir < 8; dir++) {
                // 🔧 修复：每次渲染描边前强制设置字体样式
                TTF_SetFontStyle(font, TTF_STYLE_NORMAL);
                int x = 0, y = 0;
                switch (dir) {
                    case 0: x = offset; y = 0; break;     // 右
                    case 1: x = -offset; y = 0; break;    // 左
                    case 2: x = 0; y = offset; break;    // 下
                    case 3: x = 0; y = -offset; break;    // 上
                    case 4: x = offset; y = offset; break;   // 右下
                    case 5: x = -offset; y = offset; break;  // 左下
                    case 6: x = offset; y = -offset; break;  // 右上
                    case 7: x = -offset; y = -offset; break; // 左上
                }
                
                SDL_Surface *stroke_surface = TTF_RenderUTF8_Solid(font, lines[i], stroke_color);
                if (stroke_surface) {
                    int align_x = 0;
                    switch (alignment) {
                        case ALIGN_CENTER:
                            align_x = (target_surface->w - stroke_surface->w) / 2;
                            break;
                        case ALIGN_RIGHT:
                            align_x = target_surface->w - stroke_surface->w;
                            break;
                        case ALIGN_LEFT:
                        default:
                            align_x = 0;
                            break;
                    }
                    
                    SDL_Rect dst_rect = {align_x + x + stroke_width, 
                                       current_y + y + stroke_width, 
                                       stroke_surface->w, stroke_surface->h};
                    SDL_BlitSurface(stroke_surface, NULL, target_surface, &dst_rect);
                    SDL_FreeSurface(stroke_surface);
                }
            }
        }
        
        // 渲染主体文字
        TTF_SetFontStyle(font, TTF_STYLE_NORMAL);
        SDL_Surface *text_surface = TTF_RenderUTF8_Blended(font, lines[i], fg_color);
        if (text_surface) {
            int align_x = 0;
            switch (alignment) {
                case ALIGN_CENTER:
                    align_x = (target_surface->w - text_surface->w) / 2;
                    break;
                case ALIGN_RIGHT:
                    align_x = target_surface->w - text_surface->w;
                    break;
                case ALIGN_LEFT:
                default:
                    align_x = 0;
                    break;
            }

            // 🔧 修复：检查边界
            if (current_y + text_surface->h > target_surface->h - stroke_width) {
                ss_log_e("描边文本行 %d 超出边界: Y=%d, 高度=%d", i, current_y, text_surface->h);
                SDL_FreeSurface(text_surface);
                return TD_FAILURE;
            }
            
            SDL_Rect text_rect = {align_x + stroke_width, 
                                current_y + stroke_width, 
                                text_surface->w, text_surface->h};
            SDL_BlitSurface(text_surface, NULL, target_surface, &text_rect);
            SDL_FreeSurface(text_surface);
        }
        
        // 🔧 修复：正确更新Y坐标（考虑描边和行间距）
        SDL_Surface *sample = TTF_RenderUTF8_Blended(font, lines[i], fg_color);
        if (sample) {
            current_y += sample->h;
            if (i < line_count - 1) { // 不是最后一行
                current_y += line_spacing;
            }
            SDL_FreeSurface(sample);
        }
    }
    
    return TD_SUCCESS;
}

/**
 * @brief 多行文本渲染主函数（支持对齐和描边）
 */
int ttf_get_multiline_bmp(ot_bmpp *osd_bmp, const char *osd_buf, SDL_Color color, 
                         int32_t fontSize, SDL_Color *stroke_color, int stroke_width,
                         TextAlignment alignment, int line_spacing) {
    
    int ret = TD_FAILURE;
    
    // 确保TTF已初始化
    pthread_mutex_lock(&g_mux);
    ss_log_d("初始化TTF库");
    if (TTF_Init() == -1) {
        ss_log_e("TTF_Init failed: %s", TTF_GetError());
        pthread_mutex_unlock(&g_mux);
        return TD_FAILURE;
    }
    ss_log_d("TTF库初始化完成");
    
    // 直接加载字体
    const char *font_path = DEFAULT_FONT_PATH;

    TTF_Font *font = TTF_OpenFont(font_path, fontSize);
    
    // 如果默认字体加载失败，尝试备用字体
    if (!font && strcmp(font_path, FALLBACK_FONT_PATH) != 0) {
        ss_log_w("默认字体加载失败，尝试备用字体: %s", FALLBACK_FONT_PATH);
        font = TTF_OpenFont(FALLBACK_FONT_PATH, fontSize);
    }
    
    if (!font) {
        ss_log_e("字体加载失败");
        pthread_mutex_unlock(&g_mux);
        return TD_FAILURE;
    }
    
    pthread_mutex_unlock(&g_mux);

    if (!font) {
        ss_log_e("无法打开字体文件: %s, 字号: %d", DEFAULT_FONT_PATH, fontSize);
        return TD_FAILURE;
    }

    // 添加日志：记录输入参数
    ss_log_d("渲染多行文字: %s, 字号: %d, 对齐: %d, 行距: %d", 
             osd_buf, fontSize, alignment, line_spacing);

    // 🔧 修复：彻底重置字体样式
    TTF_SetFontStyle(font, TTF_STYLE_NORMAL);
    
    // 三重保险：多次设置以确保生效
    for (int i = 0; i < 3; i++) {
        TTF_SetFontStyle(font, TTF_STYLE_NORMAL);
    }
    
    // 验证样式设置
    int current_style = TTF_GetFontStyle(font);
    if (current_style != TTF_STYLE_NORMAL) {
        ss_log_e("字体样式设置失败: %d", current_style);
        return TD_FAILURE;
    }

    
    // 分割文本为多行
    char **lines = NULL;
    int line_count = 0;
    if (split_text_lines(osd_buf, &lines, &line_count) != TD_SUCCESS) {
        ss_log_e("分割文本行失败");
        return TD_FAILURE;
    }

    
    // // 检查行数是否合理
    // if (line_count > 20) {
    //     ss_log_e("行数过多: %d", line_count);
    //     for (int i = 0; i < line_count; i++) free(lines[i]);
    //     free(lines);
    //     return TD_FAILURE;
    // }
    
    // 计算多行文本尺寸
    int max_width = 0, total_height = 0;
    if (calculate_multiline_size(font, lines, line_count, &max_width, &total_height, line_spacing) != TD_SUCCESS) {
        ss_log_e("计算多行文本尺寸失败");
        for (int i = 0; i < line_count; i++) free(lines[i]);
        free(lines);
        return TD_FAILURE;
    }

    // 检查尺寸是否合理
    if (max_width > 4096 || total_height > 4096) {
        ss_log_e("文本尺寸过大: %dx%d", max_width, total_height);
        for (int i = 0; i < line_count; i++) free(lines[i]);
        free(lines);
        return TD_FAILURE;
    }
    
    // 考虑描边宽度
    if (stroke_color && stroke_width > 0) {
        max_width += stroke_width * 2;
        total_height += stroke_width * 2;
    }
    
    // 创建32位表面用于合成
    Uint32 rmask32 = 0x00FF0000, gmask32 = 0x0000FF00, bmask32 = 0x000000FF, amask32 = 0xFF000000;
    SDL_Surface *final_32bit_surface = SDL_CreateRGBSurface(0, max_width, total_height, 32,
                                                           rmask32, gmask32, bmask32, amask32);
    if (!final_32bit_surface) {
        ss_log_e("创建多行文本表面失败");
        for (int i = 0; i < line_count; i++) free(lines[i]);
        free(lines);
        return TD_FAILURE;
    }
    
    SDL_SetSurfaceBlendMode(final_32bit_surface, SDL_BLENDMODE_BLEND);

    
    
    // 渲染多行文本
    if (stroke_color && stroke_width > 0) {
        ret = render_multiline_text_with_stroke(font, color, lines, line_count, 
                                               final_32bit_surface, *stroke_color, stroke_width,
                                               alignment, line_spacing);
    } else {
        ret = render_multiline_text(font, color, lines, line_count, 
                                   final_32bit_surface, alignment, line_spacing);
    }
    
    // 清理行数据
    for (int i = 0; i < line_count; i++) free(lines[i]);
    free(lines);
    
    if (ret == TD_SUCCESS) {
        // 转换为16位格式
        Uint32 rmask16 = 0x7C00, gmask16 = 0x03E0, bmask16 = 0x001F, amask16 = 0x8000;
        SDL_Surface *target_16bit_surface = SDL_CreateRGBSurface(0, final_32bit_surface->w, 
                                                                 final_32bit_surface->h, 16,
                                                                 rmask16, gmask16, bmask16, amask16);
        if (target_16bit_surface) {
            if (convert_32bit_to_16bit(final_32bit_surface, target_16bit_surface) == TD_SUCCESS) {
                size_t buffer_size = target_16bit_surface->pitch * target_16bit_surface->h;
                osd_bmp->data = malloc(buffer_size);
                if (osd_bmp->data) {
                    memcpy(osd_bmp->data, target_16bit_surface->pixels, buffer_size);
                    osd_bmp->pixel_format = OT_PIXEL_FORMAT_ARGB_1555;
                    osd_bmp->width = target_16bit_surface->w;
                    osd_bmp->height = target_16bit_surface->h;
                    osd_bmp->pitch = target_16bit_surface->pitch;
                    
                    // 🔧 优化：存储渲染参数到ot_bmpp结构体
                    osd_bmp->text_color = color;
                    osd_bmp->font_size = fontSize;
                    osd_bmp->alignment = alignment;
                    osd_bmp->line_spacing = line_spacing;
                    
                    if (stroke_color && stroke_width > 0) {
                        osd_bmp->stroke_color = *stroke_color;
                        osd_bmp->stroke_width = stroke_width;
                        osd_bmp->has_stroke = 1;
                    } else {
                        osd_bmp->stroke_width = 0;
                        osd_bmp->has_stroke = 0;
                        // 设置默认描边颜色（黑色）
                        osd_bmp->stroke_color = (SDL_Color){0, 0, 0, 255};
                    }
                    
                    ret = TD_SUCCESS;
                    ss_log_d("渲染参数已存储: 颜色(%d,%d,%d,%d), 字号=%d, 对齐=%d, 行距=%d, 描边=%s", 
                            osd_bmp->text_color.r, osd_bmp->text_color.g, osd_bmp->text_color.b, osd_bmp->text_color.a,
                            osd_bmp->font_size, osd_bmp->alignment, osd_bmp->line_spacing,
                            osd_bmp->has_stroke ? "是" : "否");
                }
            }
            SDL_FreeSurface(target_16bit_surface);
        }
    }
    
    SDL_FreeSurface(final_32bit_surface);
    
    // 释放字体引用
    pthread_mutex_lock(&g_mux);
    
    pthread_mutex_unlock(&g_mux);

    // 在ttf_get_multiline_bmp函数末尾添加验证
    ss_log_d("生成的位图: %dx%d, 格式: %s, 步长: %d", 
         osd_bmp->width, osd_bmp->height,
         (osd_bmp->pixel_format == OT_PIXEL_FORMAT_ARGB_1555) ? "16位ARGB1555" : "其他格式",
         osd_bmp->pitch);
    
    return ret;
}



/**
 * @brief 确保字体样式为正常样式
 * @param font 字体对象指针
 * @return 成功返回TD_SUCCESS，失败返回TD_FAILURE
 */
static int ensure_font_style_normal(TTF_Font *font) {
    if (!font) {
        ss_log_e("字体指针为空");
        return TD_FAILURE;
    }
    
    // 强制设置为正常
    TTF_SetFontStyle(font, TTF_STYLE_NORMAL);
    
    // 五重保险：多次设置以确保生效
    for (int i = 0; i < 5; i++) {
        TTF_SetFontStyle(font, TTF_STYLE_NORMAL);
        // 添加微小延迟，确保设置生效
        usleep(1000); // 1毫秒延迟
    }
    
    // 验证设置
    int verified_style = TTF_GetFontStyle(font);
    if (verified_style != TTF_STYLE_NORMAL) {
        ss_log_e("字体样式设置失败: %d", verified_style);
        
        // 🔧 修复：尝试终极修复 - 完全重新初始化
        int fontSize = TTF_FontHeight(font);
        const char *fontPath = "/root/app/SimHei.ttf";
        
        // 使用返回值进行日志记录
        ss_log_e("字体样式严重错误，当前字体高度: %d，建议完全重新初始化字体系统", fontSize);
        return TD_FAILURE;
    }
    
    return TD_SUCCESS;
}




/**
 * @brief 从BMP文件加载图像数据到ot_bmpp结构
 * @param filename BMP文件路径
 * @param osd_bmp 存储图像数据的结构体指针
 * @return 成功返回TD_SUCCESS，失败返回TD_FAILURE
 */
int bmp_load_file(const char *filename, ot_bmpp *osd_bmp) {
    if (!filename || !osd_bmp) {
        ss_log_e("参数错误: 文件名或输出结构为空");
        return TD_FAILURE;
    }
    
    // 检查文件是否存在
    if (access(filename, F_OK) != 0) {
        ss_log_e("文件不存在: %s", filename);
        return TD_FAILURE;
    }
    
    FILE *file = fopen(filename, "rb");
    if (!file) {
        ss_log_e("无法打开文件: %s", filename);
        return TD_FAILURE;
    }
    
    // 读取文件头
    BMPFileHeader file_header;
    if (fread(&file_header, sizeof(BMPFileHeader), 1, file) != 1) {
        ss_log_e("读取BMP文件头失败");
        fclose(file);
        return TD_FAILURE;
    }
    
    // 检查BMP文件标识
    if (file_header.bfType != 0x4D42) { // "BM"
        ss_log_e("不是有效的BMP文件: 标识符=%04X", file_header.bfType);
        fclose(file);
        return TD_FAILURE;
    }
    
    // 读取信息头
    BMPInfoHeader info_header;
    if (fread(&info_header, sizeof(BMPInfoHeader), 1, file) != 1) {
        ss_log_e("读取BMP信息头失败");
        fclose(file);
        return TD_FAILURE;
    }
    
    // 检查支持的格式
    if (info_header.biCompression != 0) {
        ss_log_e("不支持压缩的BMP格式: 压缩类型=%u", info_header.biCompression);
        fclose(file);
        return TD_FAILURE;
    }
    
    ss_log_d("BMP文件信息: %dx%d, 位深=%d, 大小=%u字节", 
             info_header.biWidth, info_header.biHeight, 
             info_header.biBitCount, file_header.bfSize);
    
    // 🔧 优化：初始化所有字段
    osd_bmp->palette = NULL;
    osd_bmp->palette_size = 0;
    osd_bmp->stroke_mask = NULL;
    
    // 初始化渲染参数字段（从文件加载时设为默认值）
    osd_bmp->text_color = (SDL_Color){255, 255, 255, 255}; // 默认白色
    osd_bmp->stroke_color = (SDL_Color){0, 0, 0, 255}; // 默认黑色描边
    osd_bmp->stroke_width = 0;
    osd_bmp->font_size = 0; // 从文件加载时无法确定字号
    osd_bmp->alignment = ALIGN_LEFT; // 默认左对齐
    osd_bmp->line_spacing = 0;
    osd_bmp->has_stroke = 0;
    
    // 读取颜色表（仅适用于1/4/8位色深）
    if (info_header.biBitCount <= 8 && info_header.biClrUsed > 0) {
        osd_bmp->palette_size = info_header.biClrUsed;
        osd_bmp->palette = malloc(osd_bmp->palette_size * sizeof(SDL_Color));
        if (!osd_bmp->palette) {
            ss_log_e("分配颜色表内存失败");
            fclose(file);
            return TD_FAILURE;
        }
        
        // 读取颜色表数据
        if (fread(osd_bmp->palette, sizeof(SDL_Color), osd_bmp->palette_size, file) != osd_bmp->palette_size) {
            ss_log_e("读取颜色表失败");
            free(osd_bmp->palette);
            fclose(file);
            return TD_FAILURE;
        }
    }
    
    // 🔧 优化：检查是否有扩展渲染参数
    size_t extended_data_size = file_header.bfOffBits - (sizeof(BMPFileHeader) + sizeof(BMPInfoHeader) + osd_bmp->palette_size * sizeof(SDL_Color));
    
    if (extended_data_size > 0) {
        // 读取扩展渲染参数
        SDL_Color text_color, stroke_color;
        int stroke_width, font_size, alignment, line_spacing, has_stroke;
        
        if (fread(&text_color, sizeof(SDL_Color), 1, file) != 1 ||
            fread(&stroke_color, sizeof(SDL_Color), 1, file) != 1 ||
            fread(&stroke_width, sizeof(int), 1, file) != 1 ||
            fread(&font_size, sizeof(int), 1, file) != 1 ||
            fread(&alignment, sizeof(int), 1, file) != 1 ||
            fread(&line_spacing, sizeof(int), 1, file) != 1 ||
            fread(&has_stroke, sizeof(int), 1, file) != 1) {
            ss_log_e("读取扩展渲染参数失败");
        } else {
            // 存储到ot_bmpp结构体
            osd_bmp->text_color = text_color;
            osd_bmp->stroke_color = stroke_color;
            osd_bmp->stroke_width = stroke_width;
            osd_bmp->font_size = font_size;
            osd_bmp->alignment = (TextAlignment)alignment;
            osd_bmp->line_spacing = line_spacing;
            osd_bmp->has_stroke = has_stroke;
            
            ss_log_d("从BMP文件读取扩展渲染参数: 字号=%d, 描边=%s", 
                    osd_bmp->font_size, osd_bmp->has_stroke ? "是" : "否");
        }
    }
    
    // 定位到像素数据
    if (fseek(file, file_header.bfOffBits, SEEK_SET) != 0) {
        ss_log_e("定位到像素数据失败: 偏移量=%u", file_header.bfOffBits);
        if (osd_bmp->palette) free(osd_bmp->palette);
        fclose(file);
        return TD_FAILURE;
    }
    
    // 计算步长（每行字节数，BMP文件行对齐到4字节）
    int width = info_header.biWidth;
    int height = abs(info_header.biHeight); // 高度可能为负（自上而下存储）
    int src_stride = ((width * info_header.biBitCount + 31) / 32) * 4;
    int dst_stride = width * 2; // ARGB1555每像素2字节
    
    // 分配内存
    size_t buffer_size = dst_stride * height;
    osd_bmp->data = malloc(buffer_size);
    if (!osd_bmp->data) {
        ss_log_e("分配内存失败: %zu字节", buffer_size);
        if (osd_bmp->palette) free(osd_bmp->palette);
        fclose(file);
        return TD_FAILURE;
    }
    
    // 分配描边掩码内存
    osd_bmp->stroke_mask = calloc(width * height, sizeof(uint8_t));
    if (!osd_bmp->stroke_mask) {
        ss_log_e("分配描边掩码内存失败");
        free(osd_bmp->data);
        if (osd_bmp->palette) free(osd_bmp->palette);
        fclose(file);
        return TD_FAILURE;
    }
    
    // 根据位深度处理不同的像素格式
    int ret = TD_SUCCESS;
    
    switch (info_header.biBitCount) {
        case 16: {
            // 16位BMP（可能是RGB555或RGB565）
            uint16_t *line_buffer = malloc(src_stride);
            if (!line_buffer) {
                ss_log_e("分配行缓冲区失败");
                ret = TD_FAILURE;
                break;
            }
            
            // BMP文件像素数据是从下到上存储的，需要反向读取
            for (int y = height - 1; y >= 0; y--) {
                if (fread(line_buffer, src_stride, 1, file) != 1) {
                    ss_log_e("读取第%d行数据失败", y);
                    ret = TD_FAILURE;
                    break;
                }
                
                // 转换为ARGB1555格式
                uint16_t *dst_line = (uint16_t *)osd_bmp->data + y * width;
                for (int x = 0; x < width; x++) {
                    uint16_t src_pixel = line_buffer[x];
                    
                    // 假设源格式是RGB555，转换为ARGB1555
                    uint16_t dst_pixel = 0;
                    if (src_pixel & 0x8000) dst_pixel |= 0x8000; // Alpha
                    dst_pixel |= (src_pixel & 0x7C00);           // Red
                    dst_pixel |= (src_pixel & 0x03E0);           // Green  
                    dst_pixel |= (src_pixel & 0x001F);           // Blue
                    
                    dst_line[x] = dst_pixel;
                    
                    // 标记描边像素（示例：Alpha为0的像素）
                    if ((src_pixel & 0x8000) == 0) {
                        osd_bmp->stroke_mask[y * width + x] = 1;
                    }
                }
            }
            
            free(line_buffer);
            osd_bmp->pixel_format = OT_PIXEL_FORMAT_ARGB_1555;
            break;
        }
        
        case 24: {
            // 24位BMP（BGR888格式）
            uint8_t *line_buffer = malloc(src_stride);
            if (!line_buffer) {
                ss_log_e("分配行缓冲区失败");
                ret = TD_FAILURE;
                break;
            }
            
            // BMP文件像素数据是从下到上存储的，需要反向读取
            for (int y = height - 1; y >= 0; y--) {
                if (fread(line_buffer, src_stride, 1, file) != 1) {
                    ss_log_e("读取第%d行数据失败", y);
                    ret = TD_FAILURE;
                    break;
                }
                
                // 转换BGR888到ARGB1555
                uint16_t *dst_line = (uint16_t *)osd_bmp->data + y * width;
                for (int x = 0; x < width; x++) {
                    uint8_t b = line_buffer[x * 3 + 0];
                    uint8_t g = line_buffer[x * 3 + 1];
                    uint8_t r = line_buffer[x * 3 + 2];
                    
                    // 转换为ARGB1555格式
                    uint16_t pixel = 0x8000; // 全不透明
                    pixel |= ((r >> 3) << 10) & 0x7C00;
                    pixel |= ((g >> 3) << 5) & 0x03E0;
                    pixel |= (b >> 3) & 0x001F;
                    
                    dst_line[x] = pixel;
                }
            }
            
            free(line_buffer);
            osd_bmp->pixel_format = OT_PIXEL_FORMAT_ARGB_1555;
            break;
        }
        
        case 32: {
            // 32位BMP（BGRA8888格式）
            uint32_t *line_buffer = malloc(src_stride);
            if (!line_buffer) {
                ss_log_e("分配行缓冲区失败");
                ret = TD_FAILURE;
                break;
            }
            
            // BMP文件像素数据是从下到上存储的，需要反向读取
            for (int y = height - 1; y >= 0; y--) {
                if (fread(line_buffer, src_stride, 1, file) != 1) {
                    ss_log_e("读取第%d行数据失败", y);
                    ret = TD_FAILURE;
                    break;
                }
                
                // 转换BGRA8888到ARGB1555
                uint16_t *dst_line = (uint16_t *)osd_bmp->data + y * width;
                for (int x = 0; x < width; x++) {
                    uint32_t src_pixel = line_buffer[x];
                    uint8_t a = (src_pixel >> 24) & 0xFF;
                    uint8_t r = (src_pixel >> 16) & 0xFF;
                    uint8_t g = (src_pixel >> 8) & 0xFF;
                    uint8_t b = src_pixel & 0xFF;
                    
                    // 转换为ARGB1555格式
                    uint16_t dst_pixel = 0;
                    if (a > 128) dst_pixel |= 0x8000; // Alpha阈值
                    dst_pixel |= ((r >> 3) << 10) & 0x7C00;
                    dst_pixel |= ((g >> 3) << 5) & 0x03E0;
                    dst_pixel |= (b >> 3) & 0x001F;
                    
                    dst_line[x] = dst_pixel;
                }
            }
            
            free(line_buffer);
            osd_bmp->pixel_format = OT_PIXEL_FORMAT_ARGB_1555;
            break;
        }
        
        default:
            ss_log_e("不支持的BMP位深度: %d", info_header.biBitCount);
            ret = TD_FAILURE;
            break;
    }
    
    if (ret == TD_SUCCESS) {
        // 设置ot_bmpp结构体属性
        osd_bmp->width = width;
        osd_bmp->height = height;
        osd_bmp->pitch = dst_stride;
        
        ss_log_d("BMP加载成功: %dx%d, 格式=%d, 步长=%d, 调色板大小=%d, 描边掩码=%s", 
                 osd_bmp->width, osd_bmp->height, osd_bmp->pixel_format, osd_bmp->pitch, 
                 osd_bmp->palette_size, osd_bmp->stroke_mask ? "是" : "否");
    } else {
        // 失败时释放内存
        if (osd_bmp->data) {
            free(osd_bmp->data);
            osd_bmp->data = NULL;
        }
    }
    
    fclose(file);
    return ret;
}

/**
 * @brief 释放ot_bmpp结构体内存
 * @param osd_bmp 要释放的结构体指针
 */
void bmp_free(ot_bmpp *osd_bmp) {
    if (osd_bmp) {
        if (osd_bmp->data) {
            free(osd_bmp->data);
            osd_bmp->data = NULL;
        }
        if (osd_bmp->palette) {
            free(osd_bmp->palette);
            osd_bmp->palette = NULL;
        }
        if (osd_bmp->stroke_mask) {
            free(osd_bmp->stroke_mask);
            osd_bmp->stroke_mask = NULL;
        }
        
        // 重置所有字段
        osd_bmp->width = 0;
        osd_bmp->height = 0;
        osd_bmp->pitch = 0;
        osd_bmp->pixel_format = 0;
        osd_bmp->text_color = (SDL_Color){0, 0, 0, 0};
        osd_bmp->stroke_color = (SDL_Color){0, 0, 0, 0};
        osd_bmp->stroke_width = 0;
        osd_bmp->font_size = 0;
        osd_bmp->alignment = ALIGN_LEFT;
        osd_bmp->line_spacing = 0;
        osd_bmp->has_stroke = 0;
        
        ss_log_d("ot_bmpp结构体内存已完全释放并重置");
    }
}

/**
 * @brief 获取ot_bmpp中存储的渲染参数
 * @param osd_bmp 位图结构体指针
 * @param text_color 输出文本颜色
 * @param stroke_color 输出描边颜色
 * @param stroke_width 输出描边宽度
 * @param font_size 输出字体大小
 * @param alignment 输出对齐方式
 * @param line_spacing 输出行间距
 * @return 成功返回TD_SUCCESS，失败返回TD_FAILURE
 */
int bmp_get_render_params(ot_bmpp *osd_bmp, SDL_Color *text_color, SDL_Color *stroke_color, 
                         int *stroke_width, int *font_size, TextAlignment *alignment, int *line_spacing) {
    if (!osd_bmp) {
        ss_log_e("位图结构体指针为空");
        return TD_FAILURE;
    }
    
    // 复制渲染参数到输出参数
    if (text_color) *text_color = osd_bmp->text_color;
    if (stroke_color) *stroke_color = osd_bmp->stroke_color;
    if (stroke_width) *stroke_width = osd_bmp->stroke_width;
    if (font_size) *font_size = osd_bmp->font_size;
    if (alignment) *alignment = osd_bmp->alignment;
    if (line_spacing) *line_spacing = osd_bmp->line_spacing;
    
    ss_log_d("获取渲染参数: 文字颜色(%d,%d,%d,%d), 字号=%d, 对齐=%d, 描边=%s", 
            osd_bmp->text_color.r, osd_bmp->text_color.g, osd_bmp->text_color.b, osd_bmp->text_color.a,
            osd_bmp->font_size, osd_bmp->alignment, osd_bmp->has_stroke ? "是" : "否");
    
    return TD_SUCCESS;
}



/**
 * @brief 将ot_bmpp数据保存为BMP文件
 * @param osd_bmp 包含位图数据的结构体指针
 * @param filename 要保存的文件名
 * @return 成功返回TD_SUCCESS，失败返回TD_FAILURE
 */
int save_bmp_file(ot_bmpp *osd_bmp, const char *filename) {
    pthread_mutex_lock(&g_mux);
    if (!osd_bmp || !osd_bmp->data) {
        ss_log_e("无效的位图数据");
        pthread_mutex_unlock(&g_mux);
        return TD_FAILURE;
    }
    
    // 如果不是16位格式，先转换为16位
    ot_bmpp temp_bmp;
    memset(&temp_bmp, 0, sizeof(ot_bmpp));
    
    if (osd_bmp->pixel_format != OT_PIXEL_FORMAT_ARGB_1555) {
        ss_log_d("非16位格式，尝试转换为16位ARGB1555");
        
        // 创建临时表面用于转换
        Uint32 rmask32 = 0x00FF0000, gmask32 = 0x0000FF00, bmask32 = 0x000000FF, amask32 = 0xFF000000;
        SDL_Surface *src_surface = SDL_CreateRGBSurfaceFrom(
            osd_bmp->data, osd_bmp->width, osd_bmp->height, 32, osd_bmp->pitch,
            rmask32, gmask32, bmask32, amask32
        );
        
        if (!src_surface) {
            ss_log_e("创建源表面失败: %s", SDL_GetError());
            pthread_mutex_unlock(&g_mux);
            return TD_FAILURE;
        }
        
        // 创建目标16位表面
        Uint32 rmask16 = 0x7C00, gmask16 = 0x03E0, bmask16 = 0x001F, amask16 = 0x8000;
        SDL_Surface *dest_surface = SDL_CreateRGBSurface(
            0, osd_bmp->width, osd_bmp->height, 16,
            rmask16, gmask16, bmask16, amask16
        );
        
        if (!dest_surface) {
            ss_log_e("创建目标表面失败: %s", SDL_GetError());
            SDL_FreeSurface(src_surface);
            pthread_mutex_unlock(&g_mux);
            return TD_FAILURE;
        }
        
        // 转换格式
        if (convert_32bit_to_16bit(src_surface, dest_surface) != TD_SUCCESS) {
            ss_log_e("格式转换失败");
            SDL_FreeSurface(src_surface);
            SDL_FreeSurface(dest_surface);
            pthread_mutex_unlock(&g_mux);
            return TD_FAILURE;
        }
        
        // 填充临时bmp结构
        temp_bmp.width = osd_bmp->width;
        temp_bmp.height = osd_bmp->height;
        temp_bmp.pixel_format = OT_PIXEL_FORMAT_ARGB_1555;
        temp_bmp.pitch = dest_surface->pitch;
        
        size_t buffer_size = dest_surface->pitch * dest_surface->h;
        temp_bmp.data = malloc(buffer_size);
        if (!temp_bmp.data) {
            ss_log_e("分配内存失败");
            SDL_FreeSurface(src_surface);
            SDL_FreeSurface(dest_surface);
            pthread_mutex_unlock(&g_mux);
            return TD_FAILURE;
        }
        
        memcpy(temp_bmp.data, dest_surface->pixels, buffer_size);
        
        // 复制渲染参数
        temp_bmp.text_color = osd_bmp->text_color;
        temp_bmp.stroke_color = osd_bmp->stroke_color;
        temp_bmp.stroke_width = osd_bmp->stroke_width;
        temp_bmp.font_size = osd_bmp->font_size;
        temp_bmp.alignment = osd_bmp->alignment;
        temp_bmp.line_spacing = osd_bmp->line_spacing;
        temp_bmp.has_stroke = osd_bmp->has_stroke;
        
        SDL_FreeSurface(src_surface);
        SDL_FreeSurface(dest_surface);
        
        // 使用临时bmp
        osd_bmp = &temp_bmp;
    }
    
    FILE *file = fopen(filename, "wb");
    if (!file) {
        ss_log_e("无法创建文件: %s", filename);
        if (osd_bmp == &temp_bmp) {
            free(temp_bmp.data);
        }
        pthread_mutex_unlock(&g_mux);
        return TD_FAILURE;
    }
    
    // 创建BMP文件头和信息头
    BMPFileHeader file_header = {0};
    BMPInfoHeader info_header = {0};
    
    // 计算扩展数据大小（渲染参数）
    size_t extended_data_size = 0;
    if (osd_bmp->has_stroke || osd_bmp->font_size > 0) {
        extended_data_size = sizeof(SDL_Color) * 2 + sizeof(int) * 5;
    }
    
    // 设置文件头
    file_header.bfType = 0x4D42; // "BM"
    file_header.bfOffBits = sizeof(BMPFileHeader) + sizeof(BMPInfoHeader) + extended_data_size;
    file_header.bfSize = file_header.bfOffBits + osd_bmp->height * osd_bmp->pitch;
    
    // 设置信息头
    info_header.biSize = sizeof(BMPInfoHeader);
    info_header.biWidth = osd_bmp->width;
    info_header.biHeight = osd_bmp->height;
    info_header.biPlanes = 1;
    info_header.biBitCount = 16; // 16位
    info_header.biCompression = 0; // 不压缩
    info_header.biSizeImage = osd_bmp->height * osd_bmp->pitch;
    
    // 写入文件头和信息头
    fwrite(&file_header, sizeof(BMPFileHeader), 1, file);
    fwrite(&info_header, sizeof(BMPInfoHeader), 1, file);
    
    // 写入扩展渲染参数（如果有的话）
    if (extended_data_size > 0) {
        fwrite(&osd_bmp->text_color, sizeof(SDL_Color), 1, file);
        fwrite(&osd_bmp->stroke_color, sizeof(SDL_Color), 1, file);
        fwrite(&osd_bmp->stroke_width, sizeof(int), 1, file);
        fwrite(&osd_bmp->font_size, sizeof(int), 1, file);
        fwrite(&osd_bmp->alignment, sizeof(int), 1, file);
        fwrite(&osd_bmp->line_spacing, sizeof(int), 1, file);
        fwrite(&osd_bmp->has_stroke, sizeof(int), 1, file);
        
        ss_log_d("扩展渲染参数已写入BMP文件: 字号=%d, 描边=%s", 
                osd_bmp->font_size, osd_bmp->has_stroke ? "是" : "否");
    }
    
    // 写入像素数据（注意BMP是从下到上存储的）
    for (int y = osd_bmp->height - 1; y >= 0; y--) {
        uint16_t *line = (uint16_t *)osd_bmp->data + y * osd_bmp->width;
        fwrite(line, osd_bmp->pitch, 1, file);
    }
    
    fclose(file);
    
    // 清理临时数据
    if (osd_bmp == &temp_bmp) {
        free(temp_bmp.data);
    }
    
    ss_log_d("BMP文件保存成功: %s", filename);
    pthread_mutex_unlock(&g_mux);
    return TD_SUCCESS;
}

/**
 * @brief 垂直拼接多个BMP文件
 * @param filenames BMP文件路径数组
 * @param count 文件数量
 * @param alignment 对齐方式（ALIGN_LEFT/ALIGN_CENTER/ALIGN_RIGHT）
 * @param output_filename 输出文件路径
 * @return 成功返回TD_SUCCESS，失败返回TD_FAILURE
 */
int bmp_concatenate_vertically(const char **filenames, int count, TextAlignment alignment, const char *output_filename) {
    if (!filenames || count <= 0 || !output_filename) {
        ss_log_e("参数错误: 文件名数组、数量或输出文件名为空");
        return TD_FAILURE;
    }

    // 加载所有 BMP 文件
    ot_bmpp *bmps = malloc(count * sizeof(ot_bmpp));
    if (!bmps) {
        ss_log_e("分配内存失败");
        return TD_FAILURE;
    }

    int max_width = 0;
    int total_height = 0;
    for (int i = 0; i < count; i++) {
        memset(&bmps[i], 0, sizeof(ot_bmpp));
        if (bmp_load_file(filenames[i], &bmps[i]) != TD_SUCCESS) {
            ss_log_e("加载文件失败: %s", filenames[i]);
            // 清理已加载的 BMP
            for (int j = 0; j < i; j++) bmp_free(&bmps[j]);
            free(bmps);
            return TD_FAILURE;
        }
        if (bmps[i].width > max_width) max_width = bmps[i].width;
        total_height += bmps[i].height;
    }

    // 创建输出 BMP
    ot_bmpp output_bmp = {
        .width = max_width,
        .height = total_height,
        .pixel_format = OT_PIXEL_FORMAT_ARGB_1555,
        .pitch = max_width * 2 // 16 位色深
    };
    output_bmp.data = malloc(output_bmp.pitch * total_height);
    if (!output_bmp.data) {
        for (int i = 0; i < count; i++) bmp_free(&bmps[i]);
        free(bmps);
        return TD_FAILURE;
    }
    memset(output_bmp.data, 0, output_bmp.pitch * total_height); // 初始化像素数据

    // 拼接像素数据
    uint16_t *dst = (uint16_t *)output_bmp.data;
    int current_y = 0;
    for (int i = 0; i < count; i++) {
        int offset_x = (alignment == ALIGN_CENTER) ? (max_width - bmps[i].width) / 2 :
                      (alignment == ALIGN_RIGHT) ? max_width - bmps[i].width : 0;

        uint16_t *src = (uint16_t *)bmps[i].data;
        for (int y = 0; y < bmps[i].height; y++) {
            for (int x = 0; x < bmps[i].width; x++) {
                dst[(current_y + y) * max_width + offset_x + x] = src[y * bmps[i].width + x];
            }
        }
        current_y += bmps[i].height;
    }

    // 保存输出文件
    int ret = save_bmp_file(&output_bmp, output_filename);

    // 清理内存
    bmp_free(&output_bmp);
    for (int i = 0; i < count; i++) bmp_free(&bmps[i]);
    free(bmps);

    return ret;
}


/**
 * @brief 垂直拼接多个BMP文件并支持二次渲染
 * @param filenames BMP文件路径数组
 * @param count 文件数量
 * @param alignment 对齐方式（ALIGN_LEFT/ALIGN_CENTER/ALIGN_RIGHT）
 * @param output_filename 输出文件路径
 * @param out_merged_bmp 合并后的BMP结构体指针
 * @return 成功返回TD_SUCCESS，失败返回TD_FAILURE
 */
int bmp_concatenate_with_rendering(const char **filenames, int count, TextAlignment alignment, const char *output_filename, ot_bmpp **out_merged_bmp) {
    if (!filenames || count <= 0 || !output_filename) {
        ss_log_e("参数错误: 文件名数组、数量或输出文件名为空");
        return TD_FAILURE;
    }

    // 加载所有 BMP 文件
    ot_bmpp *bmps = malloc(count * sizeof(ot_bmpp));
    if (!bmps) {
        ss_log_e("分配内存失败");
        return TD_FAILURE;
    }

    int max_width = 0;
    int total_height = 0;
    for (int i = 0; i < count; i++) {
        memset(&bmps[i], 0, sizeof(ot_bmpp));
        if (bmp_load_file(filenames[i], &bmps[i]) != TD_SUCCESS) {
            ss_log_e("加载文件失败: %s", filenames[i]);
            // 清理已加载的 BMP
            for (int j = 0; j < i; j++) bmp_free(&bmps[j]);
            free(bmps);
            return TD_FAILURE;
        }
        if (bmps[i].width > max_width) max_width = bmps[i].width;
        total_height += bmps[i].height;
    }

    // 创建目标 BMP
    ot_bmpp merged_bmp = {
        .width = max_width,
        .height = total_height,
        .pixel_format = OT_PIXEL_FORMAT_ARGB_1555,
        .pitch = max_width * 2 // 16 位色深
    };
    merged_bmp.data = malloc(merged_bmp.pitch * total_height);
    if (!merged_bmp.data) {
        for (int i = 0; i < count; i++) bmp_free(&bmps[i]);
        free(bmps);
        return TD_FAILURE;
    }
    memset(merged_bmp.data, 0, merged_bmp.pitch * total_height); // 初始化像素数据

    // 拼接像素数据
    uint16_t *dst = (uint16_t *)merged_bmp.data;
    int current_y = 0;
    for (int i = 0; i < count; i++) {
        int offset_x = (alignment == ALIGN_CENTER) ? (max_width - bmps[i].width) / 2 :
                      (alignment == ALIGN_RIGHT) ? max_width - bmps[i].width : 0;

        uint16_t *src = (uint16_t *)bmps[i].data;
        for (int y = 0; y < bmps[i].height; y++) {
            for (int x = 0; x < bmps[i].width; x++) {
                dst[(current_y + y) * max_width + offset_x + x] = src[y * bmps[i].width + x];
            }
        }
        current_y += bmps[i].height;
    }

    // 保存输出文件
    int ret = save_bmp_file(&merged_bmp, output_filename);

    // 返回拼接后的 BMP 对象
    if (out_merged_bmp && ret == TD_SUCCESS) {
        *out_merged_bmp = malloc(sizeof(ot_bmpp));
        if (!*out_merged_bmp) {
            ret = TD_FAILURE;
            free(merged_bmp.data);
        } else {
            memcpy(*out_merged_bmp, &merged_bmp, sizeof(ot_bmpp));
        }
    } else {
        // 如果不需要返回对象或失败，释放内存
        free(merged_bmp.data);
    }

    // 清理加载的BMP文件内存
    for (int i = 0; i < count; i++) bmp_free(&bmps[i]);
    free(bmps);

    return ret;
}




static int verify_font_state(TTF_Font *font) {
    int style = TTF_GetFontStyle(font);
    int outline = TTF_GetFontOutline(font);
    
    if (style != TTF_STYLE_NORMAL || outline != 0) {
        ss_log_e("字体状态异常: style=%d, outline=%d", style, outline);
        
        // 强制修复
        TTF_SetFontStyle(font, TTF_STYLE_NORMAL);
        TTF_SetFontOutline(font, 0);
        
        return TD_FAILURE;
    }
    return TD_SUCCESS;
}

// 在main函数中添加测试代码
void test_font_style_consistency() {
    printf("=== 测试字体样式一致性 ===\n");
    
    for (int size = 12; size <= 36; size += 6) {
        ot_bmpp test_bmp;
        SDL_Color test_color = {255, 0, 0, 255};
        
        // 测试混合文本
        char *mixed_text = "a测试Text123\nbb混合文本\nccc中文English";
        
        for (int i = 0; i < 3; i++) {
            char filename[100];
            sprintf(filename, "consistency_test_%d_%d.bmp", size, i);
            
            if (ttf_get_multiline_bmp(&test_bmp, mixed_text, test_color, size, 
                                     NULL, 0, ALIGN_LEFT, 10) == TD_SUCCESS) {
                if (test_bmp.data) {
                    save_bmp_file(&test_bmp, filename);

                    free(test_bmp.data);
                    printf("生成: %s (字号: %dpt, 轮次: %d)\n", filename, size, i);
                }
            }
        }
    }
}

void test_concatenate_font_style_bmps() {
    printf("=== 测试字体样式BMP拼接 ===\n");

    // 生成所有可能的文件名
    const char *filenames[15]; // 5种字号 * 3轮次 = 15个文件
    int count = 0;

    // 先确保所有测试文件都存在
    for (int size = 12; size <= 36; size += 6) {
        for (int i = 0; i < 3; i++) {
            char *filename = malloc(100);
            sprintf(filename, "consistency_test_%d_%d.bmp", size, i);
            
            // 检查文件是否存在，如果不存在则生成
            if (access(filename, F_OK) != 0) {
                printf("生成缺失的测试文件: %s\n", filename);
                test_single_case("a测试Text123\nbb混合文本\nccc中文English", size, ALIGN_LEFT, 10, filename);
            }
            
            filenames[count++] = filename;
        }
    }

    // 调用拼接函数
    ot_bmpp *merged_bmp = NULL;
    if (bmp_concatenate_with_rendering(filenames, count, ALIGN_CENTER, "concatenated_font_style.bmp", &merged_bmp) == TD_SUCCESS) {
        printf("拼接成功: concatenated_font_style.bmp\n");
        if (merged_bmp) {
            bmp_free(merged_bmp);
            merged_bmp = NULL;
        }
    } else {
        printf("拼接失败\n");
    }

    // 释放文件名内存
    for (int i = 0; i < count; i++) {
        free((void *)filenames[i]);
    }
}

void test_minimal_case() {
    printf("=== 最小化测试 ===\n");
    
    // 测试1: 纯中文
    test_single_case("纯中文测试", 24, ALIGN_LEFT, 0, "chinese_only.bmp");
    
    // 测试2: 纯英文
    test_single_case("English only", 24, ALIGN_LEFT, 0, "english_only.bmp");
    
    // 测试3: 中英混合
    test_single_case("a中英混合Text", 24, ALIGN_LEFT, 0, "mixed.bmp");
    
    // 测试4: 多次渲染同一文本
    for (int i = 0; i < 5; i++) {
        char filename[100];
        sprintf(filename, "repeat_%d.bmp", i);
        test_single_case("a重复测试", 24, ALIGN_LEFT, 0, filename);
    }
}

void test_single_case(const char *text, int size, TextAlignment align, int spacing, const char *filename) {
    ot_bmpp bmp;
    SDL_Color color = {255, 0, 0, 255};
    
    if (ttf_get_multiline_bmp(&bmp, text, color, size, NULL, 0, align, spacing) == TD_SUCCESS) {
        if (bmp.data) {
            save_bmp_file(&bmp, filename);
            free(bmp.data);
            bmp.data = NULL;
            printf("生成: %s\n", filename);
        }
    }
}


void test_bmp_concatenation() {
    printf("=== 测试BMP合成功能 ===\n");

    // 1. 创建测试BMP文件
    const char *filenames[] = {
        "test1.bmp",
        "test2.bmp",
        "test3.bmp"
    };
    int count = sizeof(filenames) / sizeof(filenames[0]);

    // 生成测试BMP文件
    for (int i = 0; i < count; i++) {
        ot_bmpp bmp;
        SDL_Color color = {255, 0, 0, 255}; // 红色
        char text[50];
        sprintf(text, "测试文本 %d", i + 1);

        if (ttf_get_bmp(&bmp, text, color, 24, NULL, 0) == TD_SUCCESS) {
            save_bmp_file(&bmp, filenames[i]);
            bmp_free(&bmp);
            printf("生成测试文件: %s\n", filenames[i]);
            
        }
    }

    // 2. 测试垂直拼接（纯拼接）
    if (bmp_concatenate_vertically(filenames, count, ALIGN_CENTER, "concatenated_vertical.bmp") == TD_SUCCESS) {
        printf("垂直拼接成功: concatenated_vertical.bmp\n");
    } else {
        printf("垂直拼接失败\n");
    }

    // 3. 测试带渲染的垂直拼接
    ot_bmpp *merged_bmp = NULL;
    if (bmp_concatenate_with_rendering(filenames, count, ALIGN_CENTER, "concatenated_with_rendering.bmp", &merged_bmp) == TD_SUCCESS) {
        printf("带渲染的垂直拼接成功: concatenated_with_rendering.bmp\n");
        if (merged_bmp) {
            bmp_free(merged_bmp);
            merged_bmp = NULL;
        }
    } else {
        printf("带渲染的垂直拼接失败\n");
    }
}

int main__osd(int argc, char **argv) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL初始化失败: %s\n", SDL_GetError());
        return 1;
    }
    
    ot_bmpp osd_bmp;
    SDL_Color text_color = {255, 0, 0, 255}; // 红色文字
    SDL_Color stroke_color = {255, 255, 0, 255}; // 黄色描边
    
    printf("开始全面测试文字渲染功能...\n");
    
    // ====================
    // 1. 测试不同大小的单行文本（无描边）
    // ====================
    printf("\n=== 测试不同大小的单行文本（无描边） ===\n");
    for (int size = 12; size <= 36; size += 6) {
        char filename[50];
        sprintf(filename, "single_no_stroke_%d.bmp", size);
        
        if (ttf_get_bmp(&osd_bmp, "a单行测试文本", text_color, size, NULL, 0) == TD_SUCCESS) {
            if (osd_bmp.data) {
                save_bmp_file(&osd_bmp, filename);
                
                free(osd_bmp.data);
                printf("生成: %s (字号: %dpt)\n", filename, size);
            }
        }
    }
    
    // ====================
    // 2. 测试不同大小的单行文本（带描边）
    // ====================
    printf("\n=== 测试不同大小的单行文本（带描边） ===\n");
    for (int size = 12; size <= 36; size += 6) {
        char filename[50];
        sprintf(filename, "single_with_stroke_%d.bmp", size);
        
        if (ttf_get_bmp(&osd_bmp, "1单行描边测试", text_color, size, &stroke_color, 2) == TD_SUCCESS) {
            if (osd_bmp.data) {
                save_bmp_file(&osd_bmp, filename);
                free(osd_bmp.data);
                printf("生成: %s (字号: %dpt)\n", filename, size);
            }
        }
    }

    
    // ====================
    // 3. 测试不同大小的多行文本（无描边，不同对齐）
    // ====================
    printf("\n=== 测试不同大小的多行文本（无描边） ===\n");
    char *multiline_text = "a第一行文本\nbb第二行内容\nccc第三行测试";
    
    // 测试不同对齐方式
    TextAlignment alignments[] = {ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT};
    const char *align_names[] = {"left", "center", "right"};
    
    for (int size = 12; size <= 36; size += 6) {
        for (int i = 0; i < 3; i++) {
            char filename[100];
            sprintf(filename, "multiline_no_stroke_%s_%d.bmp", align_names[i], size);
            
            if (ttf_get_multiline_bmp(&osd_bmp, multiline_text, text_color, size, 
                                     NULL, 0, alignments[i], 10) == TD_SUCCESS) {
                if (osd_bmp.data) {
                    save_bmp_file(&osd_bmp, filename);
                    free(osd_bmp.data);
                    printf("生成: %s (字号: %dpt, 对齐: %s)\n", filename, size, align_names[i]);
                }
            }
        }
    }
    
    // ====================
    // 4. 测试不同大小的多行文本（带描边，不同对齐）
    // ====================
    printf("\n=== 测试不同大小的多行文本（带描边） ===\n");
    for (int size = 12; size <= 36; size += 6) {
        for (int i = 0; i < 3; i++) {
            char filename[100];
            sprintf(filename, "multiline_with_stroke_%s_%d.bmp", align_names[i], size);
            
            if (ttf_get_multiline_bmp(&osd_bmp, multiline_text, text_color, size, 
                                     &stroke_color, 2, alignments[i], 10) == TD_SUCCESS) {
                if (osd_bmp.data) {
                    save_bmp_file(&osd_bmp, filename);
                    free(osd_bmp.data);
                    printf("生成: %s (字号: %dpt, 对齐: %s)\n", filename, size, align_names[i]);
                }
            }
        }
    }
    
    // ====================
    // 5. 测试不同行间距（多行文本）
    // ====================
    printf("\n=== 测试不同行间距（多行文本） ===\n");
    int spacings[] = {5, 10, 15, 20};
    
    for (int i = 0; i < 4; i++) {
        char filename[100];
        sprintf(filename, "multiline_spacing_%d.bmp", spacings[i]);
        
        if (ttf_get_multiline_bmp(&osd_bmp, multiline_text, text_color, 24, 
                                 NULL, 0, ALIGN_CENTER, spacings[i]) == TD_SUCCESS) {
            if (osd_bmp.data) {
                save_bmp_file(&osd_bmp, filename);
                free(osd_bmp.data);
                printf("生成: %s (行间距: %dpx)\n", filename, spacings[i]);
            }
        }
    }
    
    // ====================
    // 6. 测试不同描边宽度（多行文本）
    // ====================
    printf("\n=== 测试不同描边宽度（多行文本） ===\n");
    int stroke_widths[] = {1, 2, 3, 4};
    
    for (int i = 0; i < 4; i++) {
        char filename[100];
        sprintf(filename, "multiline_stroke_width_%d.bmp", stroke_widths[i]);
        
        if (ttf_get_multiline_bmp(&osd_bmp, multiline_text, text_color, 24, 
                                 &stroke_color, stroke_widths[i], ALIGN_CENTER, 10) == TD_SUCCESS) {
            if (osd_bmp.data) {
                save_bmp_file(&osd_bmp, filename);
                free(osd_bmp.data);
                printf("生成: %s (描边宽度: %dpx)\n", filename, stroke_widths[i]);
            }
        }
    }
    
    // ====================
    // 7. 测试长文本（多行）
    // ====================
    printf("\n=== 测试长文本（多行） ===\n");
    char *long_text = "这是一个较长的多行文本测试，用于验证文本渲染系统处理多行文本的能力。\n"
                     "文本应该自动换行并保持正确的对齐方式。\n"
                     "这是第三行文本，用于测试行间距和描边效果。";

    const char* long_text_filenames[3] = {};

    // 测试不同对齐方式的长文本
    for (int i = 0; i < 3; i++) {
        char filename[100];
        sprintf(filename, "long_text_%s.bmp", align_names[i]);
        long_text_filenames[i] = filename;
        
        if (ttf_get_multiline_bmp(&osd_bmp, long_text, text_color, 18, 
                                 &stroke_color, 2, alignments[i], 8) == TD_SUCCESS) {
            if (osd_bmp.data) {
                save_bmp_file(&osd_bmp, filename);
                free(osd_bmp.data);
                printf("生成: %s (对齐: %s)\n", filename, align_names[i]);
            }
        }
    }
    bmp_concatenate_vertically(long_text_filenames, 3, ALIGN_CENTER, "long_text_concatenated.bmp");

    ot_bmpp *merged_bmp = NULL;
    if (bmp_concatenate_with_rendering(long_text_filenames, 3, ALIGN_CENTER, "long_text_concatenated_with_rendering.bmp", &merged_bmp) == TD_SUCCESS) {
        printf("带渲染的垂直拼接成功: long_text_concatenated_with_rendering.bmp\n");
        if (merged_bmp) {
            bmp_free(merged_bmp);
            merged_bmp = NULL;
        }
    } else {
        printf("带渲染的垂直拼接失败\n");
    }


    test_concatenate_font_style_bmps();

    test_font_style_consistency();
    test_minimal_case();

    test_concatenate_font_style_bmps();
    
    // 测试BMP合成功能
    //test_bmp_concatenation();
    
    // ====================
    // 8. 测试渲染参数存储和读取
    // ====================
    printf("\n=== 测试渲染参数存储和读取 ===\n");
    
    // 创建一个带描边的位图
    ot_bmpp test_bmp;
    if (ttf_get_bmp(&test_bmp, "参数测试", text_color, 24, &stroke_color, 2) == TD_SUCCESS) {
        // 保存到文件
        save_bmp_file(&test_bmp, "render_params_test.bmp");
        
        // 读取渲染参数
        SDL_Color read_text_color, read_stroke_color;
        int read_stroke_width, read_font_size, read_line_spacing;
        TextAlignment read_alignment;
        
        if (bmp_get_render_params(&test_bmp, &read_text_color, &read_stroke_color, 
                                &read_stroke_width, &read_font_size, 
                                &read_alignment, &read_line_spacing) == TD_SUCCESS) {
            printf("读取渲染参数成功:\n");
            printf("  文字颜色: (%d,%d,%d,%d)\n", 
                  read_text_color.r, read_text_color.g, read_text_color.b, read_text_color.a);
            printf("  描边颜色: (%d,%d,%d,%d), 宽度: %d\n", 
                  read_stroke_color.r, read_stroke_color.g, read_stroke_color.b, read_stroke_color.a,
                  read_stroke_width);
            printf("  字号: %d, 对齐: %d, 行距: %d\n", 
                  read_font_size, read_alignment, read_line_spacing);
        }
        
        bmp_free(&test_bmp);
    }



    // 清理资源
    SDL_Quit();
    
    printf("\n所有测试完成！共生成 %d 张测试图片\n", 
          (36-12)/6+1 +              // 单行无描边
          (36-12)/6+1 +              // 单行带描边
          ((36-12)/6+1)*3 +          // 多行无描边（不同对齐）
          ((36-12)/6+1)*3 +          // 多行带描边（不同对齐）
          4 +                        // 行间距测试
          4 +                        // 描边宽度测试
          3 +                        // 长文本测试
          1);                        // 参数测试
    
    
    return 0;
}