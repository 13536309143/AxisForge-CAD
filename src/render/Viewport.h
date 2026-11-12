#pragma once
#include "core/Geometry.h"
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
    void setWireframe(bool enabled) {
        wireframe_ = enabled;
        update();
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
  signals:
    void selected(const QString &id);
    void cameraChanged();
    void moveStarted(const QString &id);
    void movePreview(const QString &id, const QVector3D &position);
    void moveFinished(bool commit);
    void deleteRequested();
    void interactionMessage(const QString &message);

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
        int count = 0, edgeCount = 0;
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
    bool dirty_ = true, wireframe_ = false, orthographic_ = false;
    float yaw_ = 45, pitch_ = 30, distance_ = 210;
    QVector3D target_ = {0, 0, 30};
    QPoint lastMouse_, pressMouse_;
    bool dragged_ = false;
    QString dragId_;
    QVector3D dragOrigin_, dragAnchor_, dragNormal_;
    bool moving_ = false;
    std::unique_ptr<QOpenGLShaderProgram> program_;
    std::vector<std::unique_ptr<GpuMesh>> meshes_;
    std::unique_ptr<GpuMesh> grid_;
};
} // namespace af
