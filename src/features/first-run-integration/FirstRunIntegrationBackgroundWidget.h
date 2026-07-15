// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_FEATURES_FIRSTRUNINTEGRATION_BACKGROUNDWIDGET_H
#define RUWA_FEATURES_FIRSTRUNINTEGRATION_BACKGROUNDWIDGET_H

#include <QColor>
#include <QElapsedTimer>
#include <QOpenGLFunctions>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QString>
#include <QTimer>

#include <memory>

class QOpenGLShaderProgram;

namespace ruwa::ui::first_run_integration {

class FirstRunIntegrationBackgroundWidget final : public QOpenGLWidget, protected QOpenGLFunctions {
public:
    explicit FirstRunIntegrationBackgroundWidget(QWidget* parent = nullptr);
    ~FirstRunIntegrationBackgroundWidget() override;

    bool isShaderPipelineReady() const;
    QString lastError() const;
    void setPreviewAnimationRunning(bool running);
    void setRevealOverlayColor(const QColor& color);
    void setBlobColor(const QColor& color);

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;

private:
    void initializeShaderPipeline();
    void destroyShaderPipeline();
    void restartRevealAnimation();
    float revealOverlayAlpha() const;

private:
    QTimer m_frameTimer;
    QElapsedTimer m_clock;
    QElapsedTimer m_revealClock;
    QColor m_revealOverlayColor { 7, 8, 9 };
    QColor m_blobColor { 87, 133, 184 };
    std::unique_ptr<QOpenGLShaderProgram> m_program;
    QOpenGLVertexArrayObject m_vao;
    QString m_lastError;
    bool m_pipelineReady { false };
};

} // namespace ruwa::ui::first_run_integration

#endif // RUWA_FEATURES_FIRSTRUNINTEGRATION_BACKGROUNDWIDGET_H
