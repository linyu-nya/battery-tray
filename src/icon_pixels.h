#pragma once
inline void outlineText(const unsigned char* coverage,int width,int height,unsigned char* pixels,unsigned char red,unsigned char green,unsigned char blue) {
    for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
        unsigned int text=coverage[y*width+x],edge=0;
        for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
            int nx=x+dx,ny=y+dy;
            if(nx>=0 && nx<width && ny>=0 && ny<height && coverage[ny*width+nx]>edge) edge=coverage[ny*width+nx];
        }
        unsigned int border=edge*(255-text)/255;
        int index=(y*width+x)*4;
        // Premultiplied BGRA: white/color text over a one-pixel dark outline.
        pixels[index]=static_cast<unsigned char>(blue*text/255+20*border/255);
        pixels[index+1]=static_cast<unsigned char>(green*text/255+20*border/255);
        pixels[index+2]=static_cast<unsigned char>(red*text/255+20*border/255);
        pixels[index+3]=static_cast<unsigned char>(text+border);
    }
}
