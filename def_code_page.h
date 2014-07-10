/* ==========================================================================
   Файл: def_code_page.h
   Компилятор: Turbo C 2.0
   Описание: библиотека для автоматического определения кодировки текста
             (ALT, WIN, KOI). Функция m_def_code - для случая, когда текст
             в памяти, функция f_def_code - когда текст в файле.
   Описание алгоритма: http://ivr.webzone.ru/articles/defcod_2/
   (c) Иван Рощин, Москва, 2004.
 ========================================================================= */

typedef enum __TCodePage {UNKNOWN_CP, CP866, CP1251, KOI8R, MACCYR, UTF8, UTF16,
                          UTF16LE,  UTF16BE, UTF32, UTF32LE, UTF32BE} TCodePage;

TCodePage m_def_code(unsigned char *p, int len, int n);
