#pragma once
#include "Scene.h"
namespace af {
struct ParameterField {
    QString key, label;
    double Parameters::*member;
};
inline QVector<ParameterField> parameterFields(Shape shape) {
    switch (shape) {
    case Shape::Box:
        return {{"width", QStringLiteral("长度 X"), &Parameters::width},
                {"depth", QStringLiteral("宽度 Y"), &Parameters::depth},
                {"height", QStringLiteral("高度 Z"), &Parameters::height}};
    case Shape::Sphere:
        return {{"radius", QStringLiteral("半径"), &Parameters::radius}};
    case Shape::Cylinder:
    case Shape::Cone:
        return {{"radius", shape == Shape::Cone ? QStringLiteral("底面半径") : QStringLiteral("半径"),
                 &Parameters::radius},
                {"height", QStringLiteral("高度"), &Parameters::height}};
    case Shape::Torus:
        return {{"radius", QStringLiteral("主半径"), &Parameters::radius},
                {"tubeRadius", QStringLiteral("次半径（管半径）"), &Parameters::tubeRadius}};
    case Shape::Tube:
        return {{"radius", QStringLiteral("外半径"), &Parameters::radius},
                {"tubeRadius", QStringLiteral("内半径"), &Parameters::tubeRadius},
                {"height", QStringLiteral("高度"), &Parameters::height}};
    case Shape::Frustum:
        return {{"radius", QStringLiteral("底半径"), &Parameters::radius},
                {"tubeRadius", QStringLiteral("顶半径"), &Parameters::tubeRadius},
                {"height", QStringLiteral("高度"), &Parameters::height}};
    case Shape::Ellipsoid:
        return {{"width", QStringLiteral("X 方向直径"), &Parameters::width},
                {"depth", QStringLiteral("Y 方向直径"), &Parameters::depth},
                {"height", QStringLiteral("Z 方向直径"), &Parameters::height}};
    case Shape::Wedge:
        return {{"width", QStringLiteral("长度 X"), &Parameters::width},
                {"depth", QStringLiteral("宽度 Y"), &Parameters::depth},
                {"height", QStringLiteral("高度 Z"), &Parameters::height}};
    default:
        return {};
    }
}
inline bool hasSubdivision(Shape shape) {
    return !parameterFields(shape).isEmpty() && shape != Shape::Box && shape != Shape::Wedge;
}
} // namespace af
