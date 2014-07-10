/* ==========================================================================
   ����: def_code_page.h
   ����������: Turbo C 2.0
   ��������: ���������� ��� ��������������� ����������� ��������� ������
             (ALT, WIN, KOI). ������� m_def_code - ��� ������, ����� �����
             � ������, ������� f_def_code - ����� ����� � �����.
   �������� ���������: http://ivr.webzone.ru/articles/defcod_2/
   (c) ���� �����, ������, 2004.
 ========================================================================= */

typedef enum __TCodePage {UNKNOWN_CP, CP866, CP1251, KOI8R, MACCYR, UTF8, UTF16,
                          UTF16LE,  UTF16BE, UTF32, UTF32LE, UTF32BE} TCodePage;

TCodePage m_def_code(unsigned char *p, int len, int n);
