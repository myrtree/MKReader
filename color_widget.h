#include <stdint.h>

enum ag_clrwidget_type {
	AG_CLRWIDGET_RGB,
	AG_CLRWIDGET_AG_COLOR
};

/* Structure describing an instance of the MyWidget class. */
typedef struct color_widget {
#define AG_CLRWIDGET_HFILL		0x01	/* Fill horizontal space */
#define AG_CLRWIDGET_VFILL		0x02	/* Fill vertical space */
#define AG_CLRWIDGET_EXPAND		(AG_CLRWIDGET_HFILL|AG_CLRWIDGET_VFILL)
    enum ag_clrwidget_type type;
    AG_Color *color;
    uint8_t *r;
    uint8_t *g;
    uint8_t *b;
	struct ag_widget _inherit;	/* Inherit from AG_Widget */
} AG_ColorWidget;

extern AG_WidgetClass colorWidgetClass;
AG_ColorWidget *AG_ColorWidgetNew(void *, uint32_t);
AG_ColorWidget *AG_ColorWidgetNewRGB(void *, uint8_t *, uint8_t *, uint8_t *, uint32_t);
AG_ColorWidget *AG_ColorWidgetNewColor(void *, AG_Color *, uint32_t);
