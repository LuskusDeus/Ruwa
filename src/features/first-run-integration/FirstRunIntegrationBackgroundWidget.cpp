// SPDX-License-Identifier: MPL-2.0

#include "features/first-run-integration/FirstRunIntegrationBackgroundWidget.h"

#include <QColor>
#include <QOpenGLShaderProgram>
#include <QSurfaceFormat>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>
#include <QtMath>

namespace ruwa::ui::first_run_integration {

namespace {

constexpr int kPreviewFrameIntervalMs = 16;
constexpr int kRevealAnimationDelayMs = 800;
constexpr int kRevealAnimationDurationMs = 1400;

constexpr const char* kVertexShader = R"(
#version 330 core

out vec2 vUv;

const vec2 kVertices[3] = vec2[3](
    vec2(-1.0, -1.0),
    vec2( 3.0, -1.0),
    vec2(-1.0,  3.0)
);

void main()
{
    vec2 position = kVertices[gl_VertexID];
    vUv = position * 0.5 + 0.5;
    gl_Position = vec4(position, 0.0, 1.0);
}
)";

constexpr const char* kFragmentShader = R"(
#version 330 core

in vec2 vUv;
out vec4 fragColor;

uniform vec2 uResolution;
uniform float uTime;
uniform vec3 uHomeBackgroundColor;
uniform vec3 uBlobColor;
uniform vec4 uRevealOverlayColor;

mat2 rotate2d(float angle)
{
    float s = sin(angle);
    float c = cos(angle);
    return mat2(c, -s, s, c);
}

float pixelNoise(vec2 pixel)
{
    vec2 seed = floor(pixel);
    return fract(52.9829189 * fract(seed.x * 0.06711056 + seed.y * 0.00583715));
}

float triangularDither(vec2 pixel)
{
    return pixelNoise(pixel) + pixelNoise(pixel + vec2(19.19, 47.47)) - 1.0;
}

vec2 blobPath(float id, float time)
{
    float phase = id * 2.173;
    float spin = time * (0.17 + id * 0.035) + phase;
    vec2 orbit = vec2(cos(spin), sin(spin * 1.27 + phase * 0.31));
    vec2 twist = vec2(sin(time * 0.39 + phase * 1.63),
                      cos(time * 0.31 + phase * 1.17));
    vec2 curl = vec2(cos(time * 0.11 + orbit.y * 1.9 + phase),
                     sin(time * 0.13 + orbit.x * 2.2 + phase));

    vec2 position = vec2(0.78, 0.50)
                  + orbit * vec2(0.075, 0.185)
                  + twist * vec2(0.050, 0.075)
                  + curl * vec2(0.030, 0.045);

    position.x = clamp(position.x, 0.58, 0.96);
    position.y = clamp(position.y, 0.16, 0.86);
    return position;
}

float blobField(vec2 uv, vec2 center, vec2 radius, float angle, float aspect)
{
    vec2 delta = uv - center;
    delta.x *= aspect;
    vec2 local = rotate2d(angle) * delta;
    local /= radius;

    return exp(-dot(local, local));
}

vec3 blobColor(int id)
{
    if (id == 0) {
        return uBlobColor;
    }
    if (id == 1) {
        return uBlobColor * 0.80;
    }
    return uBlobColor * 0.70;
}

float blobSizeScale(int id)
{
    if (id == 0) {
        return 1.0;
    }
    if (id == 1) {
        return 0.85;
    }
    return 1.0 / 1.5;
}

float stripeHash(float value)
{
    return fract(sin(value * 127.1 + 17.7) * 43758.5453);
}

vec3 backgroundBase(vec2 uv, float aspect)
{
    vec2 centered = uv - 0.5;
    centered.x *= aspect;

    vec3 homeBase = max(uHomeBackgroundColor, vec3(0.075, 0.078, 0.084));
    vec3 baseLow = homeBase * 0.62;
    vec3 baseHigh = homeBase * 0.78;
    vec3 base = mix(baseLow, baseHigh, smoothstep(0.0, 1.0, uv.y));
    base += vec3(0.010, 0.011, 0.012) * smoothstep(0.15, 0.95, 1.0 - length(centered) * 0.62);
    return base;
}

vec3 sceneColor(vec2 uv, float aspect)
{
    vec2 centered = uv - 0.5;
    centered.x *= aspect;

    vec3 base = backgroundBase(uv, aspect);
    float glowField = 0.0;
    float bodyField = 0.0;
    vec3 glowColor = vec3(0.0);

    for (int i = 0; i < 3; ++i) {
        float id = float(i);
        vec2 center = blobPath(id, uTime);
        vec2 radius = vec2(0.26, 0.34) * blobSizeScale(i);
        float angle = uTime * (0.12 + id * 0.03) + id * 1.71;
        float body = blobField(uv, center, radius, angle, aspect);
        float glow = blobField(uv, center, radius * 1.78, angle + 0.6, aspect);

        bodyField += body;
        glowField += glow;
        glowColor += blobColor(i) * (glow * 0.24 + body * 0.34);
    }

    float connection = smoothstep(1.02, 1.46, bodyField);
    float softConnection = smoothstep(0.54, 1.05, glowField);
    vec3 bridgeColor = mix(vec3(0.34, 0.54, 0.62),
                           vec3(0.72, 0.56, 0.38),
                           0.5 + 0.5 * sin(uTime * 0.21));

    vec3 color = base;
    color += glowColor * (0.42 + softConnection * 0.24);
    color += bridgeColor * connection * 0.55;

    float rightFocus = smoothstep(0.28, 0.86, uv.x);
    color = mix(base, color, rightFocus);

    float vignette = smoothstep(1.18, 0.24, length(centered));
    return color * mix(0.72, 1.0, vignette);
}

vec3 linearBlend(vec3 base, vec3 layer)
{
    return clamp(base + layer, vec3(0.0), vec3(1.0));
}

vec3 multiplyBlend(vec3 base, vec3 layer, float amount)
{
    return mix(base, base * layer, amount);
}

vec3 stripeGlassColor(float stripeId,
                      float stripePos,
                      float localY,
                      vec3 stripeBase,
                      vec3 refractedEnergy)
{
    float random = stripeHash(stripeId);
    float leftBevel = exp(-pow(stripePos * 12.0, 2.0));
    float rightBevel = exp(-pow((1.0 - stripePos) * 12.0, 2.0));
    float centerSheen = exp(-pow((stripePos - 0.46) * 3.2, 2.0));
    float innerShadow = smoothstep(0.58, 1.0, stripePos);
    float lengthGradient = 0.5 + 0.5 * sin(localY * 2.1 + random * 6.2831853);
    float brightness = mix(0.72, 1.12, random) * mix(0.78, 1.08, lengthGradient);
    float refractedLight = dot(refractedEnergy, vec3(0.2126, 0.7152, 0.0722));

    vec3 tint = mix(vec3(0.08, 0.08, 0.075), stripeBase, 0.48);
    vec3 tintedGlass = tint * brightness * refractedLight * 3.15;
    vec3 highlight = mix(tintedGlass, vec3(1.0, 0.88, 0.70), 0.54) * refractedLight;
    vec3 shadow = tintedGlass * 0.38;
    vec3 stripeColor = tintedGlass + refractedEnergy * 0.70;
    stripeColor = mix(stripeColor, highlight, leftBevel * 0.42 + centerSheen * 0.18);
    stripeColor = mix(stripeColor, shadow, rightBevel * 0.34 + innerShadow * 0.24);

    return max(stripeColor - vec3(0.045), vec3(0.0));
}

vec3 glassStripeComposite(vec2 uv, float aspect, vec3 stripeBase, vec3 color)
{
    vec2 screen = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);
    vec2 local = rotate2d(-0.76) * screen;
    float stripeWidth = 0.118;
    float stripeCoord = local.x / stripeWidth;
    float stripeId = floor(stripeCoord);
    float stripePos = fract(stripeCoord);
    float aa = max(fwidth(stripeCoord) * 4.25, 0.0060);
    float leftBevel = exp(-pow(stripePos * 11.0, 2.0));
    float rightBevel = exp(-pow((1.0 - stripePos) * 11.0, 2.0));
    float centerLens = 1.0 - abs(stripePos - 0.5) * 2.0;
    float lensPower = 0.034 + centerLens * 0.026 + (leftBevel - rightBevel) * 0.058;
    vec2 refractLocalOffset = vec2((0.5 - stripePos) * lensPower + (leftBevel - rightBevel) * 0.064,
                                   sin(local.y * 3.0 + stripeId) * 0.014);
    vec2 refractScreenOffset = rotate2d(0.76) * refractLocalOffset;
    vec2 refractUv = clamp(uv + vec2(refractScreenOffset.x / max(aspect, 0.001),
                                     refractScreenOffset.y),
                           vec2(0.0),
                           vec2(1.0));
    vec3 refractedColor = sceneColor(refractUv, aspect);
    vec3 refractedBase = backgroundBase(refractUv, aspect);
    vec3 refractedEnergy = max(refractedColor - refractedBase, vec3(0.0));

    vec3 currentStripe = stripeGlassColor(stripeId, stripePos, local.y, stripeBase, refractedEnergy);
    vec3 leftStripe = stripeGlassColor(stripeId - 1.0, stripePos + 1.0, local.y, stripeBase, refractedEnergy);
    vec3 rightStripe = stripeGlassColor(stripeId + 1.0, stripePos - 1.0, local.y, stripeBase, refractedEnergy);

    float leftMix = 1.0 - smoothstep(0.0, aa, stripePos);
    float rightMix = 1.0 - smoothstep(0.0, aa, 1.0 - stripePos);
    vec3 stripeColor = mix(currentStripe, leftStripe, leftMix);
    stripeColor = mix(stripeColor, rightStripe, rightMix);

    float bevel = max(exp(-pow(stripePos * 14.0, 2.0)),
                      exp(-pow((1.0 - stripePos) * 14.0, 2.0)));
    float seamShade = bevel * (1.0 - max(leftMix, rightMix) * 0.75);
    float refractedLight = dot(refractedEnergy, vec3(0.2126, 0.7152, 0.0722));
    float glassPresence = clamp(refractedLight * 5.0 + dot(stripeColor, vec3(0.2126, 0.7152, 0.0722)) * 3.0,
                                0.0,
                                1.0);
    vec3 distortedBase = mix(color, refractedColor, 0.72 * glassPresence);
    vec3 linear = linearBlend(distortedBase, stripeColor * 0.55);
    vec3 multiplyLayer = mix(vec3(0.72), clamp(stripeColor + vec3(0.50), vec3(0.0), vec3(1.0)), 0.66);
    vec3 multiply = multiplyBlend(distortedBase, multiplyLayer, glassPresence * (0.42 + seamShade * 0.38));
    vec3 blended = mix(linear, multiply, 0.48 + seamShade * 0.32);
    vec3 shaded = color * (1.0 - seamShade * refractedLight * 0.62);

    return mix(shaded, blended, 0.72 * glassPresence);
}

void addGlassStripes(vec2 uv, float aspect, vec3 stripeBase, inout vec3 color)
{
    vec2 pixel = 1.0 / max(uResolution, vec2(1.0));
    vec3 center = glassStripeComposite(uv, aspect, stripeBase, color);
    vec3 normalA = glassStripeComposite(
        clamp(uv + pixel * vec2(1.15, -1.15), vec2(0.0), vec2(1.0)),
        aspect,
        stripeBase,
        color);
    vec3 normalB = glassStripeComposite(
        clamp(uv + pixel * vec2(-1.15, 1.15), vec2(0.0), vec2(1.0)),
        aspect,
        stripeBase,
        color);
    vec3 tangentA = glassStripeComposite(
        clamp(uv + pixel * vec2(0.55, 0.55), vec2(0.0), vec2(1.0)),
        aspect,
        stripeBase,
        color);
    vec3 tangentB = glassStripeComposite(
        clamp(uv + pixel * vec2(-0.55, -0.55), vec2(0.0), vec2(1.0)),
        aspect,
        stripeBase,
        color);

    color = (center * 2.0 + normalA + normalB + tangentA + tangentB) / 6.0;
}

void main()
{
    vec2 uv = vUv;
    float aspect = uResolution.x / max(uResolution.y, 1.0);
    vec3 color = sceneColor(uv, aspect);
    addGlassStripes(uv, aspect, uBlobColor, color);

    vec3 dither = vec3(
        triangularDither(gl_FragCoord.xy),
        triangularDither(gl_FragCoord.xy + vec2(2.0, 1.0)),
        triangularDither(gl_FragCoord.xy + vec2(3.0, 3.0))
    );
    color += dither * (2.25 / 255.0);
    color = clamp(color, vec3(0.0), vec3(1.0));

    fragColor = mix(vec4(color, 1.0),
                    vec4(uRevealOverlayColor.rgb, 1.0),
                    uRevealOverlayColor.a);
}
)";

} // namespace

FirstRunIntegrationBackgroundWidget::FirstRunIntegrationBackgroundWidget(QWidget* parent)
    : QOpenGLWidget(parent)
{
    QSurfaceFormat surfaceFormat = format();
    surfaceFormat.setSamples(8);
    setFormat(surfaceFormat);

    setAutoFillBackground(false);
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);

    connect(&m_frameTimer, &QTimer::timeout, this, [this]() { update(); });
    m_frameTimer.setInterval(kPreviewFrameIntervalMs);
}

FirstRunIntegrationBackgroundWidget::~FirstRunIntegrationBackgroundWidget()
{
    if (isValid()) {
        makeCurrent();
        destroyShaderPipeline();
        doneCurrent();
    }
}

bool FirstRunIntegrationBackgroundWidget::isShaderPipelineReady() const
{
    return m_pipelineReady;
}

QString FirstRunIntegrationBackgroundWidget::lastError() const
{
    return m_lastError;
}

void FirstRunIntegrationBackgroundWidget::setPreviewAnimationRunning(bool running)
{
    if (running) {
        if (!m_clock.isValid()) {
            m_clock.start();
        }
        restartRevealAnimation();
        m_frameTimer.start();
    } else {
        m_frameTimer.stop();
    }
}

void FirstRunIntegrationBackgroundWidget::setRevealOverlayColor(const QColor& color)
{
    if (m_revealOverlayColor == color) {
        return;
    }

    m_revealOverlayColor = color;
    update();
}

void FirstRunIntegrationBackgroundWidget::setBlobColor(const QColor& color)
{
    if (m_blobColor == color) {
        return;
    }

    m_blobColor = color;
    update();
}

void FirstRunIntegrationBackgroundWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    initializeShaderPipeline();
}

void FirstRunIntegrationBackgroundWidget::resizeGL(int width, int height)
{
    const qreal dpr = devicePixelRatioF();
    glViewport(0, 0, qRound(width * dpr), qRound(height * dpr));
}

void FirstRunIntegrationBackgroundWidget::paintGL()
{
    const float revealAlpha = revealOverlayAlpha();
    const float overlayRed = static_cast<float>(m_revealOverlayColor.redF());
    const float overlayGreen = static_cast<float>(m_revealOverlayColor.greenF());
    const float overlayBlue = static_cast<float>(m_revealOverlayColor.blueF());
    const QVector3D homeBackgroundColor(overlayRed, overlayGreen, overlayBlue);
    const QVector3D blobColor(static_cast<float>(m_blobColor.redF()),
        static_cast<float>(m_blobColor.greenF()), static_cast<float>(m_blobColor.blueF()));
    const float clearRed = qMax(overlayRed, 0.075f) * 0.62f;
    const float clearGreen = qMax(overlayGreen, 0.078f) * 0.62f;
    const float clearBlue = qMax(overlayBlue, 0.084f) * 0.62f;

    if (!m_pipelineReady || !m_program) {
        glClearColor(clearRed * (1.0f - revealAlpha) + overlayRed * revealAlpha,
            clearGreen * (1.0f - revealAlpha) + overlayGreen * revealAlpha,
            clearBlue * (1.0f - revealAlpha) + overlayBlue * revealAlpha, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    const qreal dpr = devicePixelRatioF();
    const QVector2D pixelSize(width() * dpr, height() * dpr);
    const float time = m_clock.isValid() ? static_cast<float>(m_clock.elapsed()) / 1000.0f : 0.0f;

    glClear(GL_COLOR_BUFFER_BIT);

    m_program->bind();
    m_program->setUniformValue("uResolution", pixelSize);
    m_program->setUniformValue("uTime", time);
    m_program->setUniformValue("uHomeBackgroundColor", homeBackgroundColor);
    m_program->setUniformValue("uBlobColor", blobColor);
    m_program->setUniformValue(
        "uRevealOverlayColor", QVector4D(overlayRed, overlayGreen, overlayBlue, revealAlpha));

    m_vao.bind();
    glDrawArrays(GL_TRIANGLES, 0, 3);
    m_vao.release();

    m_program->release();
}

void FirstRunIntegrationBackgroundWidget::initializeShaderPipeline()
{
    destroyShaderPipeline();

    m_lastError.clear();
    m_program = std::make_unique<QOpenGLShaderProgram>();

    const bool ok = m_vao.create()
        && m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader)
        && m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader)
        && m_program->link();

    if (!ok) {
        m_lastError
            = m_program ? m_program->log() : QStringLiteral("Unable to create shader program");
        destroyShaderPipeline();
        return;
    }

    m_pipelineReady = true;
}

void FirstRunIntegrationBackgroundWidget::destroyShaderPipeline()
{
    m_pipelineReady = false;
    if (m_vao.isCreated()) {
        m_vao.destroy();
    }
    m_program.reset();
}

void FirstRunIntegrationBackgroundWidget::restartRevealAnimation()
{
    m_revealClock.restart();
}

float FirstRunIntegrationBackgroundWidget::revealOverlayAlpha() const
{
    if (!m_revealClock.isValid()) {
        return 0.0f;
    }

    const qint64 elapsed = m_revealClock.elapsed();
    if (elapsed < kRevealAnimationDelayMs) {
        return 1.0f;
    }

    const float progress = qMin(1.0f,
        static_cast<float>(elapsed - kRevealAnimationDelayMs)
            / static_cast<float>(kRevealAnimationDurationMs));
    return 1.0f - progress;
}

} // namespace ruwa::ui::first_run_integration
