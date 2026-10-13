#pragma once
#include <M5Cardputer.h>
#include <SD.h>
#include <math.h>
#include "../../os/app.h"
#include "../../os/app_manager.h"
#include "../../os/os_ui.h"
#include "../../config.h"


//  Editor3D v2 - solid+wireframe 3D editor, ~60fps, Blender-style
//
//  MODES (TAB / Shift+TAB):
//    VIEW  - orbit camera
//    VERT  - edit vertices
//    EDGE  - add/remove edges
//    FACE  - add/remove faces (up to 4 vertices)
//    PRIM  - insert primitives
//    FILE  - SD .obj
//
//  CONTROLS (Blender-style):
//    Fn+,/    orbit left/right
//    Fn+;/.   orbit up/down
//    Fn+=/-   zoom
//    Fn+Shift+;/.  roll (RotZ)
//    Ctrl+R   reset camera
//
//    Numpad-style: 1=Front 3=Side 7=Top (keys 1/3/7)
//    Shift+Z  toggle solid/wireframe
//
//    ,//      prev/next vertex
//    ;/.      Y+ / Y-
//    A/D      X- / X+
//    W/S      Z- / Z+
//    Shift    x5 step
//    Fn       x0.1 step
//    Ctrl+D   duplicate
//    Del      delete vertex
//
//    ,//      select A
//    Shift+,/ select B
//    Enter    add edge A-B
//    Del      remove edge A-B
//    Ctrl+Del remove all edges at A
//
//    ,//      select V0
//    Shift+,/ V1   (Ctrl+,/ V2  Fn+,/ V3)
//    Enter    add face
//    Del      remove last face
//
//    ,//      select primitive
//    WASD/;.  insert offset
//    Enter    insert
//    Ctrl+N   clear scene
//
//    ;/.      navigate
//    Enter    save/load
//    Del      delete file
//    Ctrl+S   quick save
//
//  Screen 240x135. Viewport y=[18..122]. Clip-safe.


// Raised from 80/160/60. On a no-PSRAM Cardputer there's no such thing as
// truly "unlimited" — every vertex/edge/face lives in plain internal SRAM —
// but these limits were needlessly tight (indices used to be int8_t, which
// silently capped things at 127 anyway, and larger .obj imports would just
// stop adding vertices past 80 while still referencing their old indices in
// face/edge lines, which is what caused meshes to visibly "tear" on load).
// Indices are now int16_t, and these caps are picked to comfortably fit
// within SRAM alongside everything else PaperOS needs (~14KB total for the
// arrays below), which is what "unlimited" realistically means here.
#define E3_MV      600
#define E3_ME      1200
#define E3_MF      500
#define E3_VY1     (HEADER_H + 1)
#define E3_VY2     (SCREEN_H - FOOTER_H - 1)
#define E3_VH      (E3_VY2 - E3_VY1)
#define E3_CX      120
#define E3_CY      (E3_VY1 + E3_VH/2)
#define E3_FOCAL   175.0f
#define E3_GRID    10
#define E3_TARGET_FPS 60
#define E3_FRAME_MS   (1000/E3_TARGET_FPS)

enum E3Mode { E3_VIEW=0, E3_VERT, E3_EDGE, E3_FACE, E3_PRIM, E3_FILE, E3_MODE_CNT };
static const char* E3_MN[] = {"VIEW","VERT","EDGE","FACE","PRIM","FILE"};

struct E3v3 { float x,y,z; };
struct E3Edge { int16_t a,b; };
struct E3Face { int16_t v[4]; int8_t n; };  // n=3 tri, n=4 quad

static inline E3v3 e3rx(E3v3 v,float s,float c){return{v.x,v.y*c-v.z*s,v.y*s+v.z*c};}
static inline E3v3 e3ry(E3v3 v,float s,float c){return{v.x*c+v.z*s,v.y,-v.x*s+v.z*c};}
static inline E3v3 e3rz(E3v3 v,float s,float c){return{v.x*c-v.y*s,v.x*s+v.y*c,v.z};}
static inline E3v3 e3add(E3v3 a,E3v3 b){return{a.x+b.x,a.y+b.y,a.z+b.z};}
static inline E3v3 e3sub(E3v3 a,E3v3 b){return{a.x-b.x,a.y-b.y,a.z-b.z};}
static inline E3v3 e3cross(E3v3 a,E3v3 b){return{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
static inline float e3dot(E3v3 a,E3v3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static inline float e3len(E3v3 v){return sqrtf(v.x*v.x+v.y*v.y+v.z*v.z);}
static inline E3v3 e3norm(E3v3 v){float l=e3len(v)+1e-6f;return{v.x/l,v.y/l,v.z/l};}

static bool e3proj(E3v3 v,float dist,int&sx,int&sy){
    float z=v.z+dist; if(z<0.2f)return false;
    float f=E3_FOCAL/z;
    sx=(int)(E3_CX+v.x*f); sy=(int)(E3_CY-v.y*f); return true;
}

// Cohen-Sutherland clip
static bool e3clip(int&x0,int&y0,int&x1,int&y1,int xmn,int ymn,int xmx,int ymx){
    auto code=[&](int x,int y)->int{return(x<xmn?1:0)|(x>xmx?2:0)|(y<ymn?4:0)|(y>ymx?8:0);};
    int c0=code(x0,y0),c1=code(x1,y1);
    for(int it=0;it<8;it++){
        if(!(c0|c1))return true; if(c0&c1)return false;
        int c=c0?c0:c1,x,y;
        if(c&8){x=x0+(x1-x0)*(ymx-y0)/(y1-y0);y=ymx;}
        else if(c&4){x=x0+(x1-x0)*(ymn-y0)/(y1-y0);y=ymn;}
        else if(c&2){y=y0+(y1-y0)*(xmx-x0)/(x1-x0);x=xmx;}
        else{y=y0+(y1-y0)*(xmn-x0)/(x1-x0);x=xmn;}
        if(c==c0){x0=x;y0=y;c0=code(x0,y0);}else{x1=x;y1=y;c1=code(x1,y1);}
    }
    return false;
}
static void e3ln(LGFX_Sprite&cv,int x0,int y0,int x1,int y1,uint16_t col){
    if(e3clip(x0,y0,x1,y1,0,E3_VY1,SCREEN_W-1,E3_VY2)) cv.drawLine(x0,y0,x1,y1,col);
}

// Triangle fill (scanline, clipped to viewport)
static void e3tri(LGFX_Sprite&cv,
                  int x0,int y0,int x1,int y1,int x2,int y2,
                  uint16_t fill,uint16_t wire){
    // sort by y
    if(y0>y1){std::swap(x0,x1);std::swap(y0,y1);}
    if(y1>y2){std::swap(x1,x2);std::swap(y1,y2);}
    if(y0>y1){std::swap(x0,x1);std::swap(y0,y1);}
    int ymn=max(y0,E3_VY1), ymx=min(y2,E3_VY2);
    for(int y=ymn;y<=ymx;y++){
        float t02=(y2==y0)?1.0f:(float)(y-y0)/(y2-y0);
        int xa=x0+(int)((x2-x0)*t02);
        int xb;
        if(y<=y1){
            float t01=(y1==y0)?1.0f:(float)(y-y0)/(y1-y0);
            xb=x0+(int)((x1-x0)*t01);
        } else {
            float t12=(y2==y1)?1.0f:(float)(y-y1)/(y2-y1);
            xb=x1+(int)((x2-x1)*t12);
        }
        if(xa>xb)std::swap(xa,xb);
        xa=max(xa,0); xb=min(xb,SCREEN_W-1);
        if(xa<=xb) cv.drawFastHLine(xa,y,xb-xa+1,fill);
    }
    // wireframe overlay
    if(wire){
        e3ln(cv,x0,y0,x1,y1,wire);
        e3ln(cv,x1,y1,x2,y2,wire);
        e3ln(cv,x2,y2,x0,y0,wire);
    }
}

struct E3Prim{
    const char* name;
    uint8_t vc,ec,fc;
    float   vv[10][3];
    uint8_t ee[18][2];
    int8_t  ff[8][4]; // faces, -1 = unused
    uint8_t fn[8];    // vertex count for this face
};
static const E3Prim E3P[]={
 {"Cube",8,12,6,
  {{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}},
  {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}},
  {{0,1,2,3},{4,5,6,7},{0,1,5,4},{2,3,7,6},{1,2,6,5},{0,3,7,4}},{4,4,4,4,4,4}},
 {"Pyramid",5,8,5,
  {{0,1.6f,0},{-1,-1,-1},{1,-1,-1},{1,-1,1},{-1,-1,1}},
  {{0,1},{0,2},{0,3},{0,4},{1,2},{2,3},{3,4},{4,1}},
  {{0,1,2,-1},{0,2,3,-1},{0,3,4,-1},{0,4,1,-1},{1,2,3,4}},{3,3,3,3,4}},
 {"Plane",4,5,2,
  {{-1.5f,0,-1.5f},{1.5f,0,-1.5f},{1.5f,0,1.5f},{-1.5f,0,1.5f}},
  {{0,1},{1,2},{2,3},{3,0},{0,2},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
  {{0,1,2,-1},{0,2,3,-1},{-1,-1,-1,-1},{-1,-1,-1,-1}},{3,3,0,0}},
 {"Diamond",6,12,8,
  {{0,1.5f,0},{1,0,0},{0,0,1},{-1,0,0},{0,0,-1},{0,-1.5f,0}},
  {{0,1},{0,2},{0,3},{0,4},{5,1},{5,2},{5,3},{5,4},{1,2},{2,3},{3,4},{4,1}},
  {{0,1,2,-1},{0,2,3,-1},{0,3,4,-1},{0,4,1,-1},{5,2,1,-1},{5,3,2,-1},{5,4,3,-1},{5,1,4,-1}},{3,3,3,3,3,3,3,3}},
 {"Prism",6,9,5,
  {{0,1,0},{-1,-1,0},{1,-1,0},{0,1,2},{-1,-1,2},{1,-1,2}},
  {{0,1},{1,2},{2,0},{3,4},{4,5},{5,3},{0,3},{1,4},{2,5}},
  {{0,1,2,-1},{3,4,5,-1},{0,1,4,3},{1,2,5,4},{2,0,3,5}},{3,3,4,4,4}},
};
#define E3_PCNT 5


// Set by the file manager before launch("3D Editor") so a specific .obj
// picked in the Explorer actually loads, instead of always opening a
// blank scene (same pending-file pattern as Music's g_musicPendingFile).
static char g_editor3dPendingFile[96] = "";

class Editor3D : public App {
public:
    Editor3D(LGFX_Sprite&cv):_cv(cv){}
    const char* getName() const override{return"3D Editor";}
    const char* getIcon() const override{return"3";}
    const char* getDesc() const override{return"Solid wireframe modeler";}

    void onStart() override{
        if(!_verts){
            _verts = new E3v3[E3_MV];
            _edges = new E3Edge[E3_ME];
            _faces = new E3Face[E3_MF];
            _sx    = new int[E3_MV];
            _sy    = new int[E3_MV];
            _vis   = new bool[E3_MV];
            _tv    = new E3v3[E3_MV];
        }
        _vc=_ec=_fc=0; _sv=0; _ea=0; _eb=1;
        _fa[0]=_fa[1]=_fa[2]=_fa[3]=0; _fn=3;
        _sp=0; _mode=E3_VIEW;
        _rx=0.4f;_ry=0.6f;_rz=0.0f;_dist=5.5f;
        _solid=true; _dirty=false; _lname="";
        _ofs={0,0,0}; _fsc=0; _fss=0;
        _lastMs=millis(); _needRedraw=true;
        memset(_st,0,sizeof(_st));
        _addPrim(0,{0,0,0});

        if (g_editor3dPendingFile[0]) {
            String p = g_editor3dPendingFile;
            _loadOBJ(p);
            int slash = p.lastIndexOf('/');
            _lname = (slash >= 0) ? p.substring(slash + 1) : p;
            g_editor3dPendingFile[0] = '\0';
        }
    }
    void onStop() override{
        delete[] _verts; _verts=nullptr;
        delete[] _edges; _edges=nullptr;
        delete[] _faces; _faces=nullptr;
        delete[] _sx;    _sx=nullptr;
        delete[] _sy;    _sy=nullptr;
        delete[] _vis;   _vis=nullptr;
        delete[] _tv;    _tv=nullptr;
    }

    void update() override{
        uint32_t now=millis();
        M5Cardputer.update();
        auto ks=M5Cardputer.Keyboard.keysState();

        // Exit
        if(M5Cardputer.Keyboard.isKeyPressed('`')){
            OsUI::waitRelease();
            if(!_dirty||OsUI::confirmExit(_cv,"Exit? Unsaved!"))
                AppManager::get().goHome();
            else{_needRedraw=true;}
            return;
        }
        // TAB / Shift+TAB - mode
        if(ks.tab){
            int d=ks.shift?-1:1;
            _mode=(E3Mode)((_mode+d+E3_MODE_CNT)%E3_MODE_CNT);
            if(_mode==E3_FILE)_scanFiles();
            OsUI::waitRelease(); _st[0]='\0'; _needRedraw=true; return;
        }

        // Camera in modes other than VIEW - via Fn
        bool camMoved=false;
        if(ks.fn && _mode != E3_VIEW){
            float rs=0.06f, zs=0.3f;
            if(M5Cardputer.Keyboard.isKeyPressed(','))   {_ry-=rs;camMoved=true;}
            if(M5Cardputer.Keyboard.isKeyPressed('/'))   {_ry+=rs;camMoved=true;}
            if(M5Cardputer.Keyboard.isKeyPressed(';'))   {if(ks.shift)_rz-=rs;else _rx-=rs;camMoved=true;}
            if(M5Cardputer.Keyboard.isKeyPressed('.'))   {if(ks.shift)_rz+=rs;else _rx+=rs;camMoved=true;}
            if(M5Cardputer.Keyboard.isKeyPressed('='))   {_dist=max(1.2f,_dist-zs);camMoved=true;}
            if(M5Cardputer.Keyboard.isKeyPressed('-'))   {_dist=min(22.f,_dist+zs);camMoved=true;}
        }
        if(camMoved) _needRedraw=true;

        // Ctrl+R - reset camera
        if(OsUI::ctrlChar(ks,'r')){
            _rx=0.4f;_ry=0.6f;_rz=0.0f;_dist=5.5f;
            snprintf(_st,35,"Camera reset");OsUI::waitRelease();_needRedraw=true;
        }
        // Numpad views (1=front 3=side 7=top)
        if(!ks.fn&&!ks.ctrl){
            if(M5Cardputer.Keyboard.isKeyPressed('1')){_rx=0;_ry=0;_rz=0;_needRedraw=true;OsUI::waitRelease();}
            if(M5Cardputer.Keyboard.isKeyPressed('3')){_rx=0;_ry=1.5708f;_rz=0;_needRedraw=true;OsUI::waitRelease();}
            if(M5Cardputer.Keyboard.isKeyPressed('7')){_rx=1.5708f;_ry=0;_rz=0;_needRedraw=true;OsUI::waitRelease();}
        }
        // Shift+Z - toggle solid/wireframe
        if(OsUI::shiftChar(ks,'z')){
            _solid=!_solid;snprintf(_st,35,_solid?"Solid":"Wireframe");_needRedraw=true;OsUI::waitRelease();
        }

        // Modes
        if(!ks.fn){
            switch(_mode){
                case E3_VIEW: _doView(ks); break;
                case E3_VERT: _doVert(ks); break;
                case E3_EDGE: _doEdge(ks); break;
                case E3_FACE: _doFace(ks); break;
                case E3_PRIM: _doPrim(ks); break;
                case E3_FILE: _doFile(ks); return; // FILE mode draws itself
            }
        }

        // 60fps: only redraw if needed AND a frame interval has passed
        if(_needRedraw){
            uint32_t elapsed=millis()-_lastMs;
            if(elapsed>=E3_FRAME_MS||camMoved){
                _redraw();
                _lastMs=millis();
                _needRedraw=false;
            }
        }
    }

private:
    LGFX_Sprite& _cv;
    // These used to be plain fixed-size members, which meant ~32KB was
    // permanently reserved in .bss for the whole lifetime of the firmware
    // (Editor3D is a global App instance constructed once at boot), whether
    // or not the 3D editor was ever opened. That silently ate into the RAM
    // budget every other app/subsystem has to share — including MyAI's
    // model load, which mallocs a large KV cache on demand and can fail if
    // free heap has shrunk. Now they're heap-allocated in onStart() and
    // freed in onStop(), so the memory only exists while this app is
    // actually the active one (same fix that moved the old stack-local
    // per-frame projection scratch — sx/sy/vis/tv — off the stack, just
    // without pinning it in place permanently).
    E3v3   *_verts = nullptr;
    E3Edge *_edges = nullptr;
    E3Face *_faces = nullptr;
    int    *_sx = nullptr, *_sy = nullptr;
    bool   *_vis = nullptr;
    E3v3   *_tv = nullptr;
    int   _vc,_ec,_fc;
    int   _sv,_ea,_eb;
    int16_t _fa[4]; int8_t _fn;
    int   _sp; E3Mode _mode;
    float _rx,_ry,_rz,_dist;
    bool  _solid,_dirty,_needRedraw;
    char  _st[36];
    E3v3  _ofs;
    String _lname;
    String _files[20]; int _fsc,_fss;
    uint32_t _lastMs;

    // Project all vertices
    void _projAll(int*sx,int*sy,bool*vis,E3v3*tv){
        float srx=sinf(_rx),crx=cosf(_rx);
        float sry=sinf(_ry),cry=cosf(_ry);
        float srz=sinf(_rz),crz=cosf(_rz);
        for(int i=0;i<_vc;i++){
            E3v3 v=_verts[i];
            v=e3rx(v,srx,crx); v=e3ry(v,sry,cry); v=e3rz(v,srz,crz);
            if(tv) tv[i]=v;
            vis[i]=e3proj(v,_dist,sx[i],sy[i]);
        }
    }

    // Face lighting -> fill color (two-sided)
    uint16_t _faceColor(E3v3*tv,E3Face&f){
        E3v3 v0=tv[f.v[0]],v1=tv[f.v[1]],v2=tv[f.v[2]];
        E3v3 n=e3norm(e3cross(e3sub(v1,v0),e3sub(v2,v0)));
        static const E3v3 light={0.4f,0.7f,0.9f};
        float diff=fabsf(e3dot(n,e3norm(light)));  // abs = two-sided
        diff=max(0.15f,diff);
        // white -> gray: 0x40..0xFF
        uint8_t lv=(uint8_t)(diff*0xC0+0x3F);
        lv&=0xF8;
        uint16_t r5=lv>>3, g6=(lv>>2)&0x3F, b5=lv>>3;
        return (r5<<11)|(g6<<5)|b5;
    }

    // Backface culling - DISABLED: both directions are always drawn
    bool _backface(E3v3*tv,E3Face&f){
        (void)tv; (void)f;
        return false;  // never cull a face
    }

    // Sort faces by z (painter's algorithm)
    int _fsort[E3_MF];
    float _fdepth[E3_MF];
    void _sortFaces(E3v3*tv){
        for(int i=0;i<_fc;i++){
            float d=0; int n=_faces[i].n;
            for(int j=0;j<n;j++) d+=tv[_faces[i].v[j]].z;
            _fdepth[i]=d/n; _fsort[i]=i;
        }
        // simple insertion sort (fc is usually <30)
        for(int i=1;i<_fc;i++){
            int k=_fsort[i]; float dk=_fdepth[k]; int j=i-1;
            while(j>=0&&_fdepth[_fsort[j]]<dk){_fsort[j+1]=_fsort[j];j--;}
            _fsort[j+1]=k;
        }
    }

    void _drawGrid(){
        for(int y=E3_VY1;y<E3_VY2;y+=E3_GRID){
            for(int x=0;x<SCREEN_W;x+=E3_GRID){
                int r=(y-E3_VY1)/E3_GRID, c=x/E3_GRID;
                uint16_t col=((r+c)&1)?0x0841:0x0000;
                int h=min(E3_GRID,E3_VY2-y);
                _cv.fillRect(x,y,E3_GRID,h,col);
            }
        }
        for(int x=0;x<SCREEN_W;x+=E3_GRID) _cv.drawFastVLine(x,E3_VY1,E3_VH,0x18C6);
        for(int y=E3_VY1;y<E3_VY2;y+=E3_GRID) _cv.drawFastHLine(0,y,SCREEN_W,0x18C6);
        // origin cross
        _cv.drawFastHLine(E3_CX-5,E3_CY,10,0x4208);
        _cv.drawFastVLine(E3_CX,E3_CY-5,10,0x4208);
    }

    void _redraw(){
        _cv.fillSprite(0x0000);
        _drawGrid();

        int*  sx=_sx; int* sy=_sy;
        bool* vis=_vis;
        E3v3* tv=_tv;
        _projAll(sx,sy,vis,tv);

        if(_solid&&_fc>0){
            // Painter's: farthest first
            _sortFaces(tv);
            uint16_t wcol=0x2945; // dark wire overlay
            for(int fi=0;fi<_fc;fi++){
                E3Face&f=_faces[_fsort[fi]];
                if(f.n<3) continue;
                // backface cull
                if(_backface(tv,f)) continue;
                // don't skip a face just because vertices are off-screen - e3tri clips itself
                uint16_t fc=_faceColor(tv,f);
                if(f.n==3){
                    e3tri(_cv,sx[f.v[0]],sy[f.v[0]],
                              sx[f.v[1]],sy[f.v[1]],
                              sx[f.v[2]],sy[f.v[2]],fc,wcol);
                } else {
                    // quad -> 2 triangles
                    e3tri(_cv,sx[f.v[0]],sy[f.v[0]],
                              sx[f.v[1]],sy[f.v[1]],
                              sx[f.v[2]],sy[f.v[2]],fc,0);
                    e3tri(_cv,sx[f.v[0]],sy[f.v[0]],
                              sx[f.v[2]],sy[f.v[2]],
                              sx[f.v[3]],sy[f.v[3]],fc,0);
                    // wire quad
                    e3ln(_cv,sx[f.v[0]],sy[f.v[0]],sx[f.v[1]],sy[f.v[1]],wcol);
                    e3ln(_cv,sx[f.v[1]],sy[f.v[1]],sx[f.v[2]],sy[f.v[2]],wcol);
                    e3ln(_cv,sx[f.v[2]],sy[f.v[2]],sx[f.v[3]],sy[f.v[3]],wcol);
                    e3ln(_cv,sx[f.v[3]],sy[f.v[3]],sx[f.v[0]],sy[f.v[0]],wcol);
                }
            }
        }

        // Wireframe edges - drawn even if vertices are off-screen (e3ln clips)
        // Only skip if both vertices are behind the near plane
        uint16_t ecol=_solid?0x4208:COL_ACCENT;
        for(int i=0;i<_ec;i++){
            int a=_edges[i].a,b=_edges[i].b;
            bool aVis=(tv[a].z+_dist)>=0.2f, bVis=(tv[b].z+_dist)>=0.2f;
            if(!aVis && !bVis) continue;
            bool selE=(_mode==E3_EDGE&&
                ((_edges[i].a==_ea&&_edges[i].b==_eb)||
                 (_edges[i].a==_eb&&_edges[i].b==_ea)));
            // If one vertex is behind the camera - approximate with the other's coords
            int x0=aVis?sx[a]:sx[b], y0=aVis?sy[a]:sy[b];
            int x1=bVis?sx[b]:sx[a], y1=bVis?sy[b]:sy[a];
            e3ln(_cv,x0,y0,x1,y1,selE?(uint16_t)COL_WARN:ecol);
        }

        // Vertices
        for(int i=0;i<_vc;i++){
            if(!vis[i]) continue;
            int x=constrain(sx[i],1,SCREEN_W-2);
            int y=constrain(sy[i],E3_VY1+1,E3_VY2-1);
            bool isSV =(_mode==E3_VERT&&i==_sv);
            bool isEA =(_mode==E3_EDGE&&i==_ea);
            bool isEB =(_mode==E3_EDGE&&i==_eb);
            bool isFV =(_mode==E3_FACE&&(i==_fa[0]||i==_fa[1]||i==_fa[2]||i==_fa[3]));
            if(isSV){_cv.fillCircle(x,y,3,COL_WARN);_cv.drawCircle(x,y,5,COL_WARN);}
            else if(isEA){_cv.fillCircle(x,y,3,COL_OK);}
            else if(isEB){_cv.fillCircle(x,y,3,0xF81F);}
            else if(isFV){_cv.fillCircle(x,y,3,0xFFE0);}
            else{
                if(!_solid){_cv.fillCircle(x,y,2,0x4208);_cv.drawPixel(x,y,COL_DIM);}
                else{_cv.fillCircle(x,y,1,0x4208);}
            }
            if(isSV||isEA||isEB||isFV){
                char nb[4];snprintf(nb,3,"%d",i);
                _cv.setTextColor(COL_WARN,0x0000);_cv.setTextSize(1);
                _cv.setCursor(constrain(x+4,0,SCREEN_W-12),constrain(y-8,E3_VY1,E3_VY2-8));
                _cv.print(nb);
            }
        }

        // Insert-point preview (PRIM)
        if(_mode==E3_PRIM){
            float srx=sinf(_rx),crx=cosf(_rx),sry=sinf(_ry),cry=cosf(_ry),srz=sinf(_rz),crz=cosf(_rz);
            E3v3 o=_ofs;
            o=e3rx(o,srx,crx);o=e3ry(o,sry,cry);o=e3rz(o,srz,crz);
            int px,py;
            if(e3proj(o,_dist,px,py)){
                px=constrain(px,2,SCREEN_W-3);py=constrain(py,E3_VY1+2,E3_VY2-2);
                _cv.drawCircle(px,py,6,COL_WARN);
                _cv.drawFastHLine(px-4,py,8,COL_WARN);
                _cv.drawFastVLine(px,py-4,8,COL_WARN);
            }
        }

        // Header
        char ht[38];
        snprintf(ht,37,"3D[%s] V:%d E:%d F:%d %s",
            E3_MN[_mode],_vc,_ec,_fc,_solid?"S":"W");
        OsUI::drawHeader(_cv,ht,0x0410);
        _cv.drawFastHLine(0,HEADER_H,SCREEN_W,COL_ACCENT);
        if(_dirty){_cv.setTextColor(COL_WARN,0x0410);_cv.setTextSize(1);_cv.setCursor(SCREEN_W-42,4);_cv.print("*");}
        if(_st[0]) OsUI::showToast(_cv,_st,0x8C60);

        // Footer
        _cv.fillRect(0,SCREEN_H-FOOTER_H,SCREEN_W,FOOTER_H,COL_FOOTER);
        _cv.drawFastHLine(0,SCREEN_H-FOOTER_H,SCREEN_W,COL_ACCENT);
        _cv.setTextColor(COL_DIM,COL_FOOTER);_cv.setTextSize(1);_cv.setCursor(5,SCREEN_H-FOOTER_H+3);
        switch(_mode){
            case E3_VIEW: _cv.print(",./;=cam  1/3/7=front/side/top"); break;
            case E3_VERT:
                if(_vc>0){char b[36];snprintf(b,35,"V%d(%.1f,%.1f,%.1f) WASD/;. Sh=fast",
                    _sv,_verts[_sv].x,_verts[_sv].y,_verts[_sv].z);_cv.print(b);}
                else _cv.print("No verts. Add prim first.");break;
            case E3_EDGE:{char b[36];snprintf(b,35,"A:%d B:%d ,/=A Sh+,/=B ENT=add",_ea,_eb);_cv.print(b);break;}
            case E3_FACE:{char b[36];snprintf(b,35,"V0:%d V1:%d V2:%d ,/=sel ENT=add",_fa[0],_fa[1],_fa[2]);_cv.print(b);break;}
            case E3_PRIM:{char b[36];snprintf(b,35,"[%s] ,/=sel WASD/;.=ofs ENT=ins",E3P[_sp].name);_cv.print(b);break;}
            case E3_FILE: _cv.print(";.=nav ENT=act DEL=del Ctrl+S=save");break;
        }
        _cv.pushSprite(0,0);
    }

    void _doView(Keyboard_Class::KeysState&ks){
        // VIEW without Fn - smooth rotation, no delay
        float rs=0.05f,zs=0.25f;
        bool ch=false;
        if(M5Cardputer.Keyboard.isKeyPressed(';')){if(ks.shift)_rz-=rs;else _rx-=rs;ch=true;}
        if(M5Cardputer.Keyboard.isKeyPressed('.')){if(ks.shift)_rz+=rs;else _rx+=rs;ch=true;}
        if(M5Cardputer.Keyboard.isKeyPressed(','))  {_ry-=rs;ch=true;}
        if(M5Cardputer.Keyboard.isKeyPressed('/')){_ry+=rs;ch=true;}
        if(M5Cardputer.Keyboard.isKeyPressed('=')){_dist=max(1.2f,_dist-zs);ch=true;}
        if(M5Cardputer.Keyboard.isKeyPressed('-')){_dist=min(22.f,_dist+zs);ch=true;}
        if(ch) _needRedraw=true;
    }

    void _doVert(Keyboard_Class::KeysState&ks){
        if(_vc==0) return;
        float step=ks.shift?0.5f:0.1f;
        bool ch=false;
        if(M5Cardputer.Keyboard.isKeyPressed(','))  {if(_sv>0){_sv--;ch=true;}}
        if(M5Cardputer.Keyboard.isKeyPressed('/')){if(_sv<_vc-1){_sv++;ch=true;}}
        if(M5Cardputer.Keyboard.isKeyPressed(';'))  {_verts[_sv].y+=step;ch=true;_dirty=true;}
        if(M5Cardputer.Keyboard.isKeyPressed('.')){_verts[_sv].y-=step;ch=true;_dirty=true;}
        if(M5Cardputer.Keyboard.isKeyPressed('a')||M5Cardputer.Keyboard.isKeyPressed('A')){_verts[_sv].x-=step;ch=true;_dirty=true;}
        if(M5Cardputer.Keyboard.isKeyPressed('d')||M5Cardputer.Keyboard.isKeyPressed('D')){_verts[_sv].x+=step;ch=true;_dirty=true;}
        if(M5Cardputer.Keyboard.isKeyPressed('w')||M5Cardputer.Keyboard.isKeyPressed('W')){_verts[_sv].z-=step;ch=true;_dirty=true;}
        if(M5Cardputer.Keyboard.isKeyPressed('s')||M5Cardputer.Keyboard.isKeyPressed('S')){_verts[_sv].z+=step;ch=true;_dirty=true;}
        if(OsUI::ctrlChar(ks,'d')){
            if(_vc<E3_MV){_verts[_vc]=_verts[_sv];_verts[_vc].x+=0.3f;_vc++;_sv=_vc-1;_dirty=true;ch=true;}
            OsUI::waitRelease();}
        if(ks.del){_delVert(_sv);ch=true;OsUI::waitRelease();}
        if(ch){_needRedraw=true;delay(KEY_REPEAT_MS/3);}
    }

    void _doEdge(Keyboard_Class::KeysState&ks){
        if(_vc<2) return;
        bool ch=false;
        if(!ks.shift){
            if(M5Cardputer.Keyboard.isKeyPressed(','))  {if(_ea>0){_ea--;ch=true;}delay(KEY_REPEAT_MS);}
            if(M5Cardputer.Keyboard.isKeyPressed('/')){if(_ea<_vc-1){_ea++;ch=true;}delay(KEY_REPEAT_MS);}
        }else{
            if(M5Cardputer.Keyboard.isKeyPressed(','))  {if(_eb>0){_eb--;ch=true;}delay(KEY_REPEAT_MS);}
            if(M5Cardputer.Keyboard.isKeyPressed('/')){if(_eb<_vc-1){_eb++;ch=true;}delay(KEY_REPEAT_MS);}
        }
        if(ks.enter){_addEdge(_ea,_eb);ch=true;OsUI::waitRelease();}
        if(ks.del&&!ks.ctrl){_delEdge(_ea,_eb);ch=true;OsUI::waitRelease();}
        if(ks.del&&ks.ctrl){_delAllE(_ea);ch=true;OsUI::waitRelease();}
        if(ch) _needRedraw=true;
    }

    void _doFace(Keyboard_Class::KeysState&ks){
        if(_vc<3) return;
        bool ch=false;
        // ,// selects V0, Shift-> V1, Ctrl-> V2, Fn-> V3
        int vi=ks.shift?1:(ks.ctrl?2:(ks.fn?3:0));
        if(M5Cardputer.Keyboard.isKeyPressed(','))  {if(_fa[vi]>0){_fa[vi]--;ch=true;}delay(KEY_REPEAT_MS);}
        if(M5Cardputer.Keyboard.isKeyPressed('/')){if(_fa[vi]<_vc-1){_fa[vi]++;ch=true;}delay(KEY_REPEAT_MS);}
        // number of vertices in the face
        if(M5Cardputer.Keyboard.isKeyPressed('3')) {_fn=3;ch=true;delay(200);}
        if(M5Cardputer.Keyboard.isKeyPressed('4')) {_fn=4;ch=true;delay(200);}
        if(ks.enter){
            if(_fc<E3_MF){
                E3Face f; f.n=_fn;
                for(int i=0;i<_fn;i++) f.v[i]=_fa[i];
                _faces[_fc++]=f; _dirty=true;
                snprintf(_st,35,"Face added (n=%d)",_fn);
            } else snprintf(_st,35,"Max faces!");
            OsUI::waitRelease();ch=true;
        }
        if(ks.del&&_fc>0){_fc--;_dirty=true;snprintf(_st,35,"Face removed");OsUI::waitRelease();ch=true;}
        if(ch) _needRedraw=true;
    }

    void _doPrim(Keyboard_Class::KeysState&ks){
        float os=ks.shift?0.5f:0.2f;
        bool ch=false;
        if(M5Cardputer.Keyboard.isKeyPressed(','))  {if(_sp>0){_sp--;ch=true;}delay(KEY_REPEAT_MS);}
        if(M5Cardputer.Keyboard.isKeyPressed('/')){if(_sp<E3_PCNT-1){_sp++;ch=true;}delay(KEY_REPEAT_MS);}
        if(M5Cardputer.Keyboard.isKeyPressed(';'))  {_ofs.y+=os;ch=true;delay(KEY_REPEAT_MS);}
        if(M5Cardputer.Keyboard.isKeyPressed('.')){_ofs.y-=os;ch=true;delay(KEY_REPEAT_MS);}
        if(M5Cardputer.Keyboard.isKeyPressed('a')||M5Cardputer.Keyboard.isKeyPressed('A')){_ofs.x-=os;ch=true;delay(KEY_REPEAT_MS);}
        if(M5Cardputer.Keyboard.isKeyPressed('d')||M5Cardputer.Keyboard.isKeyPressed('D')){_ofs.x+=os;ch=true;delay(KEY_REPEAT_MS);}
        if(M5Cardputer.Keyboard.isKeyPressed('w')||M5Cardputer.Keyboard.isKeyPressed('W')){_ofs.z-=os;ch=true;delay(KEY_REPEAT_MS);}
        if(M5Cardputer.Keyboard.isKeyPressed('s')||M5Cardputer.Keyboard.isKeyPressed('S')){_ofs.z+=os;ch=true;delay(KEY_REPEAT_MS);}
        if(ks.enter){_addPrim(_sp,_ofs);snprintf(_st,35,"Added %s",E3P[_sp].name);OsUI::waitRelease();ch=true;}
        if(OsUI::ctrlChar(ks,'n')){
            OsUI::waitRelease();
            if(OsUI::confirmExit(_cv,"Clear scene?")){_vc=_ec=_fc=0;_sv=0;_dirty=true;snprintf(_st,35,"Scene cleared");}
            ch=true;
        }
        if(ch) _needRedraw=true;
    }

    void _scanFiles(){
        _fsc=0;_fss=0;
        if(SD.cardType()==CARD_NONE) return;
        if(!SD.exists("/MitamaOS")) SD.mkdir("/MitamaOS");
        if(!SD.exists("/MitamaOS/3d")) SD.mkdir("/MitamaOS/3d");
        File root=SD.open("/MitamaOS/3d"); if(!root||!root.isDirectory()) return;
        File f=root.openNextFile();
        while(f&&_fsc<20){String n=String(f.name());if(n.endsWith(".obj")||n.endsWith(".OBJ"))_files[_fsc++]=n;f=root.openNextFile();}
    }

    void _doFile(Keyboard_Class::KeysState&ks){
        if(OsUI::ctrlChar(ks,'s')){
            OsUI::waitRelease();
            if(_lname.length()>0)_saveOBJ("/MitamaOS/3d/"+_lname);
            else{String n=OsUI::inputText(_cv,"Filename:");if(n.length()>0){if(!n.endsWith(".obj"))n+=".obj";_lname=n;_saveOBJ("/MitamaOS/3d/"+n);}}
            _drawFileMenu();return;
        }
        bool ch=false;
        if(M5Cardputer.Keyboard.isKeyPressed(';'))  {if(_fss>0){_fss--;ch=true;}delay(KEY_REPEAT_MS);}
        if(M5Cardputer.Keyboard.isKeyPressed('.')){if(_fss<=_fsc){_fss++;ch=true;}delay(KEY_REPEAT_MS);}
        if(ks.enter){
            OsUI::waitRelease();
            if(_fss==0){String n=OsUI::inputText(_cv,"Filename:");if(n.length()>0){if(!n.endsWith(".obj"))n+=".obj";_lname=n;_saveOBJ("/MitamaOS/3d/"+n);}}
            else if(_fss<=_fsc){_lname=_files[_fss-1];_loadOBJ("/MitamaOS/3d/"+_lname);}
            _scanFiles();ch=true;
        }
        if(ks.del&&_fss>0&&_fss<=_fsc){
            OsUI::waitRelease();
            if(OsUI::confirmExit(_cv,"Delete file?")){SD.remove("/MitamaOS/3d/"+_files[_fss-1]);snprintf(_st,35,"Deleted");_scanFiles();ch=true;}
        }
        (void)ch;
        _drawFileMenu();
    }

    void _drawFileMenu(){
        _cv.fillSprite(COL_BG);
        OsUI::drawHeader(_cv,"3D Editor [FILE]",0x3186);
        _cv.drawFastHLine(0,HEADER_H,SCREEN_W,COL_ACCENT);
        int y=E3_VY1+2;
        bool sn=(_fss==0);
        _cv.fillRect(4,y,SCREEN_W-8,14,sn?(uint16_t)COL_SELECTED:(uint16_t)COL_BG2);
        _cv.drawRect(4,y,SCREEN_W-8,14,sn?(uint16_t)COL_ACCENT:(uint16_t)0x2945);
        _cv.setTextColor(sn?(uint16_t)WHITE:(uint16_t)COL_TEXT,sn?(uint16_t)COL_SELECTED:(uint16_t)COL_BG2);
        _cv.setTextSize(1);_cv.setCursor(9,y+4);
        _cv.print(_dirty?"+ Save new...  (unsaved)":"+ Save new...");
        y+=16;
        if(_fsc==0){_cv.setTextColor(COL_DIM,COL_BG);_cv.setCursor(9,y+4);_cv.print("No .obj in /MitamaOS/3d/");}
        else{
            int show=min(_fsc,5),start=max(0,min(_fss-1,_fsc-show));
            for(int i=start;i<start+show&&i<_fsc;i++){
                bool sel=(_fss==i+1);
                _cv.fillRect(4,y,SCREEN_W-8,14,sel?(uint16_t)COL_SELECTED:(uint16_t)COL_BG2);
                _cv.drawRect(4,y,SCREEN_W-8,14,sel?(uint16_t)COL_ACCENT:(uint16_t)0x2945);
                _cv.setTextColor(sel?(uint16_t)WHITE:(uint16_t)COL_TEXT,sel?(uint16_t)COL_SELECTED:(uint16_t)COL_BG2);
                _cv.setTextSize(1);_cv.setCursor(9,y+4);
                String nm=_files[i];if(nm.length()>34)nm=nm.substring(0,33)+"~";
                _cv.print(nm);y+=16;
            }
        }
        if(_st[0])OsUI::showToast(_cv,_st,COL_OK);
        OsUI::drawFooter(_cv,";.=nav ENT=act DEL=del Ctrl+S=save");
        _cv.pushSprite(0,0);
    }

    void _addPrim(int pi,E3v3 ofs){
        const E3Prim&p=E3P[pi];
        if(_vc+p.vc>E3_MV){snprintf(_st,35,"Max verts!");_needRedraw=true;return;}
        if(_ec+p.ec>E3_ME){snprintf(_st,35,"Max edges!");_needRedraw=true;return;}
        int vb=_vc;
        for(int i=0;i<p.vc;i++) _verts[_vc++]={p.vv[i][0]+ofs.x,p.vv[i][1]+ofs.y,p.vv[i][2]+ofs.z};
        for(int i=0;i<p.ec;i++) _edges[_ec++]={(int16_t)(vb+p.ee[i][0]),(int16_t)(vb+p.ee[i][1])};
        for(int i=0;i<p.fc;i++){
            if(_fc>=E3_MF) break;
            if(p.fn[i]==0) continue;
            E3Face f; f.n=p.fn[i];
            for(int j=0;j<f.n;j++) f.v[j]=(int16_t)(vb+p.ff[i][j]);
            _faces[_fc++]=f;
        }
        _dirty=true; _needRedraw=true;
    }

    void _delVert(int vi){
        if(_vc==0) return;
        int ne=0;
        for(int i=0;i<_ec;i++){
            if(_edges[i].a==vi||_edges[i].b==vi) continue;
            int16_t a=_edges[i].a>vi?_edges[i].a-1:_edges[i].a;
            int16_t b=_edges[i].b>vi?_edges[i].b-1:_edges[i].b;
            _edges[ne++]={a,b};
        }
        _ec=ne;
        // remove faces referencing this vertex
        int nf=0;
        for(int i=0;i<_fc;i++){
            bool has=false;
            for(int j=0;j<_faces[i].n;j++) if(_faces[i].v[j]==vi){has=true;break;}
            if(has) continue;
            E3Face f=_faces[i];
            for(int j=0;j<f.n;j++) if(f.v[j]>vi) f.v[j]--;
            _faces[nf++]=f;
        }
        _fc=nf;
        for(int i=vi;i<_vc-1;i++) _verts[i]=_verts[i+1];
        _vc--;
        _sv=constrain(_sv,0,max(0,_vc-1));
        _ea=constrain(_ea,0,max(0,_vc-1));
        _eb=constrain(_eb,0,max(0,_vc-1));
        _dirty=true;
    }

    bool _hasEdge(int a,int b){for(int i=0;i<_ec;i++)if((_edges[i].a==a&&_edges[i].b==b)||(_edges[i].a==b&&_edges[i].b==a))return true;return false;}
    void _addEdge(int a,int b){
        if(a==b||_ec>=E3_ME){snprintf(_st,35,a==b?"Same vert!":"Max edges!");_needRedraw=true;return;}
        if(_hasEdge(a,b)){snprintf(_st,35,"Edge exists");_needRedraw=true;return;}
        _edges[_ec++]={(int16_t)a,(int16_t)b};_dirty=true;snprintf(_st,35,"Edge %d-%d added",a,b);_needRedraw=true;
    }
    void _delEdge(int a,int b){
        int ne=0;
        for(int i=0;i<_ec;i++) if((_edges[i].a==a&&_edges[i].b==b)||(_edges[i].a==b&&_edges[i].b==a))continue;else _edges[ne++]=_edges[i];
        if(ne<_ec){_ec=ne;_dirty=true;snprintf(_st,35,"Edge removed");}else snprintf(_st,35,"No such edge");
        _needRedraw=true;
    }
    void _delAllE(int v){
        int ne=0;for(int i=0;i<_ec;i++)if(_edges[i].a!=v&&_edges[i].b!=v)_edges[ne++]=_edges[i];
        _ec=ne;_dirty=true;snprintf(_st,35,"All edges of V%d removed",v);_needRedraw=true;
    }

    void _saveOBJ(String path){
        if(SD.cardType()==CARD_NONE){snprintf(_st,35,"No SD!");_needRedraw=true;return;}
        File f=SD.open(path,FILE_WRITE);if(!f){snprintf(_st,35,"Write fail!");_needRedraw=true;return;}
        f.println("# " OS_NAME " 3D Editor");f.println("o Mesh");
        for(int i=0;i<_vc;i++) f.printf("v %.4f %.4f %.4f\n",_verts[i].x,_verts[i].y,_verts[i].z);
        for(int i=0;i<_ec;i++) f.printf("l %d %d\n",_edges[i].a+1,_edges[i].b+1);
        for(int i=0;i<_fc;i++){
            f.print("f");for(int j=0;j<_faces[i].n;j++)f.printf(" %d",_faces[i].v[j]+1);f.println();
        }
        f.close();_dirty=false;snprintf(_st,35,"Saved!");_needRedraw=true;
    }
    void _loadOBJ(String path){
        File f=SD.open(path);if(!f){snprintf(_st,35,"Open fail!");_needRedraw=true;return;}
        _vc=_ec=_fc=0;_sv=_ea=_eb=0;
        // If the file has more verts/edges/faces than fit, we used to just
        // stop appending silently while later lines kept referencing the
        // original (now out-of-range/shifted) indices — that's what made
        // imported meshes visibly "tear": faces pointed at vertices that
        // were never loaded. Now we just track whether we hit a cap and
        // say so, instead of pretending the load was clean.
        bool truncated=false;
        while(f.available()){
            String ln=f.readStringUntil('\n');ln.trim();
            if(ln.startsWith("v ")){
                if(_vc<E3_MV){float x=0,y=0,z=0;sscanf(ln.c_str()+2,"%f %f %f",&x,&y,&z);_verts[_vc++]={x,y,z};}
                else truncated=true;
            }
            else if(ln.startsWith("l ")){
                if(_ec<E3_ME){int a=0,b=0;sscanf(ln.c_str()+2,"%d %d",&a,&b);if(a>=1&&b>=1&&a<=_vc&&b<=_vc)_edges[_ec++]={(int16_t)(a-1),(int16_t)(b-1)};}
                else truncated=true;
            }
            else if(ln.startsWith("f ")){
                if(_fc<E3_MF){
                    E3Face face;face.n=0;
                    const char*p=ln.c_str()+2;
                    while(*p&&face.n<4){while(*p==' ')p++;if(!*p)break;int vi=atoi(p)-1;if(vi>=0&&vi<_vc)face.v[face.n++]=(int16_t)vi;while(*p&&*p!=' ')p++;}
                    if(face.n>=3)_faces[_fc++]=face;
                } else truncated=true;
            }
        }
        f.close();_dirty=false;
        if(truncated) snprintf(_st,35,"Loaded V:%d E:%d F:%d (TRUNCATED!)",_vc,_ec,_fc);
        else snprintf(_st,35,"Loaded V:%d E:%d F:%d",_vc,_ec,_fc);
        _needRedraw=true;
    }
};
