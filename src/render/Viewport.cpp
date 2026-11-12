#include "Viewport.h"
#include <QKeyEvent>
#include <QOpenGLContext>
#include <QPainter>
#include <QSurfaceFormat>
#include <QTimer>
#include <cmath>
namespace af {
namespace {
constexpr float rad = 3.14159265358979323846f / 180.0f;
}
Viewport::Viewport(QWidget *parent) : QOpenGLWidget(parent) {
    setObjectName("viewport");
    setMinimumSize(480, 360);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    fpsClock_.start();
    connect(this, &QOpenGLWidget::frameSwapped, this, [this] {
        ++frames_;
        ++totalFrames_;
        if (continuous_ && isVisible())
            QTimer::singleShot(0, this, [this] {
                if (continuous_ && isVisible())
                    update();
            });
    });
}
Viewport::~Viewport() {
    cleanup();
}
void Viewport::setContinuousRendering(bool enabled) {
    continuous_ = enabled;
    frames_ = 0;
    fps_ = 0;
    fpsClock_.restart();
    if (enabled)
        update();
}
void Viewport::sampleFrames() {
    const auto ms = fpsClock_.elapsed();
    if (ms > 0) {
        fps_ = frames_ * 1000.0 / ms;
        frames_ = 0;
        fpsClock_.restart();
    }
}
Viewport::Metrics Viewport::metrics() const {
    Metrics m;
    m.instances = scene_.bodies.size();
    m.presentedFrames = totalFrames_;
    for (const auto &b : scene_.bodies)
        if (b.visible)
            ++m.visibleInstances;
    for (const auto &gpu : meshes_) {
        m.vertices += gpu->count;
        m.triangles += gpu->count / 3;
        m.gpuBufferBytes += quint64(gpu->count + gpu->edgeCount) * sizeof(Vertex);
        const auto *b = scene_.find(gpu->id);
        if (b && b->visible) {
            m.visibleVertices += gpu->count;
            m.visibleTriangles += gpu->count / 3;
        }
    }
    if (grid_)
        m.gpuBufferBytes += quint64(grid_->count + grid_->edgeCount) * sizeof(Vertex);
    m.fps = fps_;
    m.frameMs = fps_ > 0 ? 1000.0 / fps_ : 0;
    return m;
}
void Viewport::setScene(const Scene &s) {
    scene_ = s;
    dirty_ = true;
    update();
}
void Viewport::setSelection(const QString &id) {
    selected_ = id;
    update();
}
void Viewport::cleanup() {
    if (!context())
        return;
    makeCurrent();
    meshes_.clear();
    grid_.reset();
    program_.reset();
    doneCurrent();
    disconnect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &Viewport::cleanup);
    dirty_ = true;
}
void Viewport::initializeGL() {
    initializeOpenGLFunctions();
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &Viewport::cleanup, Qt::DirectConnection);
    renderer_ = QString::fromLatin1(reinterpret_cast<const char *>(glGetString(GL_RENDERER))) + " · OpenGL " +
                QString::fromLatin1(reinterpret_cast<const char *>(glGetString(GL_VERSION)));
    program_ = std::make_unique<QOpenGLShaderProgram>();
    const char *vs = R"(#version 330 core
        layout(location=0) in vec3 position;
        layout(location=1) in vec3 normal;
        uniform mat4 mvp;
        uniform mat4 model;
        uniform mat3 normalMatrix;
        out vec3 N;
        out vec3 worldPosition;
        void main(){gl_Position=mvp*vec4(position,1.0);N=normalMatrix*normal;worldPosition=(model*vec4(position,1.0)).xyz;}
    )";
    const char *fs = R"(#version 330 core
        in vec3 N;
        in vec3 worldPosition;
        uniform vec3 objectColor;
        uniform vec3 cameraPosition;
        uniform int unlit;
        uniform int selected;
        out vec4 fragColor;
        void main(){
            if(unlit==1){fragColor=vec4(objectColor,1.0);return;}
            vec3 n=normalize(N);if(!gl_FrontFacing)n=-n;
            vec3 l=normalize(vec3(-0.45,-0.6,1.0));
            vec3 v=normalize(cameraPosition-worldPosition);
            float diffuse=max(dot(n,l),0.0);
            float specular=pow(max(dot(n,normalize(l+v)),0.0),48.0)*0.25;
            float rim=pow(1.0-max(dot(n,v),0.0),3.0);
            vec3 c=objectColor*(0.32+0.68*diffuse)+specular;
            if(selected==1)c=mix(c,vec3(0.95,0.69,0.28),rim*0.7);
            fragColor=vec4(c,1.0);
        }
    )";
    if (!program_->addShaderFromSourceCode(QOpenGLShader::Vertex, vs) ||
        !program_->addShaderFromSourceCode(QOpenGLShader::Fragment, fs) || !program_->link())
        error_ = program_->log();
    dirty_ = true;
}
std::unique_ptr<Viewport::GpuMesh> Viewport::upload(const QVector<Vertex> &vertices, int count, int edges,
                                                    const QString &id) {
    auto gpu = std::make_unique<GpuMesh>();
    gpu->id = id;
    gpu->count = count;
    gpu->edgeCount = edges;
    if (!gpu->vao.create() || !gpu->buffer.create()) {
        error_ = QStringLiteral("OpenGL 缓冲创建失败。");
        return {};
    }
    gpu->vao.bind();
    gpu->buffer.bind();
    gpu->buffer.allocate(vertices.constData(), int(vertices.size() * sizeof(Vertex)));
    program_->bind();
    program_->enableAttributeArray(0);
    program_->setAttributeBuffer(0, GL_FLOAT, 0, 3, sizeof(Vertex));
    program_->enableAttributeArray(1);
    program_->setAttributeBuffer(1, GL_FLOAT, sizeof(QVector3D), 3, sizeof(Vertex));
    gpu->vao.release();
    gpu->buffer.release();
    return gpu;
}
void Viewport::rebuild() {
    meshes_.clear();
    grid_.reset();
    for (const auto &body : scene_.bodies) {
        auto v = tessellate(body).vertices;
        int count = v.size();
        for (int i = 0; i < count; i += 3) {
            auto a = v[i], b = v[i + 1], c = v[i + 2];
            v << a << b << b << c << c << a;
        }
        auto gpu = upload(v, count, count * 2, body.id);
        if (gpu)
            meshes_.push_back(std::move(gpu));
    }
    QVector<Vertex> lines;
    for (int i = -20; i <= 20; ++i) {
        float p = i * 10.0f;
        lines << Vertex{{-200, p, 0}, {0, 0, 1}} << Vertex{{200, p, 0}, {0, 0, 1}}
              << Vertex{{p, -200, 0}, {0, 0, 1}} << Vertex{{p, 200, 0}, {0, 0, 1}};
    }
    int count = lines.size();
    lines << Vertex{{0, 0, 0.05f}, {0, 0, 1}} << Vertex{{70, 0, 0.05f}, {0, 0, 1}}
          << Vertex{{0, 0, 0.05f}, {0, 0, 1}} << Vertex{{0, 70, 0.05f}, {0, 0, 1}}
          << Vertex{{0, 0, 0}, {0, 0, 1}} << Vertex{{0, 0, 70}, {0, 0, 1}};
    grid_ = upload(lines, count, 6, "grid");
    dirty_ = false;
}
QVector3D Viewport::eye() const {
    return target_ + distance_ * QVector3D(std::cos(pitch_ * rad) * std::cos(yaw_ * rad),
                                           std::cos(pitch_ * rad) * std::sin(yaw_ * rad),
                                           std::sin(pitch_ * rad));
}
QMatrix4x4 Viewport::view() const {
    QMatrix4x4 m;
    m.lookAt(eye(), target_, QVector3D(0, 0, 1));
    return m;
}
QMatrix4x4 Viewport::projection() const {
    QMatrix4x4 m;
    float aspect = float(width()) / qMax(1, height());
    if (orthographic_) {
        float h = distance_ * 0.4f;
        m.ortho(-h * aspect, h * aspect, -h, h, 0.1f, 50000.0f);
    } else
        m.perspective(42.0f, aspect, 0.1f, 50000.0f);
    return m;
}
void Viewport::fitAll() {
    auto b = sceneBounds(scene_);
    if (b.valid) {
        target_ = b.center();
        float aspect = float(width()) / qMax(1, height());
        float halfFov = 21.0f * rad;
        if (aspect < 1)
            halfFov = std::atan(std::tan(halfFov) * aspect);
        distance_ = qBound(5.0f, b.radius() / std::sin(halfFov) * 1.18f, 30000.0f);
    } else {
        target_ = {0, 0, 15};
        distance_ = 180;
    }
    update();
    emit cameraChanged();
}
void Viewport::setView(int preset) {
    if (preset == 0) {
        yaw_ = 45;
        pitch_ = 30;
    }
    if (preset == 1) {
        yaw_ = -90;
        pitch_ = 0;
    }
    if (preset == 2) {
        yaw_ = 0;
        pitch_ = 0;
    }
    if (preset == 3) {
        yaw_ = -90;
        pitch_ = 89.9f;
    }
    update();
    emit cameraChanged();
}
QPoint Viewport::projectPoint(const QVector3D &point) const {
    auto p = projection() * view() * QVector4D(point, 1);
    if (std::abs(p.w()) < 1e-8)
        return {};
    p /= p.w();
    return {qRound((p.x() + 1) * width() / 2), qRound((1 - p.y()) * height() / 2)};
}
QString Viewport::pickAt(const QPoint &point) const {
    bool ok = false;
    auto inv = (projection() * view()).inverted(&ok);
    if (!ok)
        return {};
    float x = 2.0f * point.x() / qMax(1, width()) - 1, y = 1 - 2.0f * point.y() / qMax(1, height());
    auto a = inv * QVector4D(x, y, -1, 1), b = inv * QVector4D(x, y, 1, 1);
    auto start = a.toVector3DAffine(), end = b.toVector3DAffine();
    return pick(scene_, start, end - start).id;
}
void Viewport::paintGL() {
    glViewport(0, 0, qRound(width() * devicePixelRatioF()), qRound(height() * devicePixelRatioF()));
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glClearColor(0.065f, 0.085f, 0.115f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (program_ && error_.isEmpty()) {
        if (dirty_)
            rebuild();
        program_->bind();
        QMatrix4x4 identity;
        const auto vp = projection() * view();
        program_->setUniformValue("mvp", vp);
        program_->setUniformValue("model", identity);
        program_->setUniformValue("normalMatrix", identity.normalMatrix());
        program_->setUniformValue("cameraPosition", eye());
        program_->setUniformValue("unlit", 1);
        program_->setUniformValue("selected", 0);
        if (grid_) {
            grid_->vao.bind();
            program_->setUniformValue("objectColor", QVector3D(0.14f, 0.18f, 0.23f));
            glDrawArrays(GL_LINES, 0, grid_->count);
            const QVector3D colors[] = {{0.78f, 0.33f, 0.35f}, {0.3f, 0.69f, 0.5f}, {0.35f, 0.57f, 0.9f}};
            for (int i = 0; i < 3; ++i) {
                program_->setUniformValue("objectColor", colors[i]);
                glDrawArrays(GL_LINES, grid_->count + i * 2, 2);
            }
            grid_->vao.release();
        }
        for (const auto &gpu : meshes_) {
            const Body *b = scene_.find(gpu->id);
            if (!b || !b->visible)
                continue;
            const auto model = b->matrix();
            program_->setUniformValue("mvp", vp * model);
            program_->setUniformValue("model", model);
            program_->setUniformValue("normalMatrix", model.normalMatrix());
            program_->setUniformValue("selected", int(b->id == selected_));
            program_->setUniformValue("unlit", int(wireframe_));
            program_->setUniformValue(
                "objectColor",
                QVector3D(float(b->color.redF()), float(b->color.greenF()), float(b->color.blueF())));
            gpu->vao.bind();
            if (wireframe_)
                glDrawArrays(GL_LINES, gpu->count, gpu->edgeCount);
            else {
                glEnable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(1, 1);
                glDrawArrays(GL_TRIANGLES, 0, gpu->count);
                glDisable(GL_POLYGON_OFFSET_FILL);
                if (b->id == selected_) {
                    program_->setUniformValue("unlit", 1);
                    program_->setUniformValue("objectColor", QVector3D(1.0f, 0.73f, 0.32f));
                    glDrawArrays(GL_LINES, gpu->count, gpu->edgeCount);
                }
            }
            gpu->vao.release();
        }
        program_->release();
    }
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QColor("#8595ab"));
    p.setFont(QFont("Microsoft YaHei", 9));
    p.drawText(22, 30,
               orthographic_ ? QStringLiteral("ORTHOGRAPHIC  /  正交视图")
                             : QStringLiteral("PERSPECTIVE  /  透视视图"));
    p.drawText(22, height() - 22,
               QStringLiteral("左键选择/拖动模型 · Delete 删除 · Esc 取消拖动 · 右键旋转 · 中键平移"));
    if (!error_.isEmpty()) {
        p.setPen(QColor("#f29292"));
        p.drawText(rect().adjusted(30, 60, -30, -60), Qt::TextWordWrap, error_);
    }
    const QPointF center(width() - 56, height() - 65);
    const auto v = view();
    const QVector3D axes[] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const QColor colors[] = {QColor("#e68789"), QColor("#7dcea0"), QColor("#80afff")};
    for (int i = 0; i < 3; ++i) {
        auto d = v.mapVector(axes[i]);
        QPointF end = center + QPointF(d.x() * 27, -d.y() * 27);
        p.setPen(QPen(colors[i], 2));
        p.drawLine(center, end);
        p.drawText(end + QPointF(3, -3), QString("XYZ").mid(i, 1));
    }
}
void Viewport::mousePressEvent(QMouseEvent *e) {
    lastMouse_ = pressMouse_ = e->pos();
    dragged_ = false;
    setFocus();
    if (e->button() == Qt::LeftButton) {
        dragId_ = pickAt(e->pos());
        emit selected(dragId_);
        const auto *body = scene_.find(dragId_);
        if (body && !body->fixed) {
            dragOrigin_ = body->position;
            dragNormal_ = (eye() - target_).normalized();
            if (!dragPoint(e->pos(), dragAnchor_))
                dragId_.clear();
        } else {
            if (body)
                emit interactionMessage(QStringLiteral("模型已固定；取消固定后可拖动。"));
            dragId_.clear();
        }
    }
}
void Viewport::mouseMoveEvent(QMouseEvent *e) {
    auto delta = e->pos() - lastMouse_;
    if ((e->pos() - pressMouse_).manhattanLength() > 3)
        dragged_ = true;
    if ((e->buttons() & Qt::LeftButton) && dragged_ && !dragId_.isEmpty()) {
        if (!moving_) {
            moving_ = true;
            emit moveStarted(dragId_);
        }
        QVector3D point;
        if (dragPoint(e->pos(), point))
            emit movePreview(dragId_, dragOrigin_ + point - dragAnchor_);
    } else if (e->buttons() & Qt::RightButton) {
        yaw_ -= delta.x() * 0.4f;
        pitch_ = qBound(-89.0f, pitch_ + delta.y() * 0.4f, 89.0f);
        update();
        emit cameraChanged();
    } else if (e->buttons() & Qt::MiddleButton) {
        auto inv = view().inverted();
        auto right = inv.mapVector({1, 0, 0}), up = inv.mapVector({0, 1, 0});
        target_ += (-delta.x() * right + delta.y() * up) * (distance_ * 0.0015f);
        update();
        emit cameraChanged();
    }
    lastMouse_ = e->pos();
}
void Viewport::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton) {
        if (moving_)
            emit moveFinished(true);
        moving_ = false;
        dragId_.clear();
    }
}
bool Viewport::dragPoint(const QPoint &point, QVector3D &world) const {
    bool ok = false;
    auto inverse = (projection() * view()).inverted(&ok);
    if (!ok)
        return false;
    float x = 2.0f * point.x() / qMax(1, width()) - 1;
    float y = 1 - 2.0f * point.y() / qMax(1, height());
    auto a = (inverse * QVector4D(x, y, -1, 1)).toVector3DAffine();
    auto b = (inverse * QVector4D(x, y, 1, 1)).toVector3DAffine();
    auto direction = b - a;
    float divisor = QVector3D::dotProduct(direction, dragNormal_);
    if (std::abs(divisor) < 1e-8f)
        return false;
    world = a + direction * (QVector3D::dotProduct(dragOrigin_ - a, dragNormal_) / divisor);
    return true;
}
void Viewport::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Escape && moving_) {
        moving_ = false;
        dragId_.clear();
        emit moveFinished(false);
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_Delete && !moving_) {
        emit deleteRequested();
        e->accept();
        return;
    }
    QOpenGLWidget::keyPressEvent(e);
}
void Viewport::wheelEvent(QWheelEvent *e) {
    distance_ = qBound(2.0f, distance_ * float(std::pow(0.88, e->angleDelta().y() / 120.0)), 30000.0f);
    update();
    emit cameraChanged();
    e->accept();
}
} // namespace af
