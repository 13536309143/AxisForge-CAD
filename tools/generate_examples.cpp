#include "core/Kernel.h"
#include "core/ProjectIO.h"
#include "core/MeshIO.h"
#include "core/Constraints.h"
#include <QCoreApplication>
#include <QDir>
#include <QTextStream>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <gp_Vec.hxx>
#include <cmath>
#include <stdexcept>
using namespace af;
constexpr double pi=3.14159265358979323846;
Body body(Shape shape,QString name,QVector3D position,QColor color) {
    auto b=makeBody(shape);b.name=name;b.position=position;b.color=color;b.params.segments=64;return b;
}
Body box(QString name,double x,double y,double z,QVector3D p,QColor color) {
    auto b=body(Shape::Box,name,p,color);b.params.width=x;b.params.depth=y;b.params.height=z;return b;
}
Body cylinder(double radius,double height,QVector3D p) {
    auto b=body(Shape::Cylinder,"tool",p,Qt::gray);b.params.radius=radius;b.params.height=height;return b;
}
Body cut(const Body&a,const Body&b) {
    Body result;QString why;
    if(!booleanBody(a,b,BooleanOp::Cut,result,&why))throw std::runtime_error(why.toStdString());
    result.name=a.name;result.color=a.color;return result;
}
void mate(Scene&s,const Body&a,const Body&b,MateType type,double offset=0) {
    Mate m;m.id=QString("mate-%1").arg(s.mates.size());m.source=a.id;m.target=b.id;m.type=type;m.offset=offset;s.mates<<m;
}
Scene bearing() {
    Scene s;s.title=QStringLiteral("滚珠轴承 · 20×48×14 教学装配");
    auto outer=body(Shape::Tube,QStringLiteral("外圈 Ø48 / Ø41"),{0,0,7},QColor("#95b2c7"));
    outer.params.radius=24;outer.params.tubeRadius=20.5;outer.params.height=14;outer.fixed=true;s.bodies<<outer;
    auto inner=body(Shape::Tube,QStringLiteral("内圈 Ø27 / Ø20"),{0,0,7},QColor("#c3d2df"));
    inner.params.radius=13.5;inner.params.tubeRadius=10;inner.params.height=14;s.bodies<<inner;
    mate(s,outer,inner,MateType::Coaxial);mate(s,outer,inner,MateType::AxialOffset);
    for(int i=0;i<10;++i) {
        double a=2*pi*i/10;
        auto ball=body(Shape::Sphere,QStringLiteral("滚珠 %1 · Ø7").arg(i+1),{float(17*cos(a)),float(17*sin(a)),7},QColor("#e0e8f0"));
        ball.params.radius=3.5;ball.params.segments=24;s.bodies<<ball;
    }
    for(float z:{2.5f,11.5f}) {
        auto cage=body(Shape::Torus,QStringLiteral("保持架示意环"),{0,0,z},QColor("#d6a65c"));
        cage.params.radius=17;cage.params.tubeRadius=.7;s.bodies<<cage;
    }
    return s;
}
Scene gear() {
    // Sampled involute flanks, radial root transitions, no manufacturing fillets.
    const int teeth=24;const double module=2,pressure=20*pi/180;
    const double pitch=module*teeth/2,base=pitch*cos(pressure),tip=pitch+module,root=pitch-1.25*module;
    auto inv=[](double t){return t-atan(t);};
    const double ip=tan(pressure)-pressure,half=pi/(2*teeth);
    BRepBuilderAPI_MakePolygon polygon;
    auto point=[&](double r,double a){polygon.Add(gp_Pnt(r*cos(a),r*sin(a),-6));};
    for(int k=0;k<teeth;++k) {
        double center=2*pi*k/teeth;
        auto angle=[&](double r){return half+ip-inv(sqrt(r*r/(base*base)-1));};
        point(root,center-angle(base));
        for(int j=0;j<=8;++j){double r=base+(tip-base)*j/8;point(r,center-angle(r));}
        for(int j=1;j<=4;++j)point(tip,center-angle(tip)+2*angle(tip)*j/4);
        for(int j=7;j>=0;--j){double r=base+(tip-base)*j/8;point(r,center+angle(r));}
        point(root,center+angle(base));
        const double next=2*pi*(k+1)/teeth-angle(base);
        for(int j=1;j<4;++j)point(root,center+angle(base)+(next-center-angle(base))*j/4);
    }
    polygon.Close();
    auto face=BRepBuilderAPI_MakeFace(polygon.Wire()).Face();
    auto shape=BRepPrimAPI_MakePrism(face,gp_Vec(0,0,12)).Shape();
    auto blank=body(Shape::Solid,QStringLiteral("24齿渐开线示意齿轮 · m2 · 20°"),{0,0,6},QColor("#d7ad67"));
    blank.solid=createSolid(shape,64);
    auto g=cut(blank,cylinder(5.2,18,{0,0,6}));g.fixed=true;
    Scene s;s.title=QStringLiteral("直齿轮 · 24齿 / 模数2 / 压力角20°");s.bodies<<g;
    auto shaft=cylinder(5,28,{0,0,14});shaft.name=QStringLiteral("轴 Ø10 × 28");shaft.color=QColor("#7ca7bf");s.bodies<<shaft;
    mate(s,g,shaft,MateType::Coaxial);mate(s,g,shaft,MateType::AxialOffset,14);
    return s;
}
Scene guide() {
    Scene s;s.title=QStringLiteral("双轨直线导向组件 · 180 mm");
    auto base=box(QStringLiteral("安装底板 · 180×55×8"),180,55,8,{0,0,4},QColor("#4f728c"));
    for(float x:{-70.f,70.f})for(float y:{-21.f,21.f})base=cut(base,cylinder(3,14,{x,y,4}));
    base.fixed=true;s.bodies<<base;
    for(float y:{-16.f,16.f}) {
        auto rail=box(QStringLiteral("矩形导轨 · 180×12×10"),180,12,10,{0,y,13},QColor("#afc6d6"));
        rail.fixed=true;s.bodies<<rail;
    }
    auto carriage=box(QStringLiteral("开槽滑块 · 45×50×18"),45,50,18,{15,0,25},QColor("#da8f64"));
    for(float y:{-16.f,16.f})carriage=cut(carriage,box("tool",55,12.4,8,{15,y,18},Qt::gray));
    for(float x:{1.f,29.f})for(float y:{-7.f,7.f})carriage=cut(carriage,cylinder(2.5,26,{x,y,25}));
    s.bodies<<carriage;
    // X is travel axis. These two constraints align orientation and retain the vertical mounting offset.
    mate(s,s.bodies[1],carriage,MateType::Parallel);
    return s;
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);initializeKernel();
    const QString dir=app.arguments().value(1,"examples/mechanical");QDir().mkpath(dir);
    try {
        for(const auto &entry:QVector<QPair<QString,Scene>>{{"bearing",bearing()},{"gear",gear()},{"linear-guide",guide()}}) {
            auto scene=entry.second;QString error;
            if(!solve(scene,&error))throw std::runtime_error(error.toStdString());
            QString path=dir+"/"+entry.first;
            if(!saveProject(path+".afcad",scene,&error)||!exportStep(path+".step",scene,&error)||!exportGlb(path+".glb",scene,&error))throw std::runtime_error(error.toStdString());
            Scene loaded;if(!loadProject(path+".afcad",loaded,&error)||loaded.bodies.size()!=scene.bodies.size())throw std::runtime_error("project round trip failed");
            QVector<Body> step,mesh;
            if(!importStep(path+".step",step,&error)||!importMeshFile(path+".glb",mesh,&error)||step.size()!=scene.bodies.size()||mesh.size()!=scene.bodies.size())throw std::runtime_error("exchange round trip failed");
            QTextStream(stdout)<<entry.first<<": "<<scene.bodies.size()<<" bodies, project/STEP/GLB verified\n";
        }
        return 0;
    }catch(const std::exception&e){QTextStream(stderr)<<e.what()<<"\n";return 1;}
}
