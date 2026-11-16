for (auto shape : {Shape::Tube, Shape::Frustum, Shape::Ellipsoid, Shape::Wedge})
    test("new parametric solid: " + shapeKey(shape), [shape] {
        Scene scene;
        auto b = makeBody(shape);
        scene.bodies << b;
        CHECK(scene.validate());
        CHECK(volume(b) > 0 && surfaceArea(b) > 0);
        CHECK(solidData(b)->solids == 1);
        double expected = 0;
        const double pi = 3.141592653589793;
        if (shape == Shape::Tube)
            expected = pi * (225 - 25) * 30;
        if (shape == Shape::Frustum)
            expected = pi * 30 * (225 + 75 + 25) / 3;
        if (shape == Shape::Ellipsoid)
            expected = 4 * pi * 15 * 15 * 15 / 3;
        if (shape == Shape::Wedge)
            expected = 30 * 30 * 30 / 2.;
        CHECK(std::abs(volume(b) - expected) < expected * 1e-5);
        Scene loaded;
        CHECK(deserialize(serialize(scene), loaded));
        CHECK(loaded.bodies[0].shape == shape);
        CHECK(std::abs(volume(loaded.bodies[0]) - expected) < expected * 1e-5);
    });
