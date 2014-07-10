#include <agar/core.h>
#include <agar/gui.h>

#include "mini_file_dlg.h"

#include <stdarg.h>
#include <string.h>
#include <ctype.h>

#ifdef _XBOX
# include <core/xbox.h>
#elif _WIN32
# include <core/win32.h>
#else
# include <sys/types.h>
# include <sys/stat.h>
# include <unistd.h>
# include <string.h>
# include <errno.h>
#endif

#include "agar/gui/file_dlg_common.h"

AG_SimpleFileDlg *
AG_SimpleFileDlgNew(void *parent, uint32_t flags, AG_KeySym choose_key)
{
    AG_SimpleFileDlg *fd;

    fd = malloc(sizeof(AG_SimpleFileDlg));
    AG_ObjectInit(fd, &agSimpleFileDlgClass);
    fd->choose_key = choose_key;

	if (flags & AG_SIMPLEFILEDLG_HFILL) { AG_ExpandHoriz(fd); }
	if (flags & AG_SIMPLEFILEDLG_VFILL) { AG_ExpandVert(fd); }

    AG_ObjectAttach(parent, fd);

    return (fd);
}

/* Update the file / directory listing */
static void
RefreshListing(AG_SimpleFileDlg *fd)
{
	AG_TlistItem *it;
	AG_FileInfo info;
	AG_Dir *dir;
	char **dirs, **files;
	size_t i, ndirs = 0, nfiles = 0;

	if ((dir = AG_OpenDir(fd->cwd)) == NULL) {
		AG_TextMsg(AG_MSG_ERROR, "%s: %s", fd->cwd, AG_GetError());
		return;
	}

	dirs = AG_Malloc(sizeof(char *));
	files = AG_Malloc(sizeof(char *));

	AG_ObjectLock(fd->tlDirs);
	AG_ObjectLock(fd->tlFiles);

	for (i = 0; i < dir->nents; i++) {
		char path[AG_FILENAME_MAX];

		AG_Strlcpy(path, fd->cwd, sizeof(path));
		if(path[strlen(path) - 1] != AG_PATHSEPCHAR) {
			AG_Strlcat(path, AG_PATHSEP, sizeof(path));
		}
		AG_Strlcat(path, dir->ents[i], sizeof(path));

		if (AG_PathIsFilesystemRoot(fd->cwd) &&
		    strcmp(dir->ents[i], "..")==0) {
			continue;
		}
		if (AG_GetFileInfo(path, &info) == -1) {
			continue;
		}
		/* XXX TODO: check for symlinks to directories */
		if (info.type == AG_FILE_DIRECTORY) {
			dirs = AG_Realloc(dirs, (ndirs + 1) * sizeof(char *));
			dirs[ndirs++] = AG_Strdup(dir->ents[i]);
		} else {
			files = AG_Realloc(files, (nfiles + 1) * sizeof(char *));
			files[nfiles++] = AG_Strdup(dir->ents[i]);
		}
	}
	qsort(dirs, ndirs, sizeof(char *), AG_FilenameCompare);
	qsort(files, nfiles, sizeof(char *), AG_FilenameCompare);

	AG_TlistClear(fd->tlDirs);
	AG_TlistClear(fd->tlFiles);
	for (i = 0; i < ndirs; i++) {
		it = AG_TlistAddS(fd->tlDirs, agIconDirectory.s, dirs[i]);
		it->cat = "dir";
		it->p1 = it;
		AG_Free(dirs[i]);
	}
	for (i = 0; i < nfiles; i++) {
		it = AG_TlistAddS(fd->tlFiles, agIconDoc.s, files[i]);
		it->cat = "file";
		it->p1 = it;
		AG_Free(files[i]);
	}
	AG_Free(dirs);
	AG_Free(files);
	AG_TlistRestore(fd->tlDirs);
	AG_TlistRestore(fd->tlFiles);

	AG_ObjectUnlock(fd->tlFiles);
	AG_ObjectUnlock(fd->tlDirs);
	AG_CloseDir(dir);
}

/* Move to the specified directory (C string). */
int
AG_SimpleFileDlgSetDirectoryS(AG_SimpleFileDlg *fd, const char *dir)
{
	AG_FileInfo info;
	char ncwd[AG_PATHNAME_MAX], *c;

	AG_ObjectLock(fd);

	if (dir[0] == '.' && dir[1] == '\0') {
		AG_Strlcpy(ncwd, fd->cwd, sizeof(ncwd));
	} else if (dir[0] == '.' && dir[1] == '.' && dir[2] == '\0') {
		if (!AG_PathIsFilesystemRoot(fd->cwd)) {
			AG_Strlcpy(ncwd, fd->cwd, sizeof(ncwd));
			if ((c = strrchr(ncwd, AG_PATHSEPCHAR)) != NULL) {
				*c = '\0';
			}
			if (c == &ncwd[0]) {
				ncwd[0] = AG_PATHSEPCHAR;
				ncwd[1] = '\0';
			}
#ifdef _XBOX
			if (AG_PathIsFilesystemRoot(ncwd) &&
			    ncwd[2] != AG_PATHSEPCHAR) {
				AG_Strlcat(ncwd, AG_PATHSEP, sizeof(ncwd));
			}
#endif
		}
	} else if (!AG_PathIsAbsolute(dir)) {
		AG_Strlcpy(ncwd, fd->cwd, sizeof(ncwd));
		if (!(ncwd[0] == AG_PATHSEPCHAR &&
		      ncwd[1] == '\0') &&
			  ncwd[strlen(ncwd) - 1] != AG_PATHSEPCHAR) {
			AG_Strlcat(ncwd, AG_PATHSEP, sizeof(ncwd));
		}
		AG_Strlcat(ncwd, dir, sizeof(ncwd));
	} else {
		AG_Strlcpy(ncwd, dir, sizeof(ncwd));
	}

	if (AG_GetFileInfo(ncwd, &info) == -1) {
		goto fail;
	}
	if (info.type != AG_FILE_DIRECTORY) {
		AG_SetError("%s: Not a directory", ncwd);
		goto fail;
	}
	if ((info.perms & (AG_FILE_READABLE|AG_FILE_EXECUTABLE)) == 0) {
		AG_SetError("%s: Permission denied", ncwd);
		goto fail;
	}
	if (AG_Strlcpy(fd->cwd, ncwd, sizeof(fd->cwd)) >= sizeof(fd->cwd)) {
		AG_SetError("Path is too long: `%s'", ncwd);
		goto fail;
	}

	AG_TlistScrollToStart(fd->tlDirs);
	AG_TlistScrollToStart(fd->tlFiles);

	AG_ObjectUnlock(fd);
	return (0);
fail:
	AG_ObjectUnlock(fd);
	return (-1);
}

static void
SetFilename(AG_SimpleFileDlg *fd, const char *file)
{
	if (file[0] == AG_PATHSEPCHAR) {
		AG_Strlcpy(fd->cfile, file, sizeof(fd->cfile));
	} else {
		AG_Strlcpy(fd->cfile, fd->cwd, sizeof(fd->cfile));
		if (!AG_PathIsFilesystemRoot(fd->cwd) &&
		    (fd->cfile[0] != '\0' &&
		     fd->cfile[strlen(fd->cfile)-1] != AG_PATHSEPCHAR)) {
			AG_Strlcat(fd->cfile, AG_PATHSEP, sizeof(fd->cfile));
		}
		AG_Strlcat(fd->cfile, file, sizeof(fd->cfile));
	}
}

static void
FileDblClicked(AG_Event *event)
{
    AG_Tlist *tl = AG_SELF();
    AG_SimpleFileDlg *fd = AG_PTR(1);
    AG_TlistItem *itFile;

    AG_ObjectLock(fd);
	AG_ObjectLock(tl);
    if ((itFile = AG_TlistSelectedItem(tl)) != NULL) {
        SetFilename(fd, itFile->text);
        AG_PostEvent(NULL, fd, "simpledlg-file-selected", "%s", fd->cfile);
    }
    AG_ObjectUnlock(tl);
	AG_ObjectUnlock(fd);
}

static void
FileSelectedByKey(AG_Event *event)
{
    AG_Tlist *tl = AG_SELF();
    int keysym = AG_INT(1);
    AG_SimpleFileDlg *fd = AG_ObjectParent(AG_ObjectParent(AG_ObjectParent(tl))); // lol
    AG_TlistItem *itFile;

    if(keysym == fd->choose_key)
    {
        AG_ObjectLock(fd);
        AG_ObjectLock(tl);
        if ((itFile = AG_TlistSelectedItem(tl)) != NULL) {
            SetFilename(fd, itFile->text);
            AG_PostEvent(NULL, fd, "simpledlg-file-selected", "%s", fd->cfile);
        }
        AG_ObjectUnlock(tl);
        AG_ObjectUnlock(fd);
    }
}

static void
DirSelected(AG_Event *event)
{
    AG_Tlist *tl = AG_SELF();
	AG_SimpleFileDlg *fd = AG_PTR(1);
	AG_TlistItem *ti;

	AG_ObjectLock(fd);
	AG_ObjectLock(tl);
	if ((ti = AG_TlistSelectedItem(tl)) != NULL) {
		if (AG_SimpleFileDlgSetDirectoryS((AG_SimpleFileDlg*)fd, ti->text) == -1) {
			/* AG_TextMsgFromError() */
		} else {
			RefreshListing(fd);
		}
	}
	AG_ObjectUnlock(tl);
	AG_ObjectUnlock(fd);
}

static void
DirSelectedByKey(AG_Event *event)
{
    AG_Tlist *tl = AG_SELF();
	int keysym = AG_INT(1);
	AG_SimpleFileDlg *fd = AG_ObjectParent(AG_ObjectParent(AG_ObjectParent(tl))); // lol
	AG_TlistItem *ti;

    if(keysym == fd->choose_key)
    {
        AG_ObjectLock(fd);
        AG_ObjectLock(tl);
        if ((ti = AG_TlistSelectedItem(tl)) != NULL) {
            if (AG_SimpleFileDlgSetDirectoryS((AG_SimpleFileDlg*)fd, ti->text) == -1) {
                /* AG_TextMsgFromError() */
            } else {
                RefreshListing(fd);
            }
        }
        AG_ObjectUnlock(tl);
        AG_ObjectUnlock(fd);
    }
}

static void
PressedOK(AG_Event *event)
{
    // nothing
}

static void
PressedCancel(AG_Event *event)
{
    // nothing
}

static void
Shown(AG_Event *event)
{
    AG_SimpleFileDlg *fd = AG_SELF();

    RefreshListing(fd);
}

static void
Init(void *obj)
{
    AG_SimpleFileDlg *fd = obj;

    fd->cfile[0] = '\0';
    (void)AG_GetCWD(fd->cwd, sizeof(fd->cwd)); // save path into settings

    fd->hPane = AG_PaneNewHoriz(fd, AG_PANE_EXPAND | AG_PANE_UNMOVABLE);
	AG_PaneMoveDividerPct(fd->hPane, 50);
	AG_PaneResizeAction(fd->hPane, AG_PANE_DIVIDE_EVEN);

    fd->tlDirs  = AG_TlistNew(fd->hPane->div[0], AG_TLIST_EXPAND);
    AG_TlistSizeHint(fd->tlDirs, "XXXXXXXXXXXXXX", 8);
    fd->tlFiles = AG_TlistNew(fd->hPane->div[1], AG_TLIST_EXPAND);
    AG_TlistSizeHint(fd->tlFiles, "XXXXXXXXXXXXXXXXXX", 8);

    fd->btnOk = AG_ButtonNewS(fd, 0, "OK");
    fd->btnCancel = AG_ButtonNewS(fd, 0, "Cancel");
    AG_ButtonSetFocusable(fd->btnOk, 0);
    AG_ButtonSetFocusable(fd->btnCancel, 0);

    AG_AddEvent(fd, "widget-shown", Shown, NULL);
    AG_SetEvent(fd->tlDirs, "tlist-dblclick", DirSelected, "%p", fd);
    AG_AddEvent(fd->tlDirs, "key-down", DirSelectedByKey, NULL);
    AG_SetEvent(fd->tlFiles, "tlist-dblclick", FileDblClicked, "%p", fd);
    AG_AddEvent(fd->tlFiles, "key-down", FileSelectedByKey, NULL);
    AG_SetEvent(fd->btnOk, "button-pushed", PressedOK, "%p", fd);
    AG_SetEvent(fd->btnCancel, "button-pushed", PressedCancel, "%p", fd);
}

static void
Draw(void *obj)
{
    AG_Widget *chld;

    AGWIDGET_FOREACH_CHILD(chld, obj)
        AG_WidgetDraw(chld);
}

#define MAX(h,i) ((h) > (i) ? (h) : (i))

static void
SizeRequest(void *obj, AG_SizeReq *r)
{
	AG_SimpleFileDlg *fd = obj;
	AG_SizeReq rChld, rOk, rCancel;

	AG_WidgetSizeReq(fd->hPane, &rChld);
	r->w = rChld.w;
	r->h = rChld.h+2;
	AG_WidgetSizeReq(fd->btnOk, &rOk);
	AG_WidgetSizeReq(fd->btnCancel, &rCancel);
	r->h += MAX(rOk.h,rCancel.h)+2;
}

static int
SizeAllocate(void *obj, const AG_SizeAlloc *a)
{
	AG_SimpleFileDlg *fd = obj;
	AG_SizeReq r;
	AG_SizeAlloc aChld;
	int hBtn = 0, wBtn = a->w/2;

	AG_WidgetSizeReq(fd->btnOk, &r);
	hBtn = MAX(hBtn, r.h);
	AG_WidgetSizeReq(fd->btnCancel, &r);
	hBtn = MAX(hBtn, r.h);

	/* Size horizontal pane */
	aChld.x = 0;
	aChld.y = 0;
	aChld.w = a->w;
	aChld.h = a->h - hBtn - 10;
	AG_WidgetSizeAlloc(fd->hPane, &aChld);
	aChld.y += aChld.h+4;

	/* Size buttons */
	aChld.w = wBtn;
	aChld.h = hBtn;
	AG_WidgetSizeAlloc(fd->btnOk, &aChld);
	aChld.x = wBtn;
	if (wBtn*2 < a->w) { aChld.w++; }
	aChld.h = hBtn;
	AG_WidgetSizeAlloc(fd->btnCancel, &aChld);

	return (0);
}

AG_WidgetClass agSimpleFileDlgClass =
{
    {
        "AG_Widget:SimpleFileDlg",  /* Name of class */
        sizeof(AG_SimpleFileDlg),   /* Size of structure */
        { 0,0 },                        /* Version for load/save */
        Init,                           /* Initialize dataset */
        NULL,                           /* Reinit before load */
        NULL,                           /* Release resources */
        NULL,                           /* Load widget (for GUI builder) */
        NULL,                           /* Save widget (for GUI builder) */
        NULL                            /* Edit (for GUI builder) */
    },
    Draw,                               /* Render widget */
    SizeRequest,                        /* Default size requisition */
    SizeAllocate                        /* Size allocation callback */
};
