#ifdef HAVE_X11
#define gnu_width 50
#define gnu_height 50
static char gnu_bits[] = {
   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xef, 0xff, 0xff, 0xff, 0xff, 0xff,
   0xff, 0x9f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x1f, 0xfe, 0xff, 0xff,
   0xff, 0xff, 0xff, 0x7f, 0xfc, 0xff, 0xff, 0xf7, 0xff, 0xff, 0xff, 0xf1,
   0xff, 0xff, 0xf3, 0xff, 0x8f, 0xff, 0xe1, 0xff, 0xff, 0xf9, 0x3f, 0x22,
   0xfe, 0xcb, 0xff, 0xff, 0xf8, 0xc3, 0xf8, 0xfc, 0xcb, 0xff, 0x7f, 0xfc,
   0xe0, 0xf9, 0xf9, 0xdb, 0xff, 0x7f, 0xfc, 0xf0, 0xfb, 0xf3, 0xd9, 0xff,
   0x3f, 0x7e, 0xf8, 0xff, 0xf7, 0xcc, 0xff, 0x9f, 0x3e, 0x1c, 0x7f, 0x44,
   0xce, 0xff, 0xcf, 0x1e, 0xcc, 0x01, 0x00, 0xe7, 0xff, 0xef, 0x0e, 0xce,
   0x38, 0x1c, 0xe0, 0xff, 0xef, 0x0e, 0x27, 0xfe, 0xfa, 0xc3, 0xff, 0xef,
   0x7c, 0x93, 0xff, 0xe5, 0xbf, 0xff, 0xef, 0x99, 0xc9, 0xab, 0x2a, 0x00,
   0xff, 0xcf, 0xc3, 0x24, 0x54, 0xc5, 0xd5, 0xff, 0x9f, 0x7f, 0x16, 0xab,
   0xca, 0xff, 0xff, 0x1f, 0x1f, 0x93, 0x46, 0x95, 0xff, 0xff, 0x7f, 0xc8,
   0x49, 0x99, 0x8a, 0xff, 0xff, 0xff, 0xf0, 0x49, 0x4b, 0x95, 0xff, 0xff,
   0xff, 0xf9, 0x4c, 0x88, 0x8a, 0xff, 0xff, 0xff, 0x1e, 0xe6, 0x58, 0x95,
   0xff, 0xff, 0x3f, 0x00, 0xe6, 0xb7, 0x0a, 0xff, 0xff, 0xbf, 0x8a, 0xea,
   0x50, 0x15, 0xff, 0xff, 0xff, 0x8f, 0xca, 0x99, 0x2a, 0xff, 0xff, 0xff,
   0xa7, 0x95, 0x7f, 0x15, 0xff, 0xff, 0xff, 0x23, 0x55, 0x7f, 0x2a, 0xfe,
   0xff, 0xff, 0x63, 0xd8, 0xfc, 0x14, 0xfe, 0xff, 0xff, 0x43, 0x9a, 0xfb,
   0x2b, 0xfe, 0xff, 0xff, 0xc3, 0xaa, 0x12, 0x94, 0xfc, 0xff, 0xff, 0xc1,
   0x32, 0xd5, 0xc1, 0xfd, 0xff, 0xff, 0x81, 0x46, 0xd5, 0x47, 0xfc, 0xff,
   0xff, 0x83, 0x6c, 0xc2, 0x6e, 0xfc, 0xff, 0xff, 0x83, 0x89, 0x88, 0x69,
   0xfe, 0xff, 0xff, 0x07, 0x92, 0x09, 0x3b, 0xfe, 0xff, 0xff, 0x07, 0x22,
   0x01, 0x3c, 0xfe, 0xff, 0xff, 0x0f, 0x4e, 0x02, 0x03, 0xfe, 0xff, 0xff,
   0x2f, 0xd0, 0x18, 0x3e, 0xff, 0xff, 0xff, 0x3f, 0xb0, 0x19, 0x9e, 0xff,
   0xff, 0xff, 0x7f, 0x00, 0x09, 0x80, 0xff, 0xff, 0xff, 0x7f, 0x01, 0xe3,
   0xc1, 0xff, 0xff, 0xff, 0xff, 0x05, 0xe0, 0xff, 0xff, 0xff, 0xff, 0xff,
   0x07, 0xf0, 0xff, 0xff, 0xff, 0xff, 0xff, 0x5f, 0xfd, 0xff, 0xff, 0xff,
   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
   0xff, 0xff};
#else /* X10 */
#define sink_width 48
#define sink_height 48
#define sink_mask_width 48
#define sink_mask_height 48
short sink_bits[] = {
   0xffff, 0xffff, 0xffff, 0xffff,
   0xffff, 0x9f80, 0xffff, 0xffff,
   0x9f9f, 0xffff, 0xffff, 0x8000,
   0xffff, 0x7fff, 0xbffe, 0xffff,
   0x7fff, 0xa003, 0xffff, 0x7fff,
   0xaffd, 0xffff, 0x3fff, 0xaff9,
   0xffff, 0xffff, 0xafff, 0xffff,
   0xffff, 0xaffc, 0xffff, 0x7fff,
   0xaff8, 0xffff, 0xffff, 0xaffc,
   0xffff, 0xffff, 0xafff, 0xffff,
   0xbfff, 0xaff7, 0xffff, 0x3fff,
   0xaff3, 0xffff, 0xffff, 0xaffc,
   0x003f, 0x0000, 0x2000, 0x007f,
   0x0000, 0xe000, 0xf8df, 0xffff,
   0x07ff, 0xf9cf, 0xff0f, 0xe7ff,
   0xf9cf, 0xfff7, 0xe7ff, 0xf9ff,
   0x63f7, 0xe7fb, 0xf9ff, 0x5a37,
   0xe7fb, 0xf9cf, 0x5af7, 0xe7fb,
   0xf9cf, 0x5af7, 0xe7f9, 0xf9ef,
   0xdb0f, 0xe7fa, 0xf9ff, 0xffff,
   0xe7ff, 0xf9df, 0xffff, 0xe7ff,
   0x19cf, 0xfffc, 0xe7ff, 0xd9cf,
   0xffff, 0xe7ff, 0xd9ff, 0xce47,
   0xe673, 0x19ff, 0xb5b6, 0xe7ad,
   0xd9cf, 0xb5b7, 0xe67d, 0xd9c7,
   0xb5b7, 0xe5ed, 0x19ef, 0x4db4,
   0xe673, 0xf1ff, 0xffff, 0xe3ff,
   0x03ff, 0x0380, 0xf000, 0x07ef,
   0x0100, 0xf800, 0xffc7, 0xf93f,
   0xffff, 0xffe7, 0xfd7f, 0xffe0,
   0xffff, 0x7d7f, 0xffdf, 0xffff,
   0xbd7f, 0xffb1, 0xffff, 0xbb7f,
   0xffae, 0xffef, 0xdaff, 0xffae,
   0xffc7, 0x66ff, 0xffaf, 0xffe7,
   0xbdff, 0xffaf, 0xffff, 0xc3ff,
   0xffaf, 0xffff, 0xffff, 0xffaf};
short sink_mask_bits[] = {
   0xffff, 0xffff, 0xffff, 0xffff,
   0xffff, 0xffff, 0xffff, 0xffff,
   0xffff, 0xffff, 0xffff, 0xffff,
   0xffff, 0xffff, 0xffff, 0xffff,
   0xffff, 0xffff, 0xffff, 0xffff,
   0xffff, 0xffff, 0xffff, 0xffff,
   0xffff, 0xffff, 0xffff, 0xffff,
   0xffff, 0xffff, 0xffff, 0xffff,
   0xffff, 0xffff, 0xffff, 0xffff,
   0xffff, 0xffff, 0xffff, 0xffff,
   0xffff, 0xffff, 0xffff, 0xffff,
   0xffff, 0xffff, 0xffff, 0xffff,
   0xffff, 0xffff, 0xffff, 0xffff,
   0xffff, 0xffff, 0xffff, 0xffff,
   0xffff, 0xffff, 0xffff, 0xffff,
   0xffff, 0xffff, 0xffff, 0xffff,
   0xffff, 0xffff, 0xffff, 0xffff,
   0xffff, 0xffff, 0xffff, 0xffff,
   0xffff, 0xffff, 0xffff, 0xffff,
   0xffff, 0xffff, 0xffff, 0xffff,
   0xffff, 0xffff, 0xffff, 0xffff,
   0xffff, 0xffff, 0xffff, 0xffff,
   0xffff, 0xffff, 0xffff, 0xffff,
   0xffff, 0xffff, 0xffff, 0xffff,
   0xffff, 0xffff, 0xffff, 0xffff,
   0xffff, 0xffff, 0xffff, 0xffff,
   0xffff, 0xffff, 0xffff, 0xffff,
   0xffff, 0xffff, 0xffff, 0xffff,
   0xffff, 0xffff, 0xffff, 0xffff,
   0xffff, 0xffff, 0xffff, 0xffff,
   0xffff, 0xffff, 0xffff, 0xffff,
   0xffff, 0xffff, 0xffff, 0xffff,
   0xffff, 0xffff, 0xffff, 0xffff,
   0xffff, 0xffff, 0xffff, 0xffff,
   0xffff, 0xffff, 0xffff, 0xffff,
   0xffff, 0xffff, 0xffff, 0xffff};
#endif /* X10 */
