/*	Public domain	*/

/*
 * Implementation of a typical Agar widget which uses surface mappings to
 * efficiently draw surfaces, regardless of the underlying graphics system.
 *
 * If you are not familiar with the way the Agar object system handles
 * inheritance, see demos/objsystem.
 */

#include <agar/core.h>
#include <agar/gui.h>

#include "color_widget.h"

/*
 * This is a generic constructor function. It is completely optional, but
 * customary of FooNew() functions to allocate, initialize and attach an
 * instance of the class.
 */
AG_ColorWidget *
AG_ColorWidgetNew(void *parent, uint32_t flags)
{
	AG_ColorWidget *my;

	/* Create a new instance of the AG_ColorWidget class */
	my = malloc(sizeof(AG_ColorWidget));
	AG_ObjectInit(my, &colorWidgetClass);

	/* Attach the object to the parent (no-op if parent is NULL) */
	AG_ObjectAttach(parent, my);

	if (flags & AG_CLRWIDGET_HFILL) { AG_ExpandHoriz(my); }
	if (flags & AG_CLRWIDGET_VFILL) { AG_ExpandVert(my); }

	my->type = AG_CLRWIDGET_AG_COLOR;

	return (my);
}

AG_ColorWidget *
AG_ColorWidgetNewRGB(void *parent, uint8_t *r, uint8_t *g, uint8_t *b, uint32_t flags)
{
    AG_ColorWidget *my = AG_ColorWidgetNew(parent, flags);
    my->type = AG_CLRWIDGET_RGB;
    my->r = r;
    my->g = g;
    my->b = b;

    return (my);
}

AG_ColorWidget *
AG_ColorWidgetNewColor(void *parent, AG_Color *color, uint32_t flags)
{
    AG_ColorWidget *my = AG_ColorWidgetNew(parent, flags);
    my->type = AG_CLRWIDGET_AG_COLOR;
    my->color = color;

    return (my);
}

/*
 * This function requests a minimal geometry for displaying the widget.
 * It is expected to return the width and height in pixels into r.
 *
 * Note: Some widgets will provide FooSizeHint() functions to allow the
 * programmer to request an initial size in pixels or some other metric
 * FooSizeHint() typically sets some structure variable, which are then
 * used here.
 */
static void
SizeRequest(void *p, AG_SizeReq *r)
{
    r->w = 5;
    r->h = 5;
}

/*
 * This function is called by the parent widget after it decided how much
 * space to allocate to this widget. It is mostly useful to container
 * widgets, but other widgets generally use it to check if the allocated
 * geometry can be handled by Draw().
 */
static int
SizeAllocate(void *p, const AG_SizeAlloc *a)
{
	AG_ColorWidget *my = p;

	/* If we return -1, Draw() will not be called. */
	if (a->w < 5 || a->h < 5)
		return (-1);

	return (0);
}

/*
 * Draw function. Invoked from GUI rendering context to draw the widget
 * at its current location. All primitive and surface operations operate
 * on widget coordinates.
 */
static void
Draw(void *p)
{
	AG_ColorWidget *my = p;

    if(my->type == AG_CLRWIDGET_AG_COLOR)
    {
        AG_DrawBox(my, AG_RECT(0, 0, AGWIDGET(my)->w, AGWIDGET(my)->h), 1, *my->color);
    }
    else
    {
        AG_DrawBox(my, AG_RECT(0, 0, AGWIDGET(my)->w, AGWIDGET(my)->h), 1,
                   AG_ColorRGB(*my->r, *my->g, *my->b));
    }
}

/*
 * Initialization routine. Note that the object system will automatically
 * invoke the initialization routines of the parent classes first.
 */
static void
Init(void *obj)
{
    // nothing to do
}

/*
 * This structure describes our widget class. It inherits from AG_ObjectClass.
 * Any of the function members may be NULL. See AG_Widget(3) for details.
 */
AG_WidgetClass colorWidgetClass = {
	{
		"AG_Widget:ColorWidget",	/* Name of class */
		sizeof(AG_ColorWidget),	/* Size of structure */
		{ 0,0 },		/* Version for load/save */
		Init,			/* Initialize dataset */
		NULL,			/* Free dataset */
		NULL,			/* Destroy widget */
		NULL,			/* Load widget (for GUI builder) */
		NULL,			/* Save widget (for GUI builder) */
		NULL			/* Edit (for GUI builder) */
	},
	Draw,				/* Render widget */
	SizeRequest,			/* Default size requisition */
	SizeAllocate			/* Size allocation callback */
};
