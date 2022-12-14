/*
  Based on
  https://github.com/audacious-media-player/audacious-plugins/blob/master/src/blur_scope-qt/blur_scope.cc
  Licensed under GPLv3
*/

#include <glib-2.0/glib.h>
#include <libaudcore/i18n.h>
#include <libaudcore/plugin.h>
#include <libaudcore/preferences.h>
#include <libaudcore/runtime.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <Eigen/Dense>
#include <QImage>
#include <QPainter>
#include <QWidget>

#include "libaudqt/colorbutton.h"
#include "sillyscope_config.h"

#define RED(x) (((x) >> 16) & 0xff)
#define GRN(x) (((x) >> 8) & 0xff)
#define BLU(x) ((x)&0xff)

using Eigen::Matrix;
using Eigen::Matrix4f;
using Eigen::RowVector2f;
using Eigen::RowVector4f;
using Eigen::Vector2f;

static void /* QWidget */ *sscope_get_color_chooser();

static int sscope_color;
static bool sscope_left_horiz;

static const PreferencesWidget sscope_widgets[] = {
    WidgetLabel(N_("<b>Color</b>")), WidgetCustomQt(sscope_get_color_chooser),
    WidgetCheck(N_("Left audio channel is horizontal"),
                WidgetBool("sillyscope", "left-horiz",
                           []() { sscope_left_horiz = !sscope_left_horiz; }))};

static const PluginPreferences sscope_prefs = {{sscope_widgets}};

// clang-format off
static const char *const sscope_defaults[] = {
  "color", aud::numeric_string<0x002000>::str, 
  "left-horiz", "TRUE",
  nullptr};
// clang-format on

float lerp(float t, float t0, float t1, float y0, float y1) {
  float dt = t1 - t0;
  return (y0 * (t1 - t) + y1 * (t - t0)) / dt;
}

class SillyScopeWidget : public QWidget {
 public:
  SillyScopeWidget(QWidget *parent = nullptr);
  ~SillyScopeWidget();

  void resize(int w, int h);

  void clear();
  void draw_line(int x0, int y0, int x1, int y1, float brightness);

  void blur();

 protected:
  void resizeEvent(QResizeEvent *);
  void paintEvent(QPaintEvent *);

 private:
  int m_width = 0, m_height = 0, m_image_size = 0;
  uint32_t *m_image = nullptr, *m_corner = nullptr;
};

static SillyScopeWidget *s_widget = nullptr;

SillyScopeWidget::SillyScopeWidget(QWidget *parent) : QWidget(parent) {
  resize(width(), height());
}

SillyScopeWidget::~SillyScopeWidget() {
  g_free(m_image);
  m_image = nullptr;
  s_widget = nullptr;
}

void SillyScopeWidget::paintEvent(QPaintEvent *) {
  QImage img((unsigned char *)m_image, m_width, m_height, QImage::Format_RGB32);
  QPainter p(this);

  p.drawImage(0, 0, img);
}

void SillyScopeWidget::resizeEvent(QResizeEvent *) {
  resize(width(), height());
}

void SillyScopeWidget::resize(int w, int h) {
  m_width = w;
  m_height = h;
  m_image_size = (m_width << 2) * (m_height + 2);
  m_image = (uint32_t *)g_realloc(m_image, m_image_size);
  memset(m_image, 0, m_image_size);
  m_corner = m_image + m_width + 1;
}

void SillyScopeWidget::clear() {
  memset(m_image, 0, m_image_size);
  update();
}

void SillyScopeWidget::blur() {
  // // First fade everything, then blur everything
  // for (int y = 0; y < m_height; y++) {
  //   for (int x = 0; x < m_width; x++) {
  //     uint32_t *p = m_corner + m_width * y + x;
  //     uint32_t col = *p;
  //     int r2 = aud::max((int)RED(col) - 20, 0);
  //     int g2 = aud::max((int)GRN(col) - 10, 0);
  //     int b2 = aud::max((int)BLU(col) - 20, 0);
  //     *p = (r2 << 16) | (g2 << 8) | b2;
  //   }
  // }

  for (int y = 0; y < m_height; y++) {
    uint32_t *p = m_corner + m_width * y;
    uint32_t *end = p + m_width;
    uint32_t *plast = p - m_width;
    uint32_t *pnext = p + m_width;

    // not sure how this evil pointer bullshit works tbh
    for (; p < end; p++) {
      uint32_t c = *p;
      uint32_t o0 = *plast++;
      uint32_t o1 = p[-1];
      uint32_t o2 = p[1];
      uint32_t o3 = *pnext++;

      int r2 = (5 * RED(c) + RED(o0) + RED(o1) + RED(o2) + RED(o3)) / 30;
      int g2 = (5 * GRN(c) + GRN(o0) + GRN(o1) + GRN(o2) + GRN(o3)) / 30;
      int b2 = (5 * BLU(c) + BLU(o0) + BLU(o1) + BLU(o2) + BLU(o3)) / 30;
      *p = ((r2 & 0xff) << 16) | ((g2 & 0xff) << 8) | (b2 & 0xff);
    }
  }
}
void SillyScopeWidget::draw_line(int x0, int y0, int x1, int y1,
                                 float brightness) {
  // thanks, mr bresenham
  // https://en.wikipedia.org/wiki/Bresenham%27s_line_algorithm

  int x = x0;
  int y = y0;
  int dx = aud::abs(x1 - x0);
  int sx = (x0 < x1) ? 1 : -1;
  int dy = -aud::abs(y1 - y0);
  int sy = (y0 < y1) ? 1 : -1;
  int error = dx + dy;

  for (;;) {
    uint32_t *pixel = m_corner + (y * m_width + x);
    float r = RED(*pixel) + RED(sscope_color) * brightness;
    float g = GRN(*pixel) + GRN(sscope_color) * brightness;
    float b = BLU(*pixel) + BLU(sscope_color) * brightness;
    *pixel = (((int)aud::min(r, 255.0f)) << 16 |
              ((int)aud::min(g, 255.0f)) << 8 | ((int)aud::min(b, 255.0f)));

    if (x == x1 && y == y1)
      break;

    int e2 = 2 * error;
    if (e2 >= dy) {
      if (x == x1)
        break;
      error += dy;
      x += sx;
    }
    if (e2 <= dx) {
      if (y == y1)
        break;
      error += dx;
      y += sy;
    }
  }
}

class SillyScopeQt : public VisPlugin {
 public:
  static constexpr PluginInfo info = {N_("Silly Scope"), PACKAGE, nullptr,
                                      &sscope_prefs, PluginQtOnly};

  constexpr SillyScopeQt() : VisPlugin(info, Visualizer::MultiPCM) {}

  bool init();
  void cleanup();

  void *get_qt_widget();

  void clear();
  void render_multi_pcm(const float *pcm, int channels);

  void toggle_left_horiz();
};

EXPORT SillyScopeQt aud_plugin_instance;

bool SillyScopeQt::init() {
  aud_config_set_defaults("SillyScope", sscope_defaults);
  sscope_color = aud_get_int("SillyScope", "color");
  sscope_left_horiz = aud_get_bool("SillyScope", "left-horiz");

  return true;
}

void SillyScopeQt::cleanup() {
  aud_set_int("SillyScope", "color", sscope_color);
  aud_set_bool("SillyScope", "left-horiz", sscope_left_horiz);
}

void SillyScopeQt::clear() {
  if (s_widget)
    s_widget->clear();
}

void get_horz_vert(const float *pcm, int channels, int idx, float *horz_out,
                   float *vert_out) {
  if (channels == 1) {
    // well then, oh dear, um. hm.
    *horz_out = pcm[idx];
    *vert_out = 0.5;
  } else {
    // we might have some terrible 3 or more channels
    // in this case we only use the first two
    float left = pcm[channels * idx];
    float right = pcm[channels * idx + 1];
    if (sscope_left_horiz) {
      *horz_out = left;
      *vert_out = right;
    } else {
      *vert_out = left;
      *horz_out = right;
    }
  }
}

void SillyScopeQt::render_multi_pcm(const float *pcm, int channels) {
  // Matrix4f BEAM_SPLINE_MAT{
  //     {1, 4, 1, 0}, {-3, 0, 3, 0}, {3, -6, 3, 0}, {-1, 3, -3, 1}};
  // BEAM_SPLINE_MAT *= 1.0 / 6.0;
  Matrix4f BEAM_SPLINE_MAT{
      {0, 2, 0, 0}, {-1, 0, 1, 0}, {2, -5, 4, -1}, {-1, 3, -3, 1}};
  BEAM_SPLINE_MAT *= 0.5;

  g_assert(s_widget);

  s_widget->blur();

  int width = s_widget->width();
  int height = s_widget->height();

  for (int i = 0; i < 511; i++) {
    // do a catmull-rom spline
    float x0, x1, x2, x3;
    float y0, y1, y2, y3;

    get_horz_vert(pcm, channels, i, &x1, &y1);
    get_horz_vert(pcm, channels, i + 1, &x2, &y2);
    if (i > 0) {
      get_horz_vert(pcm, channels, i - 1, &x0, &y0);
    } else {
      x0 = x1 - x2;
      y0 = y1 - y2;
    }
    if (i < 510) {
      get_horz_vert(pcm, channels, i + 1, &x3, &y3);
    } else {
      x3 = x2 - x1;
      y3 = y2 - y1;
    }

    // Smooth some more
    // float idealCenterX = (x0 + x3) / 2.0;
    // float idealCenterY = (y0 + y3) / 2.0;
    // x1 = (x1 * 2 + idealCenterX) / 3;
    // x2 = (x2 * 2 + idealCenterX) / 3;
    // y1 = (y1 * 2 + idealCenterY) / 3;
    // y2 = (y2 * 2 + idealCenterY) / 3;

    Matrix<float, 4, 2> points{{x0, y0}, {x1, y1}, {x2, y2}, {x3, y3}};

    RowVector2f pt = {x0, y0};
    int steps = 8;
    for (int t_frac = 0; t_frac <= steps; t_frac++) {
      float t = ((float)t_frac) / (float)steps;

      float volx0 = pt(0);
      float voly0 = pt(1);

      RowVector4f ts{1, t, t * t, t * t * t};
      pt = ts * BEAM_SPLINE_MAT * points;

      float volx1 = pt(0);
      float voly1 = pt(1);

      int px0 = aud::clamp((int)((0.5 + volx0 / 2.0) * width), 0, width - 1);
      int py0 = aud::clamp((int)((0.5 - voly0 / 2.0) * height), 0, height - 1);
      int px1 = aud::clamp((int)((0.5 + volx1 / 2.0) * width), 0, width - 1);
      int py1 = aud::clamp((int)((0.5 - voly1 / 2.0) * height), 0, height - 1);

      float dist =
          std::sqrt(std::pow(volx0 - volx1, 2) + std::pow(voly0 - voly1, 2));
      float brightness =
          aud::clamp(lerp(dist, 0, 1.0 / (float)steps, 1, 0), 0.0f, 1.0f);

      s_widget->draw_line(px0, py0, px1, py1, brightness);
    }
  }

  s_widget->update();
}

void *SillyScopeQt::get_qt_widget() {
  if (s_widget)
    return s_widget;

  s_widget = new SillyScopeWidget();
  return s_widget;
}

class ColorChooserWidget : public audqt::ColorButton {
 protected:
  void onColorChanged() override;
};

void ColorChooserWidget::onColorChanged() {
  QColor col(color());
  int r, g, b;

  col.getRgb(&r, &g, &b);

  sscope_color = r << 16 | g << 8 | b;
}

static void *sscope_get_color_chooser() {
  int r = RED(sscope_color);
  int g = GRN(sscope_color);
  int b = BLU(sscope_color);

  QColor color = QColor::fromRgb(r, g, b);

  auto chooser = new ColorChooserWidget();
  chooser->setColor(color);

  return chooser;
}

#undef RED
#undef GRN
#undef BLU
