// ============================================================
//  冰晶蓝玫瑰 · Ice Crystal Rose
//  A 3D rotating particle rose rendered with OpenGL (fixed pipeline).
//  Pure Win32 + system OpenGL, NO third-party dependency (no EasyX needed).
//
//  Build:
//    - Open "冰晶蓝玫瑰.sln" in Visual Studio 2022 and press F5, or
//    - Use MSBuild:
//        MSBuild 冰晶蓝玫瑰.sln /t:Build /p:Configuration=Release /p:Platform=x64
//
//  Usage:
//    IceRose.exe                -> animated rotating rose
//    IceRose.exe -shot a.bmp    -> render one frame and save as BMP (for preview)
//    IceRose.exe -shot a.bmp 35 -> render one frame with yaw = 35 degrees
// ============================================================

#include <windows.h>
#include <gl/gl.h>
#include <gl/glu.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>
#include <algorithm>

#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "glu32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

#define _USE_MATH_DEFINES
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const int DEF_W = 1200;
static const int DEF_H = 800;

static HDC   g_hDC = nullptr;
static HGLRC g_hRC = nullptr;

// ------------------------------------------------------------
//  Particle
// ------------------------------------------------------------
struct Particle {
    float x, y, z;
    unsigned char r, g, b;
    float size;          // point size
    float alpha;         // additive-blend alpha
    float twPhase;       // twinkle phase (stars)
    float twSpeed;       // twinkle speed (stars)
};

static std::vector<Particle> g_rose;    // rose body particles
static std::vector<Particle> g_stars;   // background stars

static float g_angleY = 0.0f;
static float g_angleX = 20.0f;
static float g_scale  = 0.62f;          // global size of the rose

// ------------------------------------------------------------
//  Helpers
// ------------------------------------------------------------
static float frand() { return (float)rand() / (float)RAND_MAX; }

static void pushParticle(std::vector<Particle>& v, float x, float y, float z,
                         int r, int g, int b, float size, float alpha,
                         float twPhase = 0.f, float twSpeed = 0.f)
{
    Particle p;
    p.x = x; p.y = y; p.z = z;
    p.r = (unsigned char)(r < 0 ? 0 : (r > 255 ? 255 : r));
    p.g = (unsigned char)(g < 0 ? 0 : (g > 255 ? 255 : g));
    p.b = (unsigned char)(b < 0 ? 0 : (b > 255 ? 255 : b));
    p.size = size;
    p.alpha = alpha;
    p.twPhase = twPhase;
    p.twSpeed = twSpeed;
    v.push_back(p);
}

// Ice-blue gradient: s = 0 (heart of the flower) -> 1 (petal tip)
static void iceColor(float s, float light, int& r, int& g, int& b)
{
    float R = 42.f, G = 116.f, B = 232.f;
    float t = s;
    R += (210.f - R) * t;
    G += (234.f - G) * t;
    B += (250.f - B) * t;
    if (s > 0.75f) {                       // petal tips catch the light
        float w = (s - 0.75f) / 0.25f * 0.50f;
        R += w * 78.f; G += w * 50.f; B += w * 32.f;
    }
    R *= light; G *= light; B *= light;
    float j = (frand() - 0.5f) * 20.f;
    r = (int)(R + j); g = (int)(G + j); b = (int)(B + j);
}

// ------------------------------------------------------------
//  Build the rose geometry (particle cloud)
// ------------------------------------------------------------
static void buildRose()
{
    // ---- spiral crystal heart (signature rose centre) -----------
    for (int i = 0; i < 1400; ++i) {
        float f = i / 1400.f;
        float r = 0.05f + 0.52f * f;
        float th = 6.2832f * 4.2f * f + 0.30f * sinf(6.2832f * f);
        float y = 0.28f * f * f;                     // gentle central mound
        y += 0.07f * sinf(10.f * th);                // petal-like ripples
        float wv = 0.04f + 0.14f * f;                // spiral band width
        float tw = (frand() - 0.5f) * 2.f * wv;
        float x = (r + tw * 0.30f) * cosf(th);
        float z = (r + tw * 0.30f) * sinf(th);
        y += tw * 0.12f;
        float lf = 1.00f - 0.32f * f;
        int rr = (int)(182.f * lf);
        int gg = (int)(220.f * lf);
        int bb = 255;
        pushParticle(g_rose, x, y, z, rr, gg, bb, 1.5f, 0.50f);
    }

    // ---- petals: three blooming layers --------------------------
    struct LayerDef {
        int   count;    // petals in this layer
        float r0;       // start radius (from flower centre)
        float len;      // petal length
        float w;        // petal half width
        float tilt;     // base tilt angle (deg, from horizontal)
        float droop;    // tilt change toward the tip (deg, negative = droop)
        float start;    // azimuth offset (deg)
        float curl;     // petal edge curl (bowl shape)
        float light;    // layer brightness
        float spiral;   // per-petal spiral twist (deg)
        bool  wave;     // wavy petal edge
    };
    LayerDef L[3] = {
        { 10, 0.90f, 1.75f, 0.95f,  72.f, -18.f, 10.f, 0.20f, 1.14f, 10.f, false },  // inner, half-closed bud around the heart
        {  8, 1.45f, 3.40f, 1.30f,  38.f, -32.f, 22.f, 0.26f, 1.04f, -8.f, true  },  // mid, spread
        {  8, 2.15f, 4.80f, 1.55f,  16.f, -22.f,  0.f, 0.34f, 0.94f,  6.f, true  },  // outer, open cup with ruffled rim
    };

    for (int layer = 0; layer < 3; ++layer) {
        // outer petals are much larger, sample them more densely
        int siMax = (layer == 2) ? 55 : 40;
        int tiMax = (layer == 2) ? 30 : 21;
        for (int p = 0; p < L[layer].count; ++p) {
            float phi0 = (p * 360.f / L[layer].count + L[layer].start
                          + p * L[layer].spiral) * (float)M_PI / 180.f;
            // alternate brightness between neighbouring petals
            float petalShade = 0.92f + 0.16f * ((p + layer) % 3);
            for (int si = 0; si <= siMax; ++si) {
                float s = si / (float)siMax;
                for (int ti = 0; ti <= tiMax; ++ti) {
                    float t = -1.f + 2.f * ti / (float)tiMax;

                    // soft, fading petal edges reveal the layer beneath
                    if (fabsf(t) > 0.80f && frand() < 0.30f) continue;

                    // petal surface in local coords:
                    //   u = length direction, v = width, w = thickness
                    float u = L[layer].r0 + s * L[layer].len;
                    // broad-oval petal profile: narrow base, rounded tip
                    float v = t * L[layer].w * powf(sinf((float)M_PI * s), 0.52f);
                    // thickness: bowl-shaped curl + gentle arch
                    float curlw = -L[layer].curl * (t * t) * s;
                    float archw = 0.10f * sinf((float)M_PI * s) * (1.f - t * t);
                    float wl = curlw * (0.50f + 0.50f * sinf((float)M_PI * s)) + archw;
                    if (L[layer].wave)                    // wavy ruffled edge
                        wl += 0.20f * sinf(3.f * (float)M_PI * (t + 0.5f)) * s * s;

                    // tilt (varies along the petal so the tip flips outward)
                    float a = (L[layer].tilt + L[layer].droop * s)
                              * (float)M_PI / 180.f;
                    float u2 = u * cosf(a) - wl * sinf(a);
                    float w2 = u * sinf(a) + wl * cosf(a);

                    float x = u2 * cosf(phi0) - v * sinf(phi0);
                    float z = u2 * sinf(phi0) + v * cosf(phi0);
                    float y = w2;

                    x += (frand() - 0.5f) * 0.03f;
                    y += (frand() - 0.5f) * 0.03f;
                    z += (frand() - 0.5f) * 0.03f;

                    int r, g, b;
                    float light = L[layer].light * petalShade
                                  * (0.62f + 0.38f * sinf((float)M_PI * powf(s, 0.85f)));
                    bool edge = fabsf(t) > 0.72f;
                    if (edge) light *= 1.18f;             // curled edge glints
                    iceColor(s, light, r, g, b);

                    float size = 1.2f + 0.9f * s + (frand() - 0.5f) * 0.5f;
                    if (edge) size += 0.5f;
                    float alpha = (layer >= 2) ? 0.58f : 0.62f;
                    // crystal sparkle points on the petal
                    if (frand() < 0.10f) {
                        r = (int)(234 + frand() * 21.f);
                        g = (int)(245 + frand() * 10.f);
                        b = 255;
                        size += 1.0f;
                    }
                    pushParticle(g_rose, x, y, z, r, g, b, size, alpha);
                }
            }
        }
    }

    // ---- stamens (short, tucked inside the spiral heart) --------
    for (int st = 0; st < 44; ++st) {
        float ang = (st * 137.5f) * (float)M_PI / 180.f;   // golden angle
        float tilt = 12.f + (st % 3) * 6.f;
        float a = tilt * (float)M_PI / 180.f;
        float len = 0.30f + (st % 5) * 0.07f;
        for (int k = 1; k <= 5; ++k) {
            float f = k / 5.f;
            float u = 0.20f + f * len;
            float w2 = u * sinf(a);
            float u2 = u * cosf(a);
            float x = u2 * cosf(ang) + (frand() - 0.5f) * 0.03f;
            float z = u2 * sinf(ang) + (frand() - 0.5f) * 0.03f;
            float y = w2 + (frand() - 0.5f) * 0.03f;
            float lf = 0.85f + 0.15f * f;
            int rr = (int)(210.f * lf);
            int gg = (int)(232.f * lf);
            int bb = 255;
            pushParticle(g_rose, x, y, z, rr, gg, bb, 1.0f, 0.55f);
        }
    }

    // ---- flower heart (tiny bright core) -------------------------
    for (int i = 0; i < 24; ++i) {
        float theta = i / 24.f * 2.f * (float)M_PI;
        for (int j = 0; j < 16; ++j) {
            float phi = j / 16.f * (float)M_PI;      // 0 = top, pi = bottom
            float r = 0.52f * (1.f + (frand() - 0.5f) * 0.05f);
            float x = r * sinf(phi) * cosf(theta);
            float y = r * cosf(phi);
            float z = r * sinf(phi) * sinf(theta);
            float light = 0.80f + 0.40f * sinf(phi);
            int rr = (int)(168.f * light);
            int gg = (int)(210.f * light);
            int bb = (int)(255.f * light);
            pushParticle(g_rose, x, y, z, rr, gg, bb, 1.7f, 0.80f);
        }
    }

    // ---- sepals (ice-green pointed sepals under the bloom) ------
    for (int sp = 0; sp < 5; ++sp) {
        float ph = (sp * 72.f + 20.f) * (float)M_PI / 180.f;
        float droop = -28.f * (float)M_PI / 180.f;
        for (int si = 0; si <= 14; ++si) {
            float s = si / 14.f;
            for (int ti = 0; ti <= 5; ++ti) {
                float t = -1.f + 2.f * ti / 5.f;
                float u = 0.40f + s * 0.85f;
                float v = t * 0.38f * sinf((float)M_PI * powf(s, 0.70f));
                float w = (frand() - 0.5f) * 0.04f;
                float u2 = u * cosf(droop) - w * sinf(droop);
                float w2 = u * sinf(droop) + w * cosf(droop);
                float x = u2 * cosf(ph) - v * sinf(ph);
                float z = u2 * sinf(ph) + v * cosf(ph);
                float y = -0.62f + w2;
                int rr = (int)(70 + (frand() - 0.5f) * 18.f);
                int gg = (int)(150 + (frand() - 0.5f) * 26.f);
                int bb = (int)(205 + (frand() - 0.5f) * 24.f);
                pushParticle(g_rose, x, y, z, rr, gg, bb, 1.4f, 0.65f);
            }
        }
    }

    // ---- stem (short, slight S curve) ---------------------------
    const int stemSeg = 38;
    for (int i = 0; i <= stemSeg; ++i) {
        float f = i / (float)stemSeg;
        float y = -0.80f - f * 2.50f;
        float bend = 0.28f * sinf(f * 2.2f);
        float rad = 0.18f * (1.f - 0.30f * f);
        for (int k = 0; k < 10; ++k) {
            float ang = k / 10.f * 2.f * (float)M_PI;
            float jx = (frand() - 0.5f) * 0.02f;
            float jz = (frand() - 0.5f) * 0.02f;
            float x = bend + (rad + jx) * cosf(ang);
            float z = (rad + jz) * sinf(ang);
            int rr = (int)(38 + (frand() - 0.5f) * 14.f);
            int gg = (int)(90 + (frand() - 0.5f) * 16.f);
            int bb = (int)(180 + (frand() - 0.5f) * 18.f);
            pushParticle(g_rose, x, y, z, rr, gg, bb, 1.5f, 0.70f);
        }
    }

    // ---- leaves (4, two pairs at different heights) -------------
    const float leafH[4]   = { -1.55f, -1.90f, -2.45f, -2.80f };
    const float leafPhi[4] = { 35.f, 215.f, 150.f, 330.f };
    for (int li = 0; li < 4; ++li) {
        float ph = leafPhi[li] * (float)M_PI / 180.f;
        for (int si = 0; si <= 13; ++si) {
            float s = si / 13.f;
            for (int ti = 0; ti <= 9; ++ti) {
                float t = -1.f + 2.f * ti / 9.f;
                float u = s * 1.7f;
                float v = t * 0.72f * sinf((float)M_PI * powf(s, 0.80f));
                float w = (frand() - 0.5f) * 0.05f;
                float a = -30.f * (float)M_PI / 180.f;   // droop
                float u2 = u * cosf(a) - w * sinf(a);
                float w2 = u * sinf(a) + w * cosf(a);
                float x = u2 * cosf(ph) - v * sinf(ph);
                float z = u2 * sinf(ph) + v * cosf(ph);
                float y = leafH[li] + w2;
                float lf = 0.75f + 0.25f * sinf((float)M_PI * s);
                int rr = (int)((78 + (frand() - 0.5f) * 22.f) * lf);
                int gg = (int)((172 + (frand() - 0.5f) * 26.f) * lf);
                int bb = (int)((216 + (frand() - 0.5f) * 24.f) * lf);
                pushParticle(g_rose, x, y, z, rr, gg, bb, 1.4f, 0.68f);
            }
        }
    }

    // ---- background stars ---------------------------------------
    srand(20260918);   // fixed seed: star positions stable
    for (int i = 0; i < 170; ++i) {
        float theta = frand() * 2.f * (float)M_PI;
        float phi   = acosf(2.f * frand() - 1.f);
        float r     = 26.f + frand() * 14.f;
        float x = r * sinf(phi) * cosf(theta);
        float y = r * cosf(phi) * 0.7f;
        float z = r * sinf(phi) * sinf(theta);
        float l  = 0.55f + 0.45f * frand();
        pushParticle(g_stars, x, y, z,
                     (int)(150.f * l), (int)(205.f * l), 255,
                     1.3f + frand(), 0.75f,
                     frand() * 6.28f, 0.6f + frand() * 1.6f);
    }
}

// ------------------------------------------------------------
//  Per-frame view-space transform + depth sort
// ------------------------------------------------------------
struct ViewPt {
    float x, y, z;
    unsigned char r, g, b;
    float size, alpha;
};
static std::vector<ViewPt> g_view;

static void transformRose()
{
    float yaw   = g_angleY * (float)M_PI / 180.f;
    float pitch = g_angleX * (float)M_PI / 180.f;
    float cy = cosf(yaw), sy = sinf(yaw);
    float cx = cosf(pitch), sx = sinf(pitch);

    size_t n = g_rose.size();
    g_view.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const Particle& p = g_rose[i];
        float x1 = p.x * cy + p.z * sy;
        float z1 = -p.x * sy + p.z * cy;
        float y1 = p.y;
        float y2 = y1 * cx - z1 * sx;
        float z2 = y1 * sx + z1 * cx;
        ViewPt& v = g_view[i];
        v.x = x1 * g_scale;
        v.y = y2 * g_scale + 0.35f;    // lift the rose a bit in the frame
        v.z = z2 * g_scale;
        v.r = p.r; v.g = p.g; v.b = p.b;
        v.size = p.size; v.alpha = p.alpha;
    }
    // painter's algorithm: far first (camera looks along +z)
    std::sort(g_view.begin(), g_view.end(),
              [](const ViewPt& a, const ViewPt& b) { return a.z > b.z; });
}

// ------------------------------------------------------------
//  Rendering
// ------------------------------------------------------------
static void renderFrame(float t, int w, int h)
{
    glViewport(0, 0, w, h);
    glClearColor(0.016f, 0.042f, 0.105f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(44.0, (double)w / (double)h, 0.1, 140.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(0.0, 1.60, 8.0, 0.0, 0.05, 0.0, 0.0, 1.0, 0.0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // translucent solid surface
    glEnable(GL_POINT_SMOOTH);
    glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);

    // stars stay fixed in the sky
    glBegin(GL_POINTS);
    for (size_t i = 0; i < g_stars.size(); ++i) {
        const Particle& s = g_stars[i];
        float tw = 0.5f + 0.5f * sinf(t * s.twSpeed + s.twPhase);
        glPointSize(s.size);
        glColor4f(s.r / 255.f, s.g / 255.f, s.b / 255.f,
                  s.alpha * (0.40f + 0.60f * tw));
        glVertex3f(s.x, s.y, s.z);
    }
    glEnd();

    // rose: transform + depth sort, far particles drawn darker
    transformRose();
    float zMin = g_view.front().z, zMax = g_view.front().z;
    for (size_t i = 1; i < g_view.size(); ++i) {
        float z = g_view[i].z;
        if (z < zMin) zMin = z;
        if (z > zMax) zMax = z;
    }
    float zSpan = zMax - zMin;
    if (zSpan < 1e-5f) zSpan = 1.f;
    float breath = 1.0f + 0.10f * sinf(t * 2.1f);   // subtle breathing size

    // draw in 4 size buckets so glPointSize changes rarely;
    // the array stays depth-sorted inside each bucket
    for (int bucket = 0; bucket < 4; ++bucket) {
        float bSize = 1.3f + bucket * 0.7f;
        glPointSize(bSize * breath);
        glBegin(GL_POINTS);
        for (size_t i = 0; i < g_view.size(); ++i) {
            const ViewPt& v = g_view[i];
            int b = (int)((v.size - 1.0f) / 0.8f);
            if (b < 0) b = 0;
            if (b > 3) b = 3;
            if (b != bucket) continue;
            float df = 1.0f - 0.50f * (v.z - zMin) / zSpan;   // far = darker
            glColor4f(v.r / 255.f * df, v.g / 255.f * df, v.b / 255.f * df, v.alpha);
            glVertex3f(v.x, v.y, v.z);
        }
        glEnd();
    }
}

// ------------------------------------------------------------
//  Screenshot: save current back buffer as 24-bit BMP
// ------------------------------------------------------------
static void saveBmp(const wchar_t* path, int w, int h)
{
    std::vector<unsigned char> px((size_t)w * h * 3);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());

    for (size_t i = 0; i < px.size(); i += 3)
        std::swap(px[i], px[i + 2]);          // RGB -> BGR

    int rowSize = ((w * 3 + 3) / 4) * 4;
    int dataSize = rowSize * h;

    unsigned char hdr[54] = { 0 };
    hdr[0] = 'B'; hdr[1] = 'M';
    *(int*)(hdr + 2)  = 54 + dataSize;
    *(int*)(hdr + 10) = 54;
    *(int*)(hdr + 14) = 40;
    *(int*)(hdr + 18) = w;
    *(int*)(hdr + 22) = h;
    *(short*)(hdr + 26) = 1;
    *(short*)(hdr + 28) = 24;
    *(int*)(hdr + 34) = dataSize;

    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"wb") != 0) return;
    fwrite(hdr, 1, 54, f);
    std::vector<unsigned char> row((size_t)rowSize, 0);
    for (int y = 0; y < h; ++y) {
        memcpy(row.data(), px.data() + (size_t)y * w * 3, (size_t)w * 3);
        fwrite(row.data(), 1, (size_t)rowSize, f);
    }
    fclose(f);
}

// ------------------------------------------------------------
//  Window
// ------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) { PostQuitMessage(0); return 0; }
        break;
    case WM_ERASEBKGND:
        return 1;                       // avoid flicker
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

static bool setupOpenGL(HWND hwnd)
{
    g_hDC = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd = { 0 };
    pfd.nSize        = sizeof(pfd);
    pfd.nVersion     = 1;
    pfd.dwFlags      = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType   = PFD_TYPE_RGBA;
    pfd.cColorBits   = 24;
    pfd.cDepthBits   = 24;
    pfd.iLayerType   = PFD_MAIN_PLANE;
    int pf = ChoosePixelFormat(g_hDC, &pfd);
    if (!pf) return false;
    SetPixelFormat(g_hDC, pf, &pfd);
    g_hRC = wglCreateContext(g_hDC);
    if (!g_hRC) return false;
    wglMakeCurrent(g_hDC, g_hRC);
    return true;
}

// ------------------------------------------------------------
//  Entry
// ------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nShow)
{
    // parse command line: -shot <bmp> [yaw]
    int argc = 0;
    LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool shotMode = false;
    const wchar_t* shotFile = nullptr;
    float shotYaw = 35.f;
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argvW[i], L"-shot") == 0 && i + 1 < argc) {
            shotMode = true;
            shotFile = argvW[i + 1];
            if (i + 2 < argc && argvW[i + 2][0] != L'-')
                shotYaw = (float)_wtof(argvW[i + 2]);
        }
    }

    SetProcessDPIAware();

    WNDCLASSW wc = { 0 };
    wc.style         = CS_OWNDC;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = L"IceRoseWin";
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon         = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassW(&wc);

    RECT rc = { 0, 0, DEF_W, DEF_H };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowW(L"IceRoseWin",
                              L"\u51B0\u6676\u84DD\u73AB\u7470  Ice Crystal Rose",
                              WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT,
                              rc.right - rc.left, rc.bottom - rc.top,
                              nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 1;
    if (!setupOpenGL(hwnd)) {
        MessageBoxW(hwnd, L"OpenGL initialization failed.", L"Ice Rose", MB_ICONERROR);
        return 1;
    }

    srand((unsigned)time(nullptr));
    buildRose();

    if (shotMode) {
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        MSG m;
        while (PeekMessageW(&m, hwnd, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        g_angleY = shotYaw;
        renderFrame(0.f, DEF_W, DEF_H);
        saveBmp(shotFile, DEF_W, DEF_H);
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(g_hRC);
        DestroyWindow(hwnd);
        return 0;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    LARGE_INTEGER freq, last;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&last);
    float tt = 0.f;
    float fpsAcc = 0.f;
    int   fpsFrames = 0;

    MSG msg;
    for (;;) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto done;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        float dt = (float)(now.QuadPart - last.QuadPart) / (float)freq.QuadPart;
        last = now;
        if (dt > 0.1f) dt = 0.1f;

        tt += dt;
        g_angleY += dt * 22.f;                       // rotation speed
        g_angleX  = 20.f + 5.f * sinf(tt * 0.55f);   // gentle sway

        fpsAcc += dt; fpsFrames++;
        if (fpsAcc >= 1.f) {
            wchar_t buf[96];
            swprintf_s(buf, 96, L"\u51B0\u6676\u84DD\u73AB\u7470  Ice Crystal Rose  -  %d FPS",
                       (int)(fpsFrames / fpsAcc));
            SetWindowTextW(hwnd, buf);
            fpsAcc = 0.f; fpsFrames = 0;
        }

        RECT cr;
        GetClientRect(hwnd, &cr);
        int w = cr.right - cr.left, h = cr.bottom - cr.top;
        if (w < 1) w = 1;
        if (h < 1) h = 1;
        renderFrame(tt, w, h);
        SwapBuffers(g_hDC);

        float target = 1.f / 60.f;
        if (dt < target) Sleep((DWORD)((target - dt) * 1000.f));
    }

done:
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(g_hRC);
    return 0;
}
