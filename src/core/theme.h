/* theme.h - every UI color in one place (0xRRGGBBAA). Change freely. */
#ifndef SF_THEME_H
#define SF_THEME_H

/* Background: near-black with faint dark-gray drifting shapes */
#define TH_BG_TOP          0x0E0E11FFu
#define TH_BG_BOTTOM       0x08080AFFu
#define TH_BG_SHAPE        0x2C2C34FFu   /* blob color; alpha set per blob (low) */
#define TH_BG_SHAPE_ALPHA  0.30f

/* Text */
#define TH_TEXT            0xFFFFFFFFu
#define TH_TEXT_DIM        0xFFFFFFB8u
#define TH_TEXT_FAINT      0xFFFFFF80u
#define TH_TEXT_ON_WHITE   0x0B0B0DFFu   /* text on the active (white) tab */

/* Tabs */
#define TH_TAB_ACTIVE      0xFFFFFFFFu
#define TH_TAB_INACTIVE    0xFFFFFF1Fu   /* ~12% white */
#define TH_TAB_SMALL       0xFFFFFF14u   /* L2 / R2 pills */
#define TH_DEMO_CHIP       0xFFFFFF14u

/* Chips, control bar, toast */
#define TH_CHIP            0xFFFFFF21u   /* ~13% white */
#define TH_BAR             0xFFFFFF1Cu   /* ~11% white */
#define TH_KEYCAP_OUTLINE  0xFFFFFFA0u

/* Panels */
#define TH_PANEL           0x18181CD9u   /* #18181c at ~85% */
#define TH_PANEL_OUTLINE   0xFFFFFF40u   /* white ~25% */
#define TH_ROW             0xFFFFFF0Du   /* resting row ~5% */
#define TH_ROW_HI          0xFFFFFF2Eu   /* highlighted row ~18% */
#define TH_SCROLL_TRACK    0xFFFFFF1Au
#define TH_SCROLL_THUMB    0xFFFFFF80u
#define TH_SLIDER_TRACK    0xFFFFFF33u
#define TH_SLIDER_FILL     0xFFFFFFE6u
#define TH_DIM_OVERLAY     0x000000FFu   /* alpha set by panel animation */
#define TH_DIM_MAX         0.55f

/* Cover flow */
#define TH_SEL_FRAME       0xFFFFFF1Fu   /* highlight behind the selected cover ~12% */
#define TH_SEL_GLOW        0xFFFFFF10u
#define TH_CASE            0x1C1C21FFu   /* game case body */
#define TH_CASE_HEADER     0x050506FFu   /* platform header strip */
#define TH_CASE_JEWEL      0x8A8F99FFu   /* PS1 jewel-case clear edge (dimmed per alpha) */

/* Exit / overlays */
#define TH_EXIT_RING       0xFFFFFFFFu
#define TH_FADE            0x000000FFu
#define TH_LETTER_BOX      0x18181CD9u
#define TH_TOAST           0x18181CE6u

/* Loading screen */
#define TH_WORDMARK        0xFFFFFFFFu
#define TH_WORDMARK_GLOW   0xD8DCE6FFu   /* soft ghostly glow, alpha set in code */
#define TH_PROGRESS_TRACK  0xFFFFFF1Fu
#define TH_PROGRESS_FILL   0xFFFFFFD0u
#define TH_LOAD_CARD       0xFFFFFF26u

/* PlayStation button identifier colors (kept colored on purpose) */
#define TH_BTN_CROSS       0x7CB2FFFFu
#define TH_BTN_TRIANGLE    0x5FE0B0FFu
#define TH_BTN_CIRCLE      0xFF6B7AFFu
#define TH_BTN_SQUARE      0xF59AD8FFu

#endif
