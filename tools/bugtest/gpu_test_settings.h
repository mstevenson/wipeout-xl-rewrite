/* Device-only probes do not link the game or disc state. */
int PortLinkRole(void) { return LINK_NONE; }
int PortLinkSplit(void) { return 0; }
int PortLinkBand(int *margin, int *width, int *height) { return 0; }
int PortLinkDownsample(void) { return -1; }
int PortLinkDownsampleStyle(void) { return -1; }
int PortTracing(void) { return 0; }
int PortOriginalTrack(void) { return -1; }
int PortOriginalTrackIs64(void) { return 0; }
int PortPcData(void) { return 0; }
int PortSaturnDisc(void) { return 0; }
const struct Disc *PsxIoDisc(void) { return NULL; }
int PortN64TextureInfo(int pageX, int pageY, int depth, int clutX, int clutY,
                       float u, float v, int bounds[4], int *flags) { return 0; }

static void TestPs1Settings(int depth)
{
    portSettings.render.selection = RENDER_PS1;
    portSettings.render.ps1 = (Ps1RenderSettings){.affineTextures = 1, .scale = 1, .resample = 1, .depth = depth};
    RenderCommit();
}
