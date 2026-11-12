#pragma once
#include <BRep_Builder.hxx>
#include <Quantity_Color.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <StepData_Protocol.hxx>
#include <StepData_StepWriter.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <TopoDS_Compound.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XSControl_WorkSession.hxx>
#include <sstream>
inline bool writeNestedFixture(const QString &path) {
    Handle(TDocStd_Document) doc = new TDocStd_Document(TCollection_ExtendedString("BinXCAF"));
    auto tool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
    auto colors = XCAFDoc_DocumentTool::ColorTool(doc->Main());
    auto body = af::makeBody(af::Shape::Box);
    body.params.width = body.params.depth = body.params.height = 20;
    auto part = tool->AddShape(af::solidData(body)->shape, Standard_False);
    colors->SetColor(part, Quantity_Color(0.2, 0.4, 0.8, Quantity_TOC_RGB), XCAFDoc_ColorGen);
    BRep_Builder builder;
    TopoDS_Compound topShape, nestedShape;
    builder.MakeCompound(topShape);
    builder.MakeCompound(nestedShape);
    auto top = tool->AddShape(topShape, Standard_True), nested = tool->AddShape(nestedShape, Standard_True);
    TDataStd_Name::Set(top, TCollection_ExtendedString("Root"));
    for (int i = 0; i < 2; ++i) {
        gp_Trsf place;
        place.SetTranslation(gp_Vec(5, i * 30, 0));
        auto item = tool->AddComponent(nested, part, TopLoc_Location(place));
        TDataStd_Name::Set(item, TCollection_ExtendedString(i ? "Instance B" : "Instance A"));
    }
    gp_Trsf place;
    place.SetTranslation(gp_Vec(100, 0, 0));
    auto assembly = tool->AddComponent(top, nested, TopLoc_Location(place));
    TDataStd_Name::Set(assembly, TCollection_ExtendedString("Nested"));
    tool->UpdateAssemblies();
    STEPCAFControl_Writer writer;
    if (!writer.Transfer(doc, STEPControl_AsIs))
        return false;
    std::ostringstream stream;
    StepData_StepWriter step(writer.ChangeWriter().Model());
    step.SendModel(Handle(StepData_Protocol)::DownCast(writer.Writer().WS()->Protocol()));
    if (!step.Print(stream))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    auto bytes = stream.str();
    return file.write(bytes.data(), qint64(bytes.size())) == qint64(bytes.size());
}
