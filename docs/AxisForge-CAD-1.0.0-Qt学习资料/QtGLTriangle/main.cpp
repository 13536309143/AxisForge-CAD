#include <QApplication>
#include <QOpenGLContext>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QSurfaceFormat>
#include <QDebug>
#include <QTimer>
#include <QImage>
#include <memory>

class TriangleWidget final : public QOpenGLWidget,
                             protected QOpenGLFunctions_3_3_Core {
public:
    using QOpenGLWidget::QOpenGLWidget;
    ~TriangleWidget() override { cleanup(); }
    bool ready() const { return drawable_; }

protected:
    void initializeGL() override {
        if (!initializeOpenGLFunctions()) {
            qWarning() << "OpenGL 3.3 functions unavailable";
            return;
        }
        functionsReady_ = true;
        connect(context(), &QOpenGLContext::aboutToBeDestroyed,
                this, [this] { cleanup(); }, Qt::DirectConnection);
        program_ = std::make_unique<QOpenGLShaderProgram>();
        const char *vs = R"GLSL(#version 330 core
            layout(location = 0) in vec3 position;
            void main() { gl_Position = vec4(position, 1.0); }
        )GLSL";
        const char *fs = R"GLSL(#version 330 core
            out vec4 color;
            void main() { color = vec4(0.15, 0.65, 0.9, 1.0); }
        )GLSL";
        if (!program_->addShaderFromSourceCode(QOpenGLShader::Vertex, vs)
            || !program_->addShaderFromSourceCode(QOpenGLShader::Fragment, fs)
            || !program_->link()) {
            qWarning() << program_->log();
            return;
        }
        const float vertices[] = {
            -0.6f, -0.5f, 0.0f,
             0.6f, -0.5f, 0.0f,
             0.0f,  0.6f, 0.0f
        };
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                              3 * sizeof(float), nullptr);
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        drawable_ = true;
    }

    void paintGL() override {
        if (!functionsReady_) return;
        glClearColor(0.06f, 0.08f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (!drawable_) return;
        program_->bind();
        glBindVertexArray(vao_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        program_->release();
    }

private:
    void cleanup() {
        if (!functionsReady_) return;
        makeCurrent();
        if (vbo_) glDeleteBuffers(1, &vbo_);
        if (vao_) glDeleteVertexArrays(1, &vao_);
        program_.reset();
        vbo_ = vao_ = 0;
        drawable_ = false;
        functionsReady_ = false;
        doneCurrent();
    }
    GLuint vao_ = 0, vbo_ = 0;
    bool functionsReady_ = false, drawable_ = false;
    std::unique_ptr<QOpenGLShaderProgram> program_;
};

int main(int argc, char **argv) {
    QSurfaceFormat f;
    f.setVersion(3, 3);
    f.setProfile(QSurfaceFormat::CoreProfile);
    f.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(f);
    QApplication app(argc, argv);
    TriangleWidget w;
    w.resize(800, 600);
    w.setWindowTitle("Qt + OpenGL triangle");
    w.show();
    if (app.arguments().contains("--self-test")) {
        QTimer::singleShot(800, &w, [&] {
            const auto frame = w.grabFramebuffer();
            bool ok = w.ready() && !frame.isNull();
            if (ok) {
                const auto color = frame.pixelColor(frame.width()/2, frame.height()/2);
                ok = color.blue() > 180 && color.green() > 120 && color.red() < 100;
            }
            qInfo() << "Triangle framebuffer check:" << (ok ? "passed" : "failed");
            app.exit(ok ? 0 : 1);
        });
    }
    return app.exec();
}
