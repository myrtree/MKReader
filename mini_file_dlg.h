/*	Public domain	*/

#ifndef _AGAR_WIDGET_MINI_FILE_DLG_H_
#define _AGAR_WIDGET_MINI_FILE_DLG_H_

#include <agar/gui/widget.h>
#include <agar/gui/button.h>
#include <agar/gui/window.h>
#include <agar/gui/tlist.h>
#include <agar/gui/pane.h>
#include <stdint.h>

#include <agar/gui/begin.h>

typedef struct simple_open_dlg
{
#define AG_SIMPLEFILEDLG_HFILL		0x01	/* Fill horizontal space */
#define AG_SIMPLEFILEDLG_VFILL		0x02	/* Fill vertical space */
#define AG_SIMPLEFILEDLG_EXPAND		(AG_SIMPLEFILEDLG_HFILL|AG_SIMPLEFILEDLG_VFILL)
    AG_Widget wid;
    char cwd[AG_PATHNAME_MAX];	 /* Current working directory */
    char cfile[AG_PATHNAME_MAX]; /* Current file path */
    AG_Pane *hPane;
    AG_Tlist *tlDirs;			 /* List of directories */
    AG_Tlist *tlFiles;			 /* List of files */
    AG_Button *btnOk;			 /* OK button */
    AG_Button *btnCancel;		 /* Cancel button */
    AG_KeySym choose_key;
} AG_SimpleFileDlg;

__BEGIN_DECLS
extern AG_WidgetClass agSimpleFileDlgClass;
AG_SimpleFileDlg *AG_SimpleFileDlgNew(void *, uint32_t, AG_KeySym);
__END_DECLS

#include <agar/gui/close.h>
#endif /* _AGAR_WIDGET_MINI_FILE_DLG_H_ */
