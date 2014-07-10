#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <agar/core.h>
#include <agar/gui.h>
#include "mini_file_dlg.h"
#include "color_widget.h"
#include "SDL.h"
#include "SDL_ttf.h"
#include "Dingoo_SDL_keymap.h"
#include "c_vector.h"
#include "codepages.h"
#include "def_code_page.h"

//#define _DINGUX 1

#ifdef _DINGUX
# include "MK_Dingux_keymap.h"
#elif _WIN32
# include "MK_Win_keymap.h"
#endif

#define screen_width  320
#define screen_height 240
static int screen_flags  = SDL_ANYFORMAT;//|SDL_FULLSCREEN;
static SDL_Surface *p_screen = NULL;
static SDL_Rect screen_rect = {0, 0, screen_width, screen_height};
static char font_name[] = "FONT.TTF";
static TTF_Font *font;
static SDL_Color font_color = {255, 255, 255};
static SDL_Color chgd_fnt_clr = {255, 255, 255};
static SDL_Color bg_color = {0, 0, 0};
static SDL_Color chgd_bg_clr = {0, 0, 0};

SDLKey key;
SDL_Event event;
int font_size = 14;

/* Back и Front применяются для обозначения направления чтения в файле.
   Back - движение в файле в сторону его начала.
   Front - в сторону конца.
   ToEnd и ToFront применяются для обозначения позиции вставки блока в ленту.
   ToEnd - вставка в конец ленты.
   ToFront - вставка в начало.                                                         */

typedef enum __TNewLineWay {CR, LF, CRLF, UNKNOWN_NL} TNewLineWay;
typedef enum __TDirection {End = 0,Back = 0,Front = 1,ToEnd = 0,ToFront = 1} TDirection;
typedef enum __TEndianType {LittleEndian, BigEndian} TEndianType;
typedef enum __TContentType {Image, Text} TContentType;

typedef struct __TTapeBlock
{
    int64_t upper_bound, lower_bound;
    uint32_t width, height;
    SDL_Surface *p_image;
    fpos_t f_upper_pos, f_lower_pos;
} TTapeBlock, *PTTapeBlock;

typedef struct __TTape
{
    int64_t screen_upper_bound;
    int64_t upper_bound, lower_bound;
    c_vector blocks;
} TTape, *PTTape;

typedef struct __TSource
{
    FILE *p_file;
    TNewLineWay new_line_way;
    TCodePage code_page;
    TEndianType source_endian, machine_endian;
} TSource, *PTSource;

PTTape p_tape = NULL;
PTSource p_source = NULL;

inline int
fbof(FILE *stream)
{
    return !ftell(stream);
}

PTTape
CreateTape()
{
    PTTape p_tape = malloc(sizeof(TTape));
    c_vector_create(&p_tape->blocks, NULL);
    c_vector_reserve(&p_tape->blocks, 20);
    p_tape->upper_bound = p_tape->lower_bound = p_tape->screen_upper_bound = 0;
    return p_tape;
}

inline void
FreeTapeBlock(PTTapeBlock p_block)
{
    if(p_block)
    {
        SDL_FreeSurface(p_block->p_image);
        free(p_block);
    }
}

void
ClearTape(PTTape p_tape)
{
    c_iterator first;

    p_tape->upper_bound = p_tape->lower_bound = p_tape->screen_upper_bound = 0;
    first = c_vector_begin(&p_tape->blocks);

    while(c_vector_size(&p_tape->blocks))
    {
        FreeTapeBlock((PTTapeBlock)ITER_REF(first));
        c_vector_erase(&p_tape->blocks, first);
    }
}

void
FreeTape(PTTape p_tape)
{
    ClearTape(p_tape);
    c_vector_destroy(&p_tape->blocks);
    free(p_tape);
}

PTTapeBlock
AddBlockToTape(PTTape p_tape, PTTapeBlock p_block, TDirection pos)
{
    if(!p_tape || !p_block || !p_block->p_image) return NULL;
    p_block->width  = p_block->p_image->w;
    p_block->height = p_block->p_image->h;

    if(c_vector_size(&p_tape->blocks))
    {
        if(pos == ToFront)
        {
            p_block->lower_bound = ((PTTapeBlock)c_vector_front(&p_tape->blocks))->upper_bound;
            p_block->upper_bound = p_block->lower_bound - p_block->height;
            c_vector_insert(&p_tape->blocks, c_vector_begin(&p_tape->blocks), p_block);
            p_tape->upper_bound = p_block->upper_bound;
        }
        else
        {
            p_block->upper_bound = ((PTTapeBlock)c_vector_back(&p_tape->blocks))->lower_bound;
            p_block->lower_bound = p_block->upper_bound + p_block->height;
            c_vector_insert(&p_tape->blocks, c_vector_end(&p_tape->blocks), p_block);
            p_tape->lower_bound = p_block->lower_bound;
        }
    }
    else
    {
        p_block->upper_bound = 0;
        p_block->lower_bound = p_block->height;
        c_vector_insert(&p_tape->blocks, c_vector_begin(&p_tape->blocks), p_block);
        p_tape->lower_bound = p_block->lower_bound;
    }
    return p_block;
}

PTTapeBlock
RemoveBlockFromTape(PTTape p_tape, TDirection pos)
{
    c_iterator first, last;

    if(c_vector_size(&p_tape->blocks))
    {
        if(pos == Front)
        {
            first = c_vector_begin(&p_tape->blocks);
            FreeTapeBlock((PTTapeBlock)ITER_REF(first));
            c_vector_erase(&p_tape->blocks, first);
            p_tape->upper_bound = ((PTTapeBlock)ITER_REF(first))->upper_bound;
            return c_vector_size(&p_tape->blocks) ? (PTTapeBlock)ITER_REF(first) : NULL;
        }
        else
        {
            last = c_vector_end(&p_tape->blocks);
            ITER_DEC(last);
            FreeTapeBlock((PTTapeBlock)ITER_REF(last));
            c_vector_erase(&p_tape->blocks, last);
            ITER_DEC(last);
            p_tape->lower_bound = ((PTTapeBlock)ITER_REF(last))->lower_bound;
            return c_vector_size(&p_tape->blocks) ? (PTTapeBlock)ITER_REF(last) : NULL;
        }
    }

    return NULL;
}

inline void
ScrollTape(PTTape p_tape, int64_t dy)
{
    p_tape->screen_upper_bound += dy;
    if(p_tape->screen_upper_bound < p_tape->upper_bound)
        p_tape->screen_upper_bound = p_tape->upper_bound;
    if(p_tape->screen_upper_bound > p_tape->lower_bound)
        p_tape->screen_upper_bound = p_tape->lower_bound;
}

inline bool
InSegment(int64_t up, int64_t down, int64_t y)
{
    return (y >= up && y <= down) ? true : false;
}

inline int
DrawTape(PTTape p_tape, SDL_Surface *p_screen)
{
    if(!c_vector_size(&p_tape->blocks)) return -1;

    c_iterator first, last, iter;
    SDL_Rect dst_rect, src_rect;
    PTTapeBlock p_block;
    first = c_vector_begin(&p_tape->blocks);
    last = c_vector_end(&p_tape->blocks);
    int64_t current_pos,screen_lower_bound=p_tape->screen_upper_bound+screen_height;

    SDL_FillRect(p_screen, &screen_rect, SDL_MapRGB(p_screen->format, bg_color.r, bg_color.g,
                 bg_color.b));
    for(iter = first; !ITER_EQUAL(iter, last); ITER_INC(iter))
    {
        assert(p_block = (PTTapeBlock)ITER_REF(iter));
        if(InSegment(p_block->upper_bound, p_block->lower_bound, p_tape->screen_upper_bound))
        {
            current_pos = 0;
            while(current_pos < screen_height &&!ITER_EQUAL(iter, last))
            {
                src_rect.x = 0;
                src_rect.w = p_block->width;
                if(p_block->upper_bound < p_tape->screen_upper_bound)
                    src_rect.y = p_tape->screen_upper_bound - p_block->upper_bound;
                else src_rect.y = 0;
                if(p_block->lower_bound > screen_lower_bound)
                    src_rect.h = screen_lower_bound - p_block->upper_bound - src_rect.y;
                else src_rect.h = p_block->height - src_rect.y;

                dst_rect = src_rect;
                dst_rect.y = current_pos;

                SDL_BlitSurface(p_block->p_image, &src_rect, p_screen, &dst_rect);

                current_pos += src_rect.h;
                ITER_INC(iter);
                p_block = (PTTapeBlock)ITER_REF(iter);
            }
            break;
        }
    }
    return 0;
}

TCodePage
FileHasBOM(FILE *stream)
{
    unsigned char BOM[4];
    fpos_t backup;
    TCodePage result = UNKNOWN_CP;

    fgetpos(stream, &backup);
    rewind(stream);
    fread(BOM, 1, 4, stream);
    if(BOM[0] == 0xFF && BOM[1] == 0xFE)
    {
        if(!BOM[2] && !BOM[3]) result = UTF32LE;
        else result = UTF16LE;
    }
    else if(!BOM[0] && !BOM[1] && BOM[2] == 0xFE && BOM[3] == 0xFF)
        result = UTF32BE;
    else if(BOM[0] == 0xFE && BOM[1] == 0xFF)
        result = UTF16BE;
    else if(BOM[0] == 0xEF && BOM[1] == 0xBB && BOM[2] == 0xBF)
        result = UTF8;
    fsetpos(stream, &backup);
    return result;
}

TEndianType
MachineEndianType()
{
    uint16_t x = 1; /* 0000 0001 */
    return *((uint8_t *) &x) == 0 ? BigEndian : LittleEndian;
}

inline int
NextChar(PTSource p_source, TUTF32Char *out, TDirection direction);

TNewLineWay
NewLineWay(PTSource p_source)
{
    fpos_t backup;
    TUTF32Char bufer_1, bufer_2;

    fgetpos(p_source->p_file, &backup);
    fseek(p_source->p_file, 0, SEEK_SET);

    while(!NextChar(p_source, &bufer_1, Front))
    {
        if(bufer_1 == 0x0D || bufer_1 == 0x0A)
        {
            NextChar(p_source, &bufer_2, Front);
            fsetpos(p_source->p_file, &backup);
            if(bufer_1 == 0x0D && bufer_2 == 0x0A)
            {
                printf("NEW LINE TYPE: CRLF\n");
                return CRLF;
            }
            if(bufer_1 == 0x0A && bufer_2 == 0x0D)
            {
                printf("NEW LINE TYPE: CRLF\n");
                return CRLF;
            }
            if(bufer_1 == 0x0D)
            {
                printf("NEW LINE TYPE: CR\n");
                return CR;
            }
            if(bufer_1 == 0x0A)
            {
                printf("NEW LINE TYPE: LF\n");
                return LF;
            }
            break;
        }
    }
    fsetpos(p_source->p_file, &backup);
    printf("NEW LINE TYPE: UNKNOWN\n");
    return UNKNOWN_NL;
}

TContentType
ReadTXTBlock(PTSource p_source, TDirection direction,
             TUTF32Char *p_bufer, TUTF32Char **p_out);

TContentType
ReadFB2Block(PTSource p_source, TDirection direction,
             TUTF32Char *p_bufer, TUTF32Char **p_out);

TContentType
(*ReadBlockFunc)(PTSource p_source, TDirection direction,
                 TUTF32Char *p_bufer, TUTF32Char **p_out);

PTSource
CreateSource(const char *file_name, TCodePage CP)
{
    FILE *_p_file = fopen(file_name, "rb");
    if(!_p_file) return NULL;
    PTSource p_source = (PTSource)malloc(sizeof(TSource));
    p_source->p_file = _p_file;
    if(strstr(file_name, ".fb2"))
    {
        ReadBlockFunc = ReadFB2Block;
        CP = CP1251;
    }
    else
    {
        ReadBlockFunc = ReadTXTBlock;
    }
    p_source->machine_endian = MachineEndianType();
    if(p_source->machine_endian == LittleEndian)
        printf("MACHINE ENDIAN: LITTLE ENDIAN\n");
    else printf("MACHINE ENDIAN: BIG ENDIAN\n");
    if(CP != UNKNOWN_CP) p_source->code_page = CP;
    else
    {
        unsigned char bufer[150];
        size_t readed = fread(bufer, 1, 150, p_source->p_file);
        rewind(p_source->p_file);
        p_source->code_page = m_def_code(bufer, readed, 255);
        switch(p_source->code_page)
        {
        case CP866:
            printf("CODEPAGE: CP866\n");
            break;
        case CP1251:
            printf("CODEPAGE: CP1251\n");
            break;
        case KOI8R:
            printf("CODEPAGE: KOI8R\n");
            break;
        case UTF8:
            printf("CODEPAGE: UTF8\n");
            break;
        case UTF16BE:
            p_source->code_page = UTF16;
            p_source->source_endian = BigEndian;
            printf("CODEPAGE: UTF16BE\n");
            break;
        case UTF16LE:
            p_source->code_page = UTF16;
            p_source->source_endian = LittleEndian;
            printf("CODEPAGE: UTF16LE\n");
            break;
        case UTF32LE:
            p_source->code_page = UTF32;
            p_source->source_endian = LittleEndian;
            printf("CODEPAGE: UTF32LE\n");
            break;
        case UTF32BE:
            p_source->code_page = UTF32;
            p_source->source_endian = BigEndian;
            printf("CODEPAGE: UTF32BE\n");
            break;
        default:
            printf("ERROR: UNKNOWN CODEPAGE\n");
            break;
        }
    }
    p_source->new_line_way = NewLineWay(p_source);
    return p_source;
}

int
FreeSource(PTSource p_source)
{
    if(!p_source) return - 1;
    int result = fclose(p_source->p_file);
    free(p_source);
    return result;
}

// А то хрен его знает какой EOF на других платформах
#define BOF EOF-1
#define EOF_FERROR EOF-2
#define FOK 0

inline int
NextCharUTF8(PTSource p_source, TUTF32Char *out, TDirection direction)
{
    if(ferror(p_source->p_file)) return EOF_FERROR;
    int result = FOK;
    if(!direction && fbof(p_source->p_file)) result = BOF;
    if(direction  && feof(p_source->p_file)) result = EOF;

    char utf8[6];
    TUTF32Char ch;

    fread(utf8, 1, 1, p_source->p_file);
    ch = *((unsigned char *)utf8);

    if(ch < 0x80)
    {
        *out = ch;
        if(!direction) fseek(p_source->p_file, result==BOF?-1:-2, SEEK_CUR);
        return result;
    }
    if(!direction && (result != BOF))
    {
        fseek(p_source->p_file, -2, SEEK_CUR);
        int i = 0;
        while(ch < 0xC0 && i < 6 && !fbof(p_source->p_file))
        {
            fread(utf8, 1, 1, p_source->p_file);
            fseek(p_source->p_file, -2, SEEK_CUR);
            ch = *((const unsigned char *)utf8);
            i++;
        }
        if(fbof(p_source->p_file)) return result;
        fseek(p_source->p_file, 2, SEEK_CUR);
    }
    if ( ch >= 0xFC )
    {
        ch  = (Uint32)(utf8[0]&0x01) << 30;
        fread(utf8, 1, 4, p_source->p_file);
        if(!direction) fseek(p_source->p_file, result==BOF?-6:-7, SEEK_CUR);
        ch |= (Uint32)(utf8[0]&0x3F) << 24;
        ch |= (Uint32)(utf8[1]&0x3F) << 18;
        ch |= (Uint32)(utf8[2]&0x3F) << 12;
        ch |= (Uint32)(utf8[3]&0x3F) << 6;
        ch |= (Uint32)(utf8[4]&0x3F);
    }
    else if (ch >= 0xF8)
    {
        ch  = (Uint32)(utf8[0]&0x03) << 24;
        fread(utf8, 1, 4, p_source->p_file);
        if(!direction) fseek(p_source->p_file, result==BOF?-5:-6, SEEK_CUR);
        ch |= (Uint32)(utf8[0]&0x3F) << 18;
        ch |= (Uint32)(utf8[1]&0x3F) << 12;
        ch |= (Uint32)(utf8[2]&0x3F) << 6;
        ch |= (Uint32)(utf8[3]&0x3F);
    }
    else if (ch >= 0xF0)
    {
        ch  = (TUTF32Char)(utf8[0]&0x07) << 18;
        fread(utf8, 1, 3, p_source->p_file);
        if(!direction) fseek(p_source->p_file, result==BOF?-4:-5, SEEK_CUR);
        ch |= (TUTF32Char)(utf8[0]&0x3F) << 12;
        ch |= (TUTF32Char)(utf8[1]&0x3F) << 6;
        ch |= (TUTF32Char)(utf8[2]&0x3F);
    }
    else if (ch >= 0xE0)
    {
        ch  = (TUTF32Char)(utf8[0]&0x0F) << 12;
        fread(utf8, 1, 2, p_source->p_file);
        if(!direction) fseek(p_source->p_file, result==BOF?-3:-4, SEEK_CUR);
        ch |= (TUTF32Char)(utf8[0]&0x3F) << 6;
        ch |= (TUTF32Char)(utf8[1]&0x3F);
    }
    else if (ch >= 0xC0)
    {
        ch  = (TUTF32Char)(utf8[0]&0x1F) << 6;
        fread(utf8, 1, 1, p_source->p_file);
        if(!direction) fseek(p_source->p_file, result==BOF?-2:-3, SEEK_CUR);
        ch |= (TUTF32Char)(utf8[0]&0x3F);
    }
    *out = ch;

    return result;
}

inline int
NextChar(PTSource p_source, TUTF32Char *out, TDirection direction)
{
    if(ferror(p_source->p_file)) return EOF_FERROR;
    int result = FOK;
    if(!direction) if(fbof(p_source->p_file)) result = BOF;
    if(direction)  if(feof(p_source->p_file)) result = EOF;

    static unsigned char bufer[4];
    bool swapped = p_source->source_endian != p_source->machine_endian;

    switch(p_source->code_page)
    {
    case UTF8:
        return NextCharUTF8(p_source, out, direction);
        break;
    case UTF16:
        if(fread(bufer, 1, 2, p_source->p_file) <= 0) result = EOF_FERROR;
        if(!direction) fseek(p_source->p_file, result==BOF?-2:-4, SEEK_CUR);
        *out = swapped ? SDL_Swap16(*((TUTF16Char*)bufer)) : *((TUTF16Char*)bufer);
        break;
    case UTF32:
        if(fread(bufer, 1, 4, p_source->p_file) <= 0) result = EOF_FERROR;
        if(!direction) fseek(p_source->p_file, result==BOF?-4:-8, SEEK_CUR);
        *out = swapped ? SDL_Swap32(*((TUTF32Char*)bufer)) : *((TUTF32Char*)bufer);
        break;
    case CP1251:
        if(fread(bufer, 1, 1, p_source->p_file) <= 0) result = EOF_FERROR;
        if(!direction) fseek(p_source->p_file, result==BOF?-1:-2, SEEK_CUR);
        *out = CP1251_UNICODE[*bufer];
        break;
    case KOI8R:
        if(fread(bufer, 1, 1, p_source->p_file) <= 0) result = EOF_FERROR;
        if(!direction) fseek(p_source->p_file, result==BOF?-1:-2, SEEK_CUR);
        *out = KOI8R_UNICODE[*bufer];
        break;
    case CP866:
        if(fread(bufer, 1, 1, p_source->p_file) <= 0) result = EOF_FERROR;
        if(!direction) fseek(p_source->p_file, result==BOF?-1:-2, SEEK_CUR);
        *out = CP866_UNICODE[*bufer];
        break;
    case MACCYR:
        if(fread(bufer, 1, 1, p_source->p_file) <= 0) result = EOF_FERROR;
        if(!direction) fseek(p_source->p_file, result==BOF?-1:-2, SEEK_CUR);
        *out = MACCYR_UNICODE[*bufer];
        break;
    }

    return result;
}

#define max_block_size 8192

/* Функция  читая  блок текста(абзац),  убирает лишние переносы каретки.  Формат новой
   строки - 0x0A. Далее последовательные вызовы функции RenderTXTLine отрисовывают его
   построчно.                                                                          */
TContentType
ReadTXTBlock(PTSource p_source, TDirection direction,
             TUTF32Char *p_bufer, TUTF32Char **p_out)
{
    TUTF32Char NL, nope;
    int i, result, nl_count = 0, from_nl = 0, offset;

    if(direction == Front)
    {
        i = 0;
        NL = (p_source->new_line_way == LF) ? 0x0A : 0x0D;
        offset = 1;
    }
    else //direction == Back
    {
        p_bufer[max_block_size - 1] = 0;
        i = max_block_size - 2;
        NL = (p_source->new_line_way == CR) ? 0x0D : 0x0A;
        offset = -1;
    }

    do
    {
        if(direction ? (i>=max_block_size-2) : (i<=0))
        {
            break;
        }
        result = NextChar(p_source, &p_bufer[i], direction);
        if(p_bufer[i] == NL)
        {
            if(p_source->new_line_way == CRLF)
                result = NextChar(p_source, &nope, direction);
            p_bufer[i] = 0x0A;
            nl_count++;
        }
        else if(nl_count)
        {
            result = NextChar(p_source, &nope, !direction);
            i-=offset;
            if(nl_count > 1)
            {
                break;
            }
            // новый абзац. В начале абзаца, бывают ещё и табы, бро.
            if(p_bufer[i+1] == 0x20)
            {
                break;
            }
            else
            {
                p_bufer[i] = 0x20;
            }                // Заголовок. Cтоит указать половину, примерного
            if(from_nl < 20) // кол-ва глифов, влезающих в строку.
            {
                p_bufer[i] = 0x0A;
            }
            nl_count = from_nl = 0;
        }
        if(result == FOK) i+=offset;
        from_nl++;
    }
    while(result == FOK);

    if(direction ? (i == 0) : (i == max_block_size - 2))
    {
        *p_out = NULL;
    }
    else
    {
        if(direction) p_bufer[i+1] = 0;
        *p_out = direction ? p_bufer : (p_bufer + i);
    }

    return Text;
}

#define max_tag_size 100

// Заглушка
TContentType
ReadFB2Block(PTSource p_source, TDirection direction,
             TUTF32Char *p_bufer, TUTF32Char **p_out)
{
    TUTF32Char nope, tag[max_tag_size];
    int i, j, result, offset;
    static TUTF32Char left_bracket  = '<';
    static TUTF32Char right_bracket = '>';
    static TUTF32Char tag_p[] = {'p', 0};
    bool done = false;

    TUTF32Char first_bracket, second_bracket;

    if(direction == Front)
    {
        i = 0;
        offset = 1;
        first_bracket = left_bracket;
        second_bracket = right_bracket;
    }
    else //direction == Back
    {
        p_bufer[max_block_size - 1] = 0;
        tag[max_tag_size - 1] = 0;
        i = max_block_size - 2;
        offset = -1;
        first_bracket = right_bracket;
        second_bracket = left_bracket;
    }

    result = FOK;
    while(result == FOK)
    {
        j = (direction == Front) ? 0 : max_tag_size - 2;
        do // <
        {
            result = NextChar(p_source, &nope, direction);
        }
        while(result == FOK && nope != first_bracket);
        do
        {
            result = NextChar(p_source, &tag[j], direction);
            if(tag[j] != second_bracket) j += offset;
            else break;
        }
        while(result == FOK); // tag>
        if(result == FOK)
        {
            if(direction) tag[j] = 0;
            else j -= offset;
            if((tag[0] == 'p' && tag[1] == 0) || (tag[j] == '/' && tag[j+1] == 'p' && tag[j+2] == 0))
            {
                do // text
                {
                    result = NextChar(p_source, &p_bufer[i], direction);
                    if(p_bufer[i] != first_bracket) i += offset;
                    else break;
                }
                while(result == FOK);
                if(!direction) i -= offset;

                if(direction ? (i == 0) : (i == max_block_size - 2))
                {
                    *p_out = NULL;
                }
                else
                {
                    if(direction) p_bufer[i] = 0;
                    *p_out = direction ? p_bufer : (p_bufer + i);
                }
                return Text;
            }
        }
    }
}

/* Функция рендерит строку до нуля или до 0x0A(LF).Возвращает в p_chars_printed кол-во
   считанных байт. Если строка NULL или первый символ в ней 0, возвращается NULL       */
SDL_Surface *
RenderTXTLine(TUTF32Char *p_str, int *p_chars_printed)
{
    if(!p_str) return NULL;
    if(!*p_str) return NULL;

    SDL_Surface *textbuf;
    TUTF32Char temp;
    static TUTF32Char empty_line[] = {0x20, 0x00};
    int i = 0, char_width, string_width = 0;

    while(p_str[i] != 0)
    {
        if(string_width >= screen_width - 10)
        {
            break;
        }
        if(p_str[i] == 0x0A)
        {
            if(!i)
            {
                //textbuf = TTF_RenderUTF32_Blended(font, empty_line, font_color);
                textbuf = TTF_RenderUTF32_Shaded(font, empty_line, font_color, bg_color);
                *p_chars_printed = 1;
                return textbuf;
            }
            else
            {
                break;
            }
        }
        TTF_GlyphMetrics(font,p_str[i],NULL,NULL,NULL,NULL,&char_width);
        string_width += char_width;
        i++;
    }
    temp = p_str[i];
    p_str[i] = 0;
    //textbuf = TTF_RenderUTF32_Blended(font, p_str, font_color);
    textbuf = TTF_RenderUTF32_Shaded(font, p_str, font_color, bg_color);
    p_str[i] = temp;
    *p_chars_printed = p_str[i] == 0x0A ? (i+1) : i;

    return textbuf;
}

PTTapeBlock
RenderTXTBlock(PTSource p_source, TDirection direction)
{
    PTTapeBlock p_block;
    static TUTF32Char block[max_block_size], *p_string;
    static SDL_Surface *p_surfaces[100];
    SDL_Rect dst_rect;
    int str_count = 0, i = 0, h;
    fpos_t f_upper_pos, f_lower_pos;

    fgetpos(p_source->p_file,direction?(&f_upper_pos):&(f_lower_pos));
    //ReadTXTBlock(p_source, direction, block, &p_string);
    ReadBlockFunc(p_source, direction, block, &p_string);
    if(!p_string) return NULL;
    if(p_string[0] == 0x0A) p_string++;
    fgetpos(p_source->p_file,direction?(&f_lower_pos):(&f_upper_pos));
    while(p_surfaces[str_count] = RenderTXTLine(p_string+=i, &i))str_count++;

    if(str_count)
    {
        p_block = malloc(sizeof(TTapeBlock));
        if(!p_block) return NULL;
        p_block->f_lower_pos = f_lower_pos;
        p_block->f_upper_pos = f_upper_pos;
        for(i = 0, h = 0; i < str_count; h += p_surfaces[i]->h, i++);
        p_block->p_image = SDL_CreateRGBSurface(0, screen_width, h,
                                                p_screen->format->BitsPerPixel,
                                                0, 0, 0, 0);
        if(!p_block->p_image) return NULL;

        dst_rect.x = dst_rect.y = 0;
        dst_rect.w = screen_width;
        dst_rect.h = h;

        SDL_FillRect(p_block->p_image, &dst_rect, SDL_MapRGB(p_screen->format,
                     bg_color.r, bg_color.g, bg_color.b));
        for(i = 0; i < str_count; i++)
        {
            SDL_BlitSurface(p_surfaces[i], NULL, p_block->p_image, &dst_rect);
            dst_rect.y += p_surfaces[i]->h;
            SDL_FreeSurface(p_surfaces[i]);
        }
        return p_block;
    }
    return NULL;
}

#define min_preload_range 3 * screen_height;

/* Планируется только прокрутка юзером вручную и переход на часть по проценту.
   - С переходом по проценту всё понятно. Перешли на позицию в файле и заполнили
   ленту вверх и вниз.
   - При перемотке подгружаются блоки текста пока расстояние от верхней границы
   экрана до краёв ленты не станет больше, чем min_preload_range.                 */
void
UpdateTape(PTTape p_tape, PTSource p_source)
{
    c_iterator first, last;
    PTTapeBlock p_block;
    int64_t screen_lower_bound, min_upper_bound, min_lower_bound;

    if(!c_vector_size(&p_tape->blocks))
    {
        p_block = AddBlockToTape(p_tape, RenderTXTBlock(p_source, Front), ToEnd);
    }

    screen_lower_bound = p_tape->screen_upper_bound + screen_height;
    min_upper_bound = p_tape->screen_upper_bound - min_preload_range;
    min_lower_bound =         screen_lower_bound + min_preload_range;

    first = c_vector_begin(&p_tape->blocks);
    if(p_tape->upper_bound > min_upper_bound) // М. б. нужно добавить блоки в верх ленты
    {
        p_block = (PTTapeBlock)ITER_REF(first);
        if(p_block)
        {
            fsetpos(p_source->p_file, &p_block->f_upper_pos);
            while(p_block&&!InSegment(p_block->upper_bound,p_block->lower_bound,min_upper_bound))
            {
                p_block = AddBlockToTape(p_tape, RenderTXTBlock(p_source, Back), ToFront);
            }
        }
    }
    else // удалить ненужные блоки вверху
    {
        p_block = (PTTapeBlock)ITER_REF(first);
        while(p_block&&!InSegment(p_block->upper_bound,p_block->lower_bound,min_upper_bound))
        {
            p_block = RemoveBlockFromTape(p_tape, Front);
        }
    }

    last = c_vector_end(&p_tape->blocks);
    ITER_DEC(last);
    if(p_tape->lower_bound < min_lower_bound) // Может стоит добавить блоки в низ ленты
    {
        p_block = (PTTapeBlock)ITER_REF(last);
        if(p_block)
        {
            fsetpos(p_source->p_file, &p_block->f_lower_pos);
            while(p_block&&!InSegment(p_block->upper_bound,p_block->lower_bound,min_lower_bound))
            {
                p_block = AddBlockToTape(p_tape, RenderTXTBlock(p_source, Front), ToEnd);
            }
        }
    }
    else
    {
        p_block = (PTTapeBlock)ITER_REF(last);
        while(p_block&&!InSegment(p_block->upper_bound,p_block->lower_bound,min_lower_bound))
        {
            p_block = RemoveBlockFromTape(p_tape, End);
        }
    }
}

void
RedrawTape(PTTape p_tape, PTSource p_source)
{
    c_iterator first, last, iter;
    first = c_vector_begin(&p_tape->blocks);
    last = c_vector_end(&p_tape->blocks);
    PTTapeBlock p_block;

    for(iter = first; !ITER_EQUAL(iter, last); ITER_INC(iter))
    {
        p_block = (PTTapeBlock)ITER_REF(iter);
        if(InSegment(p_block->upper_bound, p_block->lower_bound, p_tape->screen_upper_bound))
        {
            fsetpos(p_source->p_file, &p_block->f_upper_pos);
            ClearTape(p_tape);
            UpdateTape(p_tape, p_source);
            break;
        }
    }
}

void
SaveBookPos(PTTape p_tape, char **argv)
{
    c_iterator first, last, iter;
    first = c_vector_begin(&p_tape->blocks);
    last = c_vector_end(&p_tape->blocks);
    PTTapeBlock p_block;

    for(iter = first; !ITER_EQUAL(iter, last); ITER_INC(iter))
    {
        p_block = (PTTapeBlock)ITER_REF(iter);
        if(InSegment(p_block->upper_bound, p_block->lower_bound, p_tape->screen_upper_bound))
        {
            char bookmark[FILENAME_MAX];
            strcpy(bookmark, argv[1]);
            strcat(bookmark, ".bm");
            FILE *f_bookmark = fopen(bookmark, "wb");
            if(f_bookmark)
            {
                fpos_t pos = p_block->f_upper_pos;
                fwrite(&pos, sizeof(fpos_t), 1, f_bookmark);
                fclose(f_bookmark);
            }
            break;
        }
    }
}

void
ApplySettings()
{
    if(font_size != TTF_FontHeight(font))
    {
        TTF_SetFontKegel(&font, font_size);
        RedrawTape(p_tape, p_source);
        DrawTape(p_tape, p_screen);
        font_size = TTF_FontHeight(font);
    }
    if(chgd_fnt_clr.r != font_color.r ||
            chgd_fnt_clr.g != font_color.g ||
            chgd_fnt_clr.b != font_color.b ||
            chgd_bg_clr.r != bg_color.r ||
            chgd_bg_clr.g != bg_color.g ||
            chgd_bg_clr.b != bg_color.b)
    {
        font_color = chgd_fnt_clr;
        bg_color = chgd_bg_clr;
        RedrawTape(p_tape, p_source);
        DrawTape(p_tape, p_screen);
    }

}

int pressedKey = 0;			// Last pressed key
int curFPS = 0;				// Measured frame rate
const int nominalFPS = 42;	// Nominal frame rate

bool menu_done = false;
bool done = false;

static void
MyEventLoop(void)
{
    AG_Window *win;
    Uint32 t1, t2;
    AG_DriverEvent dev;

    t1 = AG_GetTicks();
    while(!menu_done)
    {
        t2 = AG_GetTicks();
        if(t2 - t1 >= nominalFPS)
        {
            // Case 1: Update the video display.
            AG_LockVFS(&agDrivers);
            // Render the Agar windows
            if(agDriverSw)
            {
                // With single-window drivers (e.g., sdlfb).
                AG_BeginRendering(agDriverSw);
                AG_FOREACH_WINDOW(win, agDriverSw)
                {
                    AG_ObjectLock(win);
                    AG_WindowDraw(win);
                    AG_ObjectUnlock(win);
                }
                AG_EndRendering(agDriverSw);
            }
            AG_UnlockVFS(&agDrivers);

            t1 = AG_GetTicks();
            curFPS = nominalFPS - (t1 - t2);
            if (curFPS < 1)
            {
                curFPS = 1;
            }
        }
        else if(AG_PendingEvents(NULL) > 0)
        {
            // Case 2: There are events waiting to be processed.
            do
            {
                // Retrieve the next queued event.
                if(AG_GetNextEvent(NULL, &dev) == 1)
                {
                    switch (dev.type)
                    {
                    case AG_DRIVER_MOUSE_BUTTON_DOWN:
                        printf("Click at %d,%d\n",
                               dev.data.button.x,
                               dev.data.button.y);
                        break;
                    case AG_DRIVER_KEY_DOWN:
                        pressedKey = (int)dev.data.key.ks;
                        if(pressedKey == AG_KEY_RETURN)
                        {
                            if(p_source && p_tape)
                            {
                                DrawTape(p_tape, p_screen);
                                ApplySettings();
                                menu_done = true;
                            }
                        }
                        if(pressedKey == AG_KEY_ESCAPE)
                        {
                            menu_done = true;
                            done = true;
                        }
                        break;
                    default:
                        break;
                    }
                    // Forward the event to Agar.
                    if (AG_ProcessEvent(NULL, &dev) == -1)
                        return;
                }
            }
            while(AG_PendingEvents(NULL) > 0);
        }
        else if (AG_TIMEOUTS_QUEUED())
        {
            // Case 3: There are AG_Timeout(3) callbacks to run.
            AG_ProcessTimeouts(t2);
        }
        else
        {
            // Case 4: Nothing to do, idle.
            AG_Delay(1);
        }
    }
}

AG_Window *win;
AG_Notebook *nb;
AG_NotebookTab *file_tab,*setts_tab, *bkmks_tab,
*about_tab, *help_tab, *temp_tab;
AG_SimpleFileDlg *fd;
AG_Pane *color_pane;
AG_ColorWidget *color_widget;
AG_Scrollbar *sb_color_r, *sb_color_g, *sb_color_b;

int fnt_min_size = 8, fnt_max_size = 100, fnt_size_vis = 1;
uint8_t min_clr = 0, max_clr = 255, clr_vis = 1;

/* колбэк функция, вызываемая при выборе файла в диалоге */
static void
FileSelected(AG_Event *event)
{
    if(p_tape) FreeTape(p_tape);
    if(p_source) FreeSource(p_source);

    assert(p_tape = CreateTape());
    assert(p_source = CreateSource(AG_STRING(1), UNKNOWN_CP));
    printf("FILE \"%s\" IS LOADED.\n", AG_STRING(1));
    menu_done = true;
    UpdateTape(p_tape, p_source);
    DrawTape(p_tape, p_screen);
    SDL_Flip(p_screen);
}

static void
CycleFocusF(void)
{
    AG_WindowCycleFocus(win, 0);
}

static void
CycleFocusB(void)
{
    AG_WindowCycleFocus(win, 1);
}

bool setts_is_prev_tab;

static void
ChangeFocusKeys(void)
{
    if(nb->sel_tab == setts_tab)
    {
        setts_is_prev_tab = true;
        AG_UnbindGlobalKey(MK_KEY_LEFT, AG_KEYMOD_ANY);
        AG_UnbindGlobalKey(MK_KEY_RIGHT, AG_KEYMOD_ANY);
        AG_BindGlobalKey(MK_KEY_UP, AG_KEYMOD_ANY, CycleFocusB);
        AG_BindGlobalKey(MK_KEY_DOWN, AG_KEYMOD_ANY, CycleFocusF);
    }
    else if(setts_is_prev_tab)
    {
        setts_is_prev_tab = false;
        AG_UnbindGlobalKey(MK_KEY_UP, AG_KEYMOD_ANY);
        AG_UnbindGlobalKey(MK_KEY_DOWN, AG_KEYMOD_ANY);
        AG_BindGlobalKey(MK_KEY_LEFT, AG_KEYMOD_ANY, CycleFocusB);
        AG_BindGlobalKey(MK_KEY_RIGHT, AG_KEYMOD_ANY, CycleFocusF);
    }
    if(nb->sel_tab == file_tab)
    {
        AG_WindowFocus(AG_ParentWindow(fd->tlDirs));
        AG_WidgetFocus(fd->tlDirs);
        AG_TlistSelect(fd->tlDirs, AG_TlistFirstItem(fd->tlDirs));
    }
}

typedef AG_TAILQ_HEAD(IMPOSIBURU, AG_NotebookTab) __IMPOSIBURU;

static void
PrevTab(void)
{
    if(temp_tab = (AG_NotebookTab*)AG_TAILQ_PREV(nb->sel_tab, IMPOSIBURU, tabs))
    {
        AG_NotebookSelectTab(nb, temp_tab);
        ChangeFocusKeys();
    }
}

static void
NextTab(void)
{
    if(temp_tab = (AG_NotebookTab*)AG_TAILQ_NEXT(nb->sel_tab, tabs))
    {
        AG_NotebookSelectTab(nb, temp_tab);
        ChangeFocusKeys();
    }
}

static void
CreateWindow(void)
{
    // Регистрация своих виджетов в системе
    AG_RegisterClass(&agSimpleFileDlgClass);
    AG_RegisterClass(&colorWidgetClass);

    // Виджет с закладками
    nb = AG_NotebookNew(win, AG_NOTEBOOK_EXPAND);
    file_tab = AG_NotebookAddTab(nb, "Load file", AG_BOX_VERT);
    bkmks_tab = AG_NotebookAddTab(nb, "Bookmarks", AG_BOX_VERT);
    setts_tab = AG_NotebookAddTab(nb, "Settings", AG_BOX_VERT);
    help_tab = AG_NotebookAddTab(nb, "Help", AG_BOX_VERT);
    about_tab = AG_NotebookAddTab(nb, "About", AG_BOX_VERT);

    // Диалог выбора файла
    fd = AG_SimpleFileDlgNew(file_tab, AG_SIMPLEFILEDLG_EXPAND, MK_KEY_CHOOSE);
    AG_SetEvent(fd, "simpledlg-file-selected", FileSelected, NULL);

    // Выбор размера шрифта
    AG_LabelNewPolled(setts_tab, AG_LABEL_HFILL, "font size: %d", &font_size);
    AG_ScrollbarNewInt(setts_tab, AG_SCROLLBAR_HORIZ, AG_SCROLLBAR_HFILL,
                       &font_size, &fnt_min_size, &fnt_max_size, &fnt_size_vis);

    // Выбор цвета шрифта
    AG_LabelNew(setts_tab, 0, "%s", "font color (RGB)");

    color_pane = AG_PaneNewHoriz(setts_tab, AG_PANE_HFILL | AG_PANE_UNMOVABLE);
    AG_PaneMoveDividerPct(color_pane, 80);
    AG_PaneSetDividerWidth(color_pane, 0);

    sb_color_r = AG_ScrollbarNewUint8(color_pane->div[0], AG_SCROLLBAR_HORIZ, AG_SCROLLBAR_HFILL,
                                      &chgd_fnt_clr.r, &min_clr, &max_clr, &clr_vis);
    sb_color_g = AG_ScrollbarNewUint8(color_pane->div[0], AG_SCROLLBAR_HORIZ, AG_SCROLLBAR_HFILL,
                                      &chgd_fnt_clr.g, &min_clr, &max_clr, &clr_vis);
    sb_color_b = AG_ScrollbarNewUint8(color_pane->div[0], AG_SCROLLBAR_HORIZ, AG_SCROLLBAR_HFILL,
                                      &chgd_fnt_clr.b, &min_clr, &max_clr, &clr_vis);
    AG_ScrollbarSetIntIncrement(sb_color_r, 4);
    AG_ScrollbarSetIntIncrement(sb_color_g, 4);
    AG_ScrollbarSetIntIncrement(sb_color_b, 4);

    color_widget = AG_ColorWidgetNewRGB(color_pane->div[1], &chgd_fnt_clr.r, &chgd_fnt_clr.g,
                                        &chgd_fnt_clr.b, AG_CLRWIDGET_EXPAND);

    // Выбор цвета фона
    AG_LabelNew(setts_tab, 0, "%s", "background color (RGB)");

    color_pane = AG_PaneNewHoriz(setts_tab, AG_PANE_HFILL | AG_PANE_UNMOVABLE);
    AG_PaneMoveDividerPct(color_pane, 80);
    AG_PaneSetDividerWidth(color_pane, 0);

    sb_color_r = AG_ScrollbarNewUint8(color_pane->div[0], AG_SCROLLBAR_HORIZ, AG_SCROLLBAR_HFILL,
                                      &chgd_bg_clr.r, &min_clr, &max_clr, &clr_vis);
    sb_color_g = AG_ScrollbarNewUint8(color_pane->div[0], AG_SCROLLBAR_HORIZ, AG_SCROLLBAR_HFILL,
                                      &chgd_bg_clr.g, &min_clr, &max_clr, &clr_vis);
    sb_color_b = AG_ScrollbarNewUint8(color_pane->div[0], AG_SCROLLBAR_HORIZ, AG_SCROLLBAR_HFILL,
                                      &chgd_bg_clr.b, &min_clr, &max_clr, &clr_vis);
    AG_ScrollbarSetIntIncrement(sb_color_r, 4);
    AG_ScrollbarSetIntIncrement(sb_color_g, 4);
    AG_ScrollbarSetIntIncrement(sb_color_b, 4);

    color_widget = AG_ColorWidgetNewRGB(color_pane->div[1], &chgd_bg_clr.r, &chgd_bg_clr.g,
                                        &chgd_bg_clr.b, AG_CLRWIDGET_EXPAND);

    // Закладка Help
    AG_Textbox *tb;
    FILE *help_file;
    char *help_text;
    size_t hf_size, hf_bufSize;
    tb = AG_TextboxNew(help_tab, AG_TEXTBOX_EXPAND|AG_TEXTBOX_MULTILINE, 0);
    AG_TextboxSetWordWrap(tb,1);

    help_file = fopen(".MKReader//help.txt", "r");
    fseek(help_file, 0, SEEK_END);
    hf_size = ftell(help_file);
    fseek(help_file, 0, SEEK_SET);
    hf_bufSize = hf_size + 1024;
    help_text = AG_Malloc(hf_bufSize);
    fread(help_text, hf_size, 1, help_file);
    fclose(help_file);
    help_text[hf_size] = '\0';
    AG_TextboxBindUTF8(tb, help_text+3, hf_bufSize);
    AG_TextboxSetCursorPos(tb, 1);

    // Закладка About
    AG_Surface *a_surf = AG_SurfaceFromPNG(".MKReader//about_logo.png");
    AG_Pixmap *px = AG_PixmapFromSurface(about_tab, AG_PIXMAP_RESCALE, a_surf);
    AG_PixmapSetCoords(px, 10, 10);
    AG_Label *a_labe = AG_LabelNew(about_tab, 0, "%s", "Version 1.0\n\nAuthots: Daenur & Kot\n"
                                   "If you like our program, you can donate us\n"
                                   "with webmoney R142306545048, Z311148507049.\nE-Mail: "
                                   "mkreader@gmail.com\n\nRussia, Chita 2011");
    AG_LabelSetPadding(a_labe, 10, 0, 25, 0);

    //Закладка Bookmarks
    AG_Button *bm_btn = AG_ButtonNew(bkmks_tab, 0, "Save position");
    AG_Tlist *bm_tlist = AG_TlistNew (bkmks_tab, AG_TLIST_EXPAND);

    AG_BindGlobalKey(MK_KEY_LEFT, AG_KEYMOD_ANY, CycleFocusB);
    AG_BindGlobalKey(MK_KEY_RIGHT, AG_KEYMOD_ANY, CycleFocusF);
    AG_BindGlobalKey(MK_KEY_RIGHT_TAB, AG_KEYMOD_ANY, NextTab);
    AG_BindGlobalKey(MK_KEY_LEFT_TAB, AG_KEYMOD_ANY, PrevTab);

    AG_WindowShow(win);

    AG_WindowFocus(AG_ParentWindow(fd->tlDirs));
    AG_WidgetFocus(fd->tlDirs);
    AG_TlistSelect(fd->tlDirs, AG_TlistFirstItem(fd->tlDirs));
}

int main(int argc, char **argv)
{
    printf("INITIALIZING SDL.\n");
    if((SDL_Init(SDL_INIT_VIDEO) == -1))
    {
        printf("UNABLE TO INITIALIZE SDL: %s.\n", SDL_GetError());
        return (1);
    }
    SDL_ShowCursor(0);
    printf("SDL INITIALIZED.\n");
    p_screen = SDL_SetVideoMode(screen_width, screen_height, 0, screen_flags);
    if(!p_screen)
    {
        printf("SDL_SETVIDEOMODE: %s.\n", SDL_GetError());
        SDL_Quit();
        return (1);
    }
    printf("BITS PER PIXEL: %d\n", p_screen->format->BitsPerPixel);

    printf("INITIALIZING SDL_TTF.\n");
    if(TTF_Init() == -1)
    {
        printf("UNABLE TO INITIALIZE SDL_TTF: %s.\n", TTF_GetError());
        SDL_Quit();
        return (1);
    }
    printf("SDL_TTF INITIALIZED.\n");
    font = TTF_OpenFont(font_name, font_size);
    if(!font)
    {
        printf("TTF_OPENFONT: %s.\n", TTF_GetError());
        TTF_Quit();
        SDL_Quit();
        return (1);
    }
    printf("FONT \"%s\" IS LOADED.\n", font_name);

    printf("INITIALIZING LIBAGAR.\n");
    if (AG_InitCore("MKReader", 0) == -1)
    {
        printf("UNABLE TOO INITIALIZE LIBAGAR: %s.\n", AG_GetError());
        SDL_Quit();
        return (1);
    }
    printf("LIBAGAR INITIALIZED.\n");
    AG_InitVideoSDL(p_screen, AG_VIDEO_HWSURFACE);
    win = AG_WindowNewNamedS(AG_WINDOW_NOTITLE | AG_WINDOW_NOBORDERS, "MKReader");
    AG_WindowSetGeometry(win, 0, 0, screen_width, screen_height + 1);
    AG_WindowSetCaption(win, "MKReader");
    // Создание меню
    CreateWindow();
    printf("MENU BUILDED.\n");

    // Load config
    AG_ConfigLoad();
    AG_BindInt(agConfig, "font-size", &font_size);
    AG_BindUint8(agConfig, "font-color-r", &chgd_fnt_clr.r);
    AG_BindUint8(agConfig, "font-color-g", &chgd_fnt_clr.g);
    AG_BindUint8(agConfig, "font-color-b", &chgd_fnt_clr.b);
    AG_BindUint8(agConfig, "bg-color-r", &chgd_bg_clr.r);
    AG_BindUint8(agConfig, "bg-color-g", &chgd_bg_clr.g);
    AG_BindUint8(agConfig, "bg-color-b", &chgd_bg_clr.b);
    AG_ConfigLoad();
    TTF_SetFontKegel(&font, font_size);
    font_size = TTF_FontHeight(font);
    font_color = chgd_fnt_clr;
    bg_color = chgd_bg_clr;
    printf("CONFIG IS LOADED.\n");

    // Почему то агар, при работе на дингуксе, не передаёт виджетам в фокусе событий от кнопок,
    // Пока физически не кликнешь в окно. Я честно не знаю какого хуя.
    SDL_Event event;
    event.type = SDL_MOUSEBUTTONDOWN;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = event.button.y = 50;
    SDL_PushEvent(&event);

    // Если путь к файлу передан через параметры, то откроем книгу сразу.
    if(argc > 1)
    {
        assert(p_tape = CreateTape());
        assert(p_source = CreateSource(argv[1], UNKNOWN_CP));
        printf("FILE \"%s\" IS LOADED.\n", argv[1]);

        /*char bookmark[FILENAME_MAX];
        strcpy(bookmark, argv[1]);
        strcat(bookmark, ".bm");
        FILE *f_bookmark = fopen(bookmark, "rb");
        if(f_bookmark)
        {
            fpos_t pos;
            fread(&pos, sizeof(fpos_t), 1, f_bookmark);
            fsetpos(p_source->p_file, &pos);
            fclose(f_bookmark);
        }*/

        UpdateTape(p_tape, p_source);
        DrawTape(p_tape, p_screen);

        AG_DriverEvent dev;
        if(AG_GetNextEvent(NULL, &dev) == 1)
            AG_ProcessEvent(NULL, &dev);
    }
    else
    {
        // Запуск цикла обработки событий
        MyEventLoop();
    }

    SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);
    while(!done && SDL_WaitEvent(&event) != -1)
    {
        switch(event.type)
        {
        case SDL_KEYDOWN:
            key = event.key.keysym.sym;
            switch(key)
            {
            case DINGOO_BUTTON_L:
                //printf("LEFT SHIFT.\n");
                break;
            case DINGOO_BUTTON_R:
                //printf("RIGHT SHIFT.\n");
                break;
            case DINGOO_BUTTON_LEFT:
                ScrollTape(p_tape, -screen_height);
                DrawTape(p_tape, p_screen);
                UpdateTape(p_tape, p_source);
                //printf("TO THE LEFT.\n");
                break;
            case DINGOO_BUTTON_RIGHT:
                ScrollTape(p_tape, screen_height);
                DrawTape(p_tape, p_screen);
                UpdateTape(p_tape, p_source);
                //printf("TO THE RIGHT.\n");
                break;
            case DINGOO_BUTTON_UP:
                ScrollTape(p_tape, -1);
                DrawTape(p_tape, p_screen);
                UpdateTape(p_tape, p_source);
                //printf("TO THE UP.\n");
                break;
            case DINGOO_BUTTON_DOWN:
                ScrollTape(p_tape, 1);
                DrawTape(p_tape, p_screen);
                UpdateTape(p_tape, p_source);
                //printf("TO THE DOWN.\n");
                break;
            case DINGOO_BUTTON_SELECT:
                //printf("SELECT.\n");
                done = true;
                break;
            case DINGOO_BUTTON_START:
                menu_done = false;
                MyEventLoop();
                //printf("START.\n");
                break;
            }
        case SDL_VIDEOEXPOSE:
            SDL_Flip(p_screen);
            break;
        case SDL_QUIT:
            done = true;
            break;
        default:
            break;
        }
        SDL_UpdateRect(p_screen, 0, 0, screen_width, screen_height);
    }

    SDL_FreeSurface(p_screen);

    //SaveBookPos(p_tape, argv);
    if(p_tape)
    {
        ClearTape(p_tape);
        FreeTape(p_tape);
    }
    if(p_source) FreeSource(p_source);

    AG_ConfigSave();

    printf("QUITING SDL_TTF.\n");
    TTF_Quit();
    printf("QUITING SDL.\n");
    SDL_Quit();
    printf("QUITING LIBAGAR.\n");
    AG_Destroy();
    printf("ALL OK.\n");

    return 0;
}
