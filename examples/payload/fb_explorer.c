/* fb_explorer.c — Explorador de archivos PS5 usando PS5SDK */
#include "ps5sdk.h"

#define MAX_FILES 512
#define LIST_STEP  42

typedef struct { char name[256]; int is_dir; } FileEntry;
typedef struct {
    char      cwd[512];
    FileEntry files[MAX_FILES];
    int       count, cursor, scroll;
} ExplorerState;

static int cmp_entry(const void *a, const void *b) {
    const FileEntry *fa = a, *fb = b;
    if (!strcmp(fa->name, "..")) return -1;
    if (!strcmp(fb->name, "..")) return  1;
    if (fa->is_dir != fb->is_dir) return fb->is_dir - fa->is_dir;
    return strcmp(fa->name, fb->name);
}

static void load_dir(ExplorerState *st) {
    st->count = st->cursor = st->scroll = 0;
    DIR *d = opendir(st->cwd);
    if (!d) {
        strcpy(st->files[0].name, "<ACCESS DENIED>"); st->files[0].is_dir=0; st->count=1; return;
    }
    struct dirent *e;
    while ((e=readdir(d)) && st->count < MAX_FILES) {
        if (!strcmp(e->d_name,".")) continue;
        strncpy(st->files[st->count].name, e->d_name, 255);
        st->files[st->count].is_dir = (e->d_type == DT_DIR);
        st->count++;
    }
    closedir(d);
    if (!st->count) { strcpy(st->files[0].name,"<EMPTY>"); st->count=1; }
    else qsort(st->files, st->count, sizeof(FileEntry), cmp_entry);
}

static void nav_enter(ExplorerState *st) {
    if (!st->count || !st->files[st->cursor].is_dir) return;
    if (!strcmp(st->files[st->cursor].name, "..")) {
        int l = strlen(st->cwd);
        if (l<=1) return;
        if (st->cwd[l-1]=='/') st->cwd[--l]=0;
        while (l>0 && st->cwd[l-1]!='/') l--;
        st->cwd[l>1?l-1:1]=0;
        if (!st->cwd[0]) strcpy(st->cwd,"/");
    } else {
        int l=strlen(st->cwd);
        if (l>1) strncat(st->cwd,"/",511-l);
        strncat(st->cwd, st->files[st->cursor].name, 511-strlen(st->cwd));
    }
    load_dir(st);
}

static void render_ui(PS5SDKVideo *v, ExplorerState *st) {
    /* Fondo */
    ps5sdk_fb_fill(v, PS5SDK_DGRAY);

    /* Header */
    ps5sdk_fb_rect(v, 0, 0, PS5SDK_W, 140, PS5SDK_RGB(21,24,34));
    ps5sdk_fb_rect(v, 0, 140, PS5SDK_W/2, 3, PS5SDK_MAGENTA);
    ps5sdk_fb_rect(v, PS5SDK_W/2, 140, PS5SDK_W/2, 3, PS5SDK_CYAN);
    ps5sdk_fb_text(v, 40, 30, "PS5 SYSTEM EXPLORER", PS5SDK_CYAN);

    /* Path */
    ps5sdk_fb_rect(v, 40, 88, PS5SDK_W-80, 36, PS5SDK_RGB(32,36,51));
    ps5sdk_fb_text_sm(v, 50, 96, st->cwd, PS5SDK_RGB(224,224,224));

    /* Lista */
    int ly=170, lh=PS5SDK_H-ly-50;
    ps5sdk_fb_rect(v, 40, ly, PS5SDK_W-80, lh, PS5SDK_RGB(17,19,26));

    int mv = (lh-16)/LIST_STEP;
    for (int i=0; i<mv && (st->scroll+i)<st->count; i++) {
        int idx=st->scroll+i, y=ly+8+i*LIST_STEP;
        FileEntry *f=&st->files[idx];
        u32 tc = f->is_dir ? PS5SDK_CYAN : PS5SDK_RGB(160,170,191);
        if (f->name[0]=='<') tc=PS5SDK_MAGENTA;
        if (idx==st->cursor) { ps5sdk_fb_rect(v,45,y-4,PS5SDK_W-90,LIST_STEP,PS5SDK_RGB(41,49,69)); tc=PS5SDK_WHITE; }
        char line[280];
        if (f->is_dir) snprintf(line,sizeof(line),"[%s]",f->name);
        else strncpy(line,f->name,sizeof(line)-1);
        ps5sdk_fb_text_sm(v, 55, y+4, line, tc);
    }

    /* Scrollbar */
    if (st->count>mv) {
        int sbh=(mv*lh)/st->count; if (sbh<20) sbh=20;
        int sby=ly+(st->scroll*(lh-sbh))/(st->count-mv);
        ps5sdk_fb_rect(v,PS5SDK_W-48,ly+2,6,lh-4,PS5SDK_RGB(32,36,51));
        ps5sdk_fb_rect(v,PS5SDK_W-48,sby,6,sbh,PS5SDK_CYAN);
    }

    /* Footer */
    ps5sdk_fb_rect(v, 0, PS5SDK_H-44, PS5SDK_W, 44, PS5SDK_RGB(21,24,34));
    ps5sdk_fb_text_sm(v, 40, PS5SDK_H-34,
        "↑↓:Navegar  ✕:Entrar  ◯:Volver  OPTIONS:Salir",
        PS5SDK_RGB(138,149,165));

    ps5sdk_video_flip(v);
}

int SDL_main(int argc, char *argv[]) {
    PS5SDKVideo v;
    if (ps5sdk_video_init(&v, 0) < 0) return 1;

    PS5SDKPad pad = ps5sdk_pad_create();

    ExplorerState st;
    memset(&st, 0, sizeof(st));
    strcpy(st.cwd, "/");
    load_dir(&st);

    int running = 1;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) { running=0; break; }
            if (ev.type == SDL_CONTROLLERBUTTONDOWN) {
                int btn = ev.cbutton.button;
                int mv  = (PS5SDK_H-170-50)/LIST_STEP;
                if (btn==PS5SDK_BTN_UP   && st.cursor>0)    { st.cursor--; if (st.cursor<st.scroll) st.scroll=st.cursor; }
                if (btn==PS5SDK_BTN_DOWN && st.cursor<st.count-1) { st.cursor++; if (st.cursor>=st.scroll+mv) st.scroll=st.cursor-mv+1; }
                if (btn==PS5SDK_BTN_CROSS)   nav_enter(&st);
                if (btn==PS5SDK_BTN_CIRCLE && strcmp(st.cwd,"/")) {
                    strcpy(st.files[0].name,".."); st.files[0].is_dir=1; st.cursor=0; nav_enter(&st);
                }
                if (btn==PS5SDK_BTN_OPTIONS || btn==SDL_CONTROLLER_BUTTON_BACK ||
                    btn==SDL_CONTROLLER_BUTTON_GUIDE) running=0;
            }
            /* Options straight from the joystick, in case it is not mapped */
            if (ev.type==SDL_JOYBUTTONDOWN && (ev.jbutton.button==9||ev.jbutton.button==6)) running=0;
        }
        render_ui(&v, &st);
    }

    ps5sdk_pad_close(&pad);
    ps5sdk_video_close(&v);
    return 0;
}
