/* Timed button script for DASH_AUTOTEST builds (development only). */
static const struct
{
	u32 ms;
	u32 btn;
} dash_autotest_script[] = {
    {3000, PAD_RIGHT},     /* HOME: move along Continue */
    {6000, PAD_R1},        /* APPS grid   (shot 8) */
    {7000, PAD_RIGHT},
    {7400, PAD_DOWN},
    {9500, PAD_SELECT},    /* list        (shot 11) */
    {12000, PAD_SELECT},   /* carousel    (shot 14) */
    {12400, PAD_RIGHT},
    {15000, PAD_TRIANGLE}, /* options menu (shot 16) */
    {17000, PAD_TRIANGLE}, /* close */
    {17500, PAD_SELECT},   /* grid */
    {18000, PAD_L1},       /* HOME (shot 20) */
    {21000, PAD_R1},
    {21300, PAD_R1},       /* FILES */
    {21800, PAD_R1},       /* DEVICES (shot 23) */
    {24000, PAD_L1},       /* FILES */
    {25000, PAD_CIRCLE},   /* open "All devices" (Circle or Cross, whichever confirms) */
    {25000, PAD_CROSS},
    {29000, PAD_DOWN},     /* classic browser themed (shot 30) */
    {29300, PAD_DOWN},
    {31500, PAD_R1},       /* browser R1 menu (shot 33) */
};
