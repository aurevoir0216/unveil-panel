#pragma once
/* All three charts share an identical 20 by 15 cell geometry, with 2px gutters. */
enum { UV_COLS=20, UV_ROWS=15, UV_PLOT_X=20, UV_PLOT_Y=4, UV_PLOT_W=416, UV_PLOT_H=304 };
static void uv_mosaic_cell(uint16_t *pixels,int c,int r,uint32_t bg,float level)
{
    level=uv_clamp(level);
    uint16_t color=uv_565(uv_mix(bg,0xf54b2c,.035f+.965f*powf(level,1.2f)));
    int xa=UV_PLOT_X+c*UV_PLOT_W/UV_COLS+1,xb=UV_PLOT_X+(c+1)*UV_PLOT_W/UV_COLS-1;
    int ya=UV_PLOT_Y+r*UV_PLOT_H/UV_ROWS+1,yb=UV_PLOT_Y+(r+1)*UV_PLOT_H/UV_ROWS-1;
    for (int y=ya;y<yb;++y) for (int x=xa;x<xb;++x) pixels[y*442+x]=color;
}
static void uv_activity_mosaic(uint16_t *pixels,const int *values,uint32_t bg,int low,int high)
{
    for(int c=0;c<UV_COLS;++c) {
        float fraction=uv_clamp((float)(values[c]-low)/(high-low));
        float height=fraction*UV_ROWS;
        int top=UV_ROWS-(int)ceilf(height);
        for(int r=0;r<UV_ROWS;++r) {
            float fill=uv_clamp(height-(UV_ROWS-1-r));
            if(fill>0) {
                /* Brighter peaks and a vivid cap separate real power levels. */
                float brightness=r==top ? .8f+.2f*fill : .12f+.7f*fraction*fraction;
                uv_mosaic_cell(pixels,c,r,bg,brightness);
            }
        }
    }
}
static void uv_stft_mosaic(uint16_t *pixels,const uint8_t *values,uint32_t bg)
{
    float cells[UV_ROWS*UV_COLS],minimum=63,maximum=0;
    for(int r=0;r<UV_ROWS;++r) for(int c=0;c<UV_COLS;++c) {
        unsigned sum=0,count=0;
        for(int y=r*64/UV_ROWS;y<(r+1)*64/UV_ROWS;++y)
            for(int x=c*96/UV_COLS;x<(c+1)*96/UV_COLS;++x) {sum+=values[y*96+x];++count;}
        float mean=(float)sum/count;cells[r*UV_COLS+c]=mean;
        if(mean<minimum) minimum=mean;
        if(mean>maximum) maximum=mean;
    }
    /* Contrast is derived from actual aggregated cells, never invented activity. */
    float range=maximum-minimum;
    for(int r=0;r<UV_ROWS;++r) for(int c=0;c<UV_COLS;++c)
        uv_mosaic_cell(pixels,c,r,bg,range>0.01f ? (cells[r*UV_COLS+c]-minimum)/range : cells[r*UV_COLS+c]/63.f);
}
static void uv_spectrum_mosaic(uint16_t *pixels,const int *values,uint32_t bg,int low,int high)
{
    /* Conventional spectrum: frequency left to right, relative dB bottom to top. */
    for(int c=0;c<UV_COLS;++c) {
        int peak=low;
        for(int bin=c*96/UV_COLS;bin<(c+1)*96/UV_COLS;++bin)
            if(values[bin]>peak) peak=values[bin];
        float height=uv_clamp((float)(peak-low)/(high-low))*UV_ROWS;
        for(int r=0;r<UV_ROWS;++r) {
            float fill=uv_clamp(height-(UV_ROWS-1-r));
            if(fill>0) uv_mosaic_cell(pixels,c,r,bg,(.35f+.65f*height/UV_ROWS)*fill);
        }
    }
}
