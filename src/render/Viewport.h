#pragma once
#include "core/Geometry.h"
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QWheelEvent>
#include <memory>
#include <vector>
namespace af {
class Viewport final : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
  public:
    explicit Viewport(QWidget *parent = nullptr);
    ~Viewport() override;
    void setScene(const Scene &scene);
    void setSelection(const QString &id);
    void fitAll();
    void setView(int preset);
    enum class DisplayMode { Normal, Triangles, Bounds, Wireframe };
    Q_ENUM(DisplayMode)
    void setDisplayMode(DisplayMode mode);
    DisplayMode displayMode() const {
        return displayMode_;
    }
    void setWireframe(bool enabled) {
        setDisplayMode(enabled ? DisplayMode::Wireframe : DisplayMode::Normal);
    }
    void setOrthographic(bool enabled) {
        orthographic_ = enabled;
        update();
    }
    QString rendererInfo() const {
        return renderer_;
    }
    QString renderError() const {
        return error_;
    }
    QPoint projectPoint(const QVector3D &point) const;
    QString pickAt(const QPoint &point) const;
    struct Metrics {
        quint64 triangles = 0, vertices = 0, visibleTriangles = 0, visibleVertices = 0, gpuBufferBytes = 0;
        int instances = 0, visibleInstances = 0;
        double fps = 0, frameMs = 0;
        quint64 presentedFrames = 0;
    };
    Metrics metrics() const;
    void sampleFrames();
    void setContinuousRendering(bool enabled);
    bool continuousRendering() const {
        return continuous_;
    }
  signals:
    void selected(const QString &id);
    void cameraChanged();
    void moveStarted(const QString &id);
    void movePreview(const QString &id, const QVector3D &position);
    void moveFinished(bool commit);
    void deleteRequested();
    void interactionMessage(const QString &message);
    void displayModeChanged(int mode);

  protected:
    void initializeGL() override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

  private:
    struct GpuMesh {
        QString id;
        QOpenGLBuffer buffer;
        QOpenGLVertexArrayObject vao;
        int count = 0, edgeCount = 0, triangleEdgeCount = 0, boundsCount = 0, allocatedVertices = 0;
        Bounds bounds;
        GpuMesh() : buffer(QOpenGLBuffer::VertexBuffer) {}
    };
    void cleanup();
    void rebuild();
    std::unique_ptr<GpuMesh> upload(const QVector<Vertex> &vertices, int count, int edges, const QString &id);
    QMatrix4x4 view() const;
    QMatrix4x4 projection() const;
    QVector3D eye() const;
    bool dragPoint(const QPoint &point, QVector3D &world) const;
    Scene scene_;
    QString selected_, renderer_, error_;
    bool dirty_ = true, orthographic_ = false;
    DisplayMode displayMode_ = DisplayMode::Normal;
    float yaw_ = 45, pitch_ = 30, distance_ = 210;
    QVector3D target_ = {0, 0, 30};
    QPoint lastMouse_, pressMouse_;
    bool dragged_ = false;
    QString dragId_;
    QVector3D dragOrigin_, dragAnchor_, dragNormal_;
    bool moving_ = false;
    bool continuous_ = false;
    QElapsedTimer fpsClock_;
    quint64 frames_ = 0, totalFrames_ = 0;
    double fps_ = 0;
    std::unique_ptr<QOpenGLShaderProgram> program_;
    std::vector<std::unique_ptr<GpuMesh>> meshes_;
    std::unique_ptr<GpuMesh> grid_;
};
} // namespace af
