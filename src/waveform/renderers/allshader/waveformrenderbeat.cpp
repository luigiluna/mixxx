#include "waveform/renderers/allshader/waveformrenderbeat.h"

#include <QDomNode>
#include <algorithm>
#include <cmath>

#include "skin/legacy/skincontext.h"
#include "track/track.h"
#include "waveform/renderers/allshader/matrixforwidgetgeometry.h"
#include "waveform/renderers/waveformwidgetrenderer.h"
#include "widget/wskincolor.h"

namespace allshader {

WaveformRenderBeat::WaveformRenderBeat(WaveformWidgetRenderer* waveformWidget,
        ::WaveformRendererAbstract::PositionSource type)
        : WaveformRenderer(waveformWidget),
          m_isSlipRenderer(type == ::WaveformRendererAbstract::Slip) {
}

void WaveformRenderBeat::initializeGL() {
    WaveformRenderer::initializeGL();
    m_shader.init();
}

void WaveformRenderBeat::setup(const QDomNode& node, const SkinContext& context) {
    m_color = QColor(context.selectString(node, "BeatColor"));
    m_color = WSkinColor::getCorrectColor(m_color).toRgb();
}

namespace {

// Adaptive density: a line class is fully visible when its lines are at least
// kFullSpacingPx apart and hidden below kMinSpacingPx, with a linear fade in
// between so zooming does not make lines pop in and out.
constexpr float kMinSpacingPx = 4.f;
constexpr float kFullSpacingPx = 10.f;
// Height of a tick (Ticks / Hybrid styles) relative to the renderer breadth.
constexpr float kTickFraction = 0.12f;
constexpr float kMinTickHeightPx = 4.f;

constexpr int kBeatsPerBar = 4;
constexpr int kBeatsPerPhrase = 16;

float fadeForSpacing(float spacingPx) {
    return std::clamp((spacingPx - kMinSpacingPx) / (kFullSpacingPx - kMinSpacingPx), 0.f, 1.f);
}

int positiveModulo(int value, int divisor) {
    const int result = value % divisor;
    return result < 0 ? result + divisor : result;
}

} // namespace

void WaveformRenderBeat::paintGL() {
    TrackPointer trackInfo = m_waveformRenderer->getTrackInfo();

    if (!trackInfo || (m_isSlipRenderer && !m_waveformRenderer->isSlipActive())) {
        return;
    }

    auto positionType = m_isSlipRenderer ? ::WaveformRendererAbstract::Slip
                                         : ::WaveformRendererAbstract::Play;

    mixxx::BeatsPointer trackBeats = trackInfo->getBeats();
    if (!trackBeats) {
        return;
    }

    int alpha = m_waveformRenderer->getBeatGridAlpha();
    if (alpha == 0) {
        return;
    }

    const float devicePixelRatio = m_waveformRenderer->getDevicePixelRatio();

    const double trackSamples = m_waveformRenderer->getTrackSamples();
    if (trackSamples <= 0) {
        return;
    }

    const double firstDisplayedPosition =
            m_waveformRenderer->getFirstDisplayedPosition(positionType);
    const double lastDisplayedPosition =
            m_waveformRenderer->getLastDisplayedPosition(positionType);

    const auto startPosition = mixxx::audio::FramePos::fromEngineSamplePos(
            firstDisplayedPosition * trackSamples);
    const auto endPosition = mixxx::audio::FramePos::fromEngineSamplePos(
            lastDisplayedPosition * trackSamples);

    if (!startPosition.isValid() || !endPosition.isValid()) {
        return;
    }

    const bool adaptive = m_waveformRenderer->isBeatGridAdaptive();
    const auto style = m_waveformRenderer->getBeatGridStyle();

    // Collect the visible beats with their position in the bar/phrase.
    // By Mixxx convention the first marker of the grid is a downbeat.
    m_lines.clear();
    auto it = trackBeats->iteratorFrom(startPosition);
    if (it == trackBeats->cend()) {
        return;
    }
    int beatIndex = it - trackBeats->cfirstmarker();
    for (; it != trackBeats->cend() && *it <= endPosition; ++it, ++beatIndex) {
        double beatPosition = it->toEngineSamplePos();
        double xBeatPoint =
                m_waveformRenderer->transformSamplePositionInRendererWorld(
                        beatPosition, positionType);

        xBeatPoint = qRound(xBeatPoint * devicePixelRatio) / devicePixelRatio;

        Level level = kBeat;
        if (positiveModulo(beatIndex, kBeatsPerPhrase) == 0) {
            level = kPhrase;
        } else if (positiveModulo(beatIndex, kBeatsPerBar) == 0) {
            level = kBar;
        }
        m_lines.push_back(BeatLine{static_cast<float>(xBeatPoint), level});
    }

    if (m_lines.empty()) {
        return;
    }

    // Opacity of each line class.
    float levelAlpha[kLevelCount] = {
            m_waveformRenderer->getBeatGridBeatAlpha() / 100.f, 1.f, 1.f};
    if (adaptive) {
        // Average distance between two consecutive beats on screen.
        float beatSpacingPx = kFullSpacingPx * kBeatsPerPhrase;
        if (m_lines.size() >= 2) {
            beatSpacingPx = std::abs(m_lines.back().x - m_lines.front().x) /
                    static_cast<float>(m_lines.size() - 1);
        }
        levelAlpha[kBeat] *= fadeForSpacing(beatSpacingPx);
        levelAlpha[kBar] = fadeForSpacing(beatSpacingPx * kBeatsPerBar);
        // Phrases always stay visible so there is some orientation even at
        // the widest zoom.
        levelAlpha[kPhrase] = 1.f;
    }

    const float rendererBreadth = m_waveformRenderer->getBreadth();
    const float lineBottom = m_isSlipRenderer ? rendererBreadth / 2 : rendererBreadth;
    const float tickHeight = std::min(lineBottom / 2,
            std::max(kMinTickHeightPx, lineBottom * kTickFraction));

    const bool levelIsTicks[kLevelCount] = {
            style != WaveformWidgetRenderer::BeatGridStyle::Lines,
            style == WaveformWidgetRenderer::BeatGridStyle::Ticks,
            style == WaveformWidgetRenderer::BeatGridStyle::Ticks,
    };
    // Optional accent color for bars and 16-beat markers; the skin color is
    // used when it is not set. Ordinary beats always use the skin color.
    const QColor& accentColor = m_waveformRenderer->getBeatGridAccentColor();
    const bool hasAccent = accentColor.isValid();
    const QColor levelColor[kLevelCount] = {
            m_color,
            hasAccent ? accentColor : m_color,
            hasAccent ? accentColor : m_color,
    };

    // Phrase lines are 2px wide in adaptive mode to stand out when zoomed out.
    // With an accent color bars and 16-beat markers share the color, so the
    // width is also needed to tell them apart without adaptive density.
    const float levelWidth[kLevelCount] = {1.f, 1.f, (adaptive || hasAccent) ? 2.f : 1.f};
    // With an accent color, ticks get longer for more prominent levels so the
    // hierarchy does not depend on the color alone.
    const float levelTickScale[kLevelCount] = {1.f, hasAccent ? 1.5f : 1.f, hasAccent ? 2.f : 1.f};

    const int numVerticesPerRectangle = 6; // 2 triangles
    for (auto& vertices : m_vertices) {
        vertices.clear();
        vertices.reserve(static_cast<int>(m_lines.size()) * numVerticesPerRectangle * 2);
    }

    for (const auto& line : m_lines) {
        if (levelAlpha[line.level] <= 0.f) {
            continue;
        }
        VertexData& vertices = m_vertices[line.level];
        const float x1 = line.x;
        const float x2 = x1 + levelWidth[line.level];
        if (levelIsTicks[line.level]) {
            const float levelTickHeight = std::min(lineBottom / 2,
                    tickHeight * levelTickScale[line.level]);
            vertices.addRectangle(x1, 0.f, x2, levelTickHeight);
            vertices.addRectangle(x1, lineBottom - levelTickHeight, x2, lineBottom);
        } else {
            vertices.addRectangle(x1, 0.f, x2, lineBottom);
        }
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    const float baseAlpha = alpha / 100.0f;
    for (int level = kBeat; level < kLevelCount; ++level) {
        drawVertices(m_vertices[level], levelColor[level], baseAlpha * levelAlpha[level]);
    }
}

void WaveformRenderBeat::drawVertices(
        const VertexData& vertices, const QColor& baseColor, float alpha) {
    if (vertices.size() == 0 || alpha <= 0.f) {
        return;
    }

    QColor color = baseColor;
    color.setAlphaF(alpha);

    const int positionLocation = m_shader.positionLocation();
    const int matrixLocation = m_shader.matrixLocation();
    const int colorLocation = m_shader.colorLocation();

    m_shader.bind();
    m_shader.enableAttributeArray(positionLocation);

    const QMatrix4x4 matrix = matrixForWidgetGeometry(m_waveformRenderer, false);

    m_shader.setAttributeArray(
            positionLocation, GL_FLOAT, vertices.constData(), 2);

    m_shader.setUniformValue(matrixLocation, matrix);
    m_shader.setUniformValue(colorLocation, color);

    glDrawArrays(GL_TRIANGLES, 0, vertices.size());

    m_shader.disableAttributeArray(positionLocation);
    m_shader.release();
}

} // namespace allshader
