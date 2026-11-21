#pragma once

#include <QColor>
#include <vector>

#include "shaders/unicolorshader.h"
#include "util/class.h"
#include "waveform/renderers/allshader/vertexdata.h"
#include "waveform/renderers/allshader/waveformrenderer.h"

class QDomNode;
class SkinContext;

namespace allshader {
class WaveformRenderBeat;
}

class allshader::WaveformRenderBeat final : public allshader::WaveformRenderer {
  public:
    explicit WaveformRenderBeat(WaveformWidgetRenderer* waveformWidget,
            ::WaveformRendererAbstract::PositionSource type =
                    ::WaveformRendererAbstract::Play);

    void setup(const QDomNode& node, const SkinContext& context) override;
    void paintGL() override;
    void initializeGL() override;

  private:
    // Beat grid line classes, drawn in increasing order of prominence.
    enum Level {
        kBeat = 0,
        kBar = 1,    // every 4 beats (downbeat)
        kPhrase = 2, // every 16 beats
        kLevelCount = 3,
    };
    struct BeatLine {
        float x;
        Level level;
    };

    void drawVertices(const VertexData& vertices, const QColor& baseColor, float alpha);

    mixxx::UnicolorShader m_shader;
    QColor m_color;
    VertexData m_vertices[kLevelCount];
    std::vector<BeatLine> m_lines;

    bool m_isSlipRenderer;

    DISALLOW_COPY_AND_ASSIGN(WaveformRenderBeat);
};
