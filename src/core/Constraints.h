#pragma once
#include "Scene.h"
namespace af {
struct Residual {
    double linear = 0, angular = 0;
    bool satisfied() const {
        return linear <= 0.001 && angular <= 0.0001;
    }
};
Residual residual(const Scene &scene, const Mate &mate);
// Transactional: the input scene changes only on complete success.
bool solve(Scene &scene, QString *error = nullptr);
} // namespace af
