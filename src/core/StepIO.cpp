#include "Kernel.h"
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Interface_Static.hxx>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <Quantity_Color.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <STEPControl_Reader.hxx>
#include <STEPControl_Writer.hxx>
#include <Standard_Failure.hxx>
#include <StepData_Protocol.hxx>
#include <StepData_StepWriter.hxx>
#include <TCollection_ExtendedString.hxx>
#include <TDF_LabelSequence.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XSControl_WorkSession.hxx>
#include <functional>
#include <sstream>
#include <stdexcept>

namespace af {
namespace {
QString labelName(const TDF_Label &label) {
    Handle(TDataStd_Name) name;
    if (!label.FindAttribute(TDataStd_Name::GetID(), name))
        return {};
    return QString::fromUtf16(reinterpret_cast<const ushort *>(name->Get().ToExtString()));
}
bool labelColor(const Handle(XCAFDoc_ColorTool) & colors, const TDF_Label &label, QColor &out) {
    Quantity_Color color;
    if (colors->GetColor(label, XCAFDoc_ColorGen, color) ||
        colors->GetColor(label, XCAFDoc_ColorSurf, color)) {
        out = QColor::fromRgbF(color.Red(), color.Green(), color.Blue());
        return true;
    }
    return false;
}
QString kernelFailure(const Standard_Failure &e) {
    return QString::fromUtf8(e.GetMessageString() ? e.GetMessageString() : "STEP error");
}
} // namespace
bool importStep(const QString &path, QVector<Body> &bodies, QString *error) {
    initializeKernel();
    try {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            throw std::runtime_error(file.errorString().toStdString());
        if (file.size() > 64 * 1024 * 1024)
            throw std::runtime_error("STEP 文件不得超过64 MiB。");
        QByteArray bytes = file.readAll();
        std::istringstream stream(std::string(bytes.constData(), size_t(bytes.size())));
        Handle(TDocStd_Document) document = new TDocStd_Document(TCollection_ExtendedString("BinXCAF"));
        STEPCAFControl_Reader reader;
        reader.SetNameMode(Standard_True);
        reader.SetColorMode(Standard_True);
        if (reader.ChangeReader().ReadStream("input.step", stream) != IFSelect_RetDone)
            throw std::runtime_error("STEP 解析失败。");
        reader.ChangeReader().SetSystemLengthUnit(1.0);
        if (!reader.Transfer(document))
            throw std::runtime_error("STEP 几何转换失败。");
        auto shapes = XCAFDoc_DocumentTool::ShapeTool(document->Main());
        auto colors = XCAFDoc_DocumentTool::ColorTool(document->Main());
        TDF_LabelSequence roots;
        shapes->GetFreeShapes(roots);
        QVector<Body> imported;
        std::function<void(const TDF_Label &, const gp_Trsf &, const QString &, const QColor &, int)> visit;
        visit = [&](const TDF_Label &label, const gp_Trsf &parent, const QString &prefix,
                    const QColor &inherited, int depth) {
            if (depth > 64 || imported.size() >= 300)
                throw std::runtime_error("STEP 装配层级或模型数量超过限制。");
            TDF_Label definition = label;
            gp_Trsf transform = parent;
            QString name = labelName(label);
            QColor color = inherited;
            if (shapes->IsReference(label)) {
                shapes->GetReferredShape(label, definition);
                transform = parent * shapes->GetLocation(label).Transformation();
                if (name.isEmpty())
                    name = labelName(definition);
                labelColor(colors, definition, color);
            }
            labelColor(colors, label, color);
            QString full = prefix.isEmpty() ? name : prefix + " / " + name;
            if (shapes->IsAssembly(definition)) {
                TDF_LabelSequence components;
                shapes->GetComponents(definition, components);
                for (int i = 1; i <= components.Length(); ++i)
                    visit(components.Value(i), transform, full, color, depth + 1);
                return;
            }
            auto shape = shapes->GetShape(definition);
            if (shape.IsNull())
                return;
            Body body = makeBody(Shape::Box, imported.size() + 1);
            body.shape = Shape::Solid;
            body.name = full.trimmed().isEmpty()
                            ? QFileInfo(path).completeBaseName() + QString(" %1").arg(imported.size() + 1)
                            : full.left(160);
            body.color = color;
            auto placed = BRepBuilderAPI_Transform(shape, transform, Standard_True).Shape();
            GProp_GProps properties;
            BRepGProp::VolumeProperties(placed, properties);
            const auto center = properties.CentreOfMass();
            gp_Trsf local;
            local.SetTranslation(gp_Vec(-center.X(), -center.Y(), -center.Z()));
            body.solid = createSolid(BRepBuilderAPI_Transform(placed, local, Standard_True).Shape());
            body.position = {float(center.X()), float(center.Y()), float(center.Z())};
            imported << body;
        };
        for (int i = 1; i <= roots.Length(); ++i)
            visit(roots.Value(i), gp_Trsf(), {}, QColor("#87bed9"), 0);
        if (imported.isEmpty())
            throw std::runtime_error("STEP 中没有有效闭合实体；当前不导入独立曲面或线框。");
        Scene candidate;
        candidate.bodies = imported;
        QString why;
        if (!candidate.validate(&why))
            throw std::runtime_error(why.toStdString());
        bodies = imported;
        return true;
    } catch (const Standard_Failure &e) {
        if (error)
            *error = kernelFailure(e);
    } catch (const std::exception &e) {
        if (error)
            *error = QString::fromUtf8(e.what());
    }
    return false;
}
bool exportStep(const QString &path, const Scene &scene, QString *error) {
    initializeKernel();
    try {
        QString why;
        if (!scene.validate(&why))
            throw std::runtime_error(why.toStdString());
        Handle(TDocStd_Document) document = new TDocStd_Document(TCollection_ExtendedString("BinXCAF"));
        auto shapes = XCAFDoc_DocumentTool::ShapeTool(document->Main());
        auto colors = XCAFDoc_DocumentTool::ColorTool(document->Main());
        int count = 0;
        for (const auto &body : scene.bodies) {
            if (!body.visible)
                continue;
            auto label = shapes->AddShape(worldShape(body), Standard_False);
            TDataStd_Name::Set(label,
                               TCollection_ExtendedString(
                                   reinterpret_cast<const Standard_ExtCharacter *>(body.name.utf16())));
            colors->SetColor(
                label,
                Quantity_Color(body.color.redF(), body.color.greenF(), body.color.blueF(), Quantity_TOC_RGB),
                XCAFDoc_ColorGen);
            ++count;
        }
        if (!count)
            throw std::runtime_error("没有可导出的可见实体。");
        STEPCAFControl_Writer writer;
        writer.SetNameMode(Standard_True);
        writer.SetColorMode(Standard_True);
        Interface_Static::SetCVal("write.step.unit", "MM");
        if (!writer.Transfer(document, STEPControl_AsIs))
            throw std::runtime_error("STEP 导出转换失败。");
        std::ostringstream stream;
        StepData_StepWriter streamWriter(writer.ChangeWriter().Model());
        streamWriter.SendModel(Handle(StepData_Protocol)::DownCast(writer.Writer().WS()->Protocol()));
        if (!streamWriter.Print(stream))
            throw std::runtime_error("STEP 流写入失败。");
        const std::string data = stream.str();
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) ||
            file.write(data.data(), qint64(data.size())) != qint64(data.size()) || !file.commit())
            throw std::runtime_error("STEP 文件原子写入失败。");
        return true;
    } catch (const Standard_Failure &e) {
        if (error)
            *error = kernelFailure(e);
    } catch (const std::exception &e) {
        if (error)
            *error = QString::fromUtf8(e.what());
    }
    return false;
}
} // namespace af
