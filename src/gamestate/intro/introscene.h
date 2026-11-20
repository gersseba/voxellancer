#pragma once

#include "display/scene.h"


class Intro;
class VoxelRenderer;

class IntroScene : public Scene {
public:
    IntroScene(Intro& intro);
    ~IntroScene() override;

    void update(float deltaSec) override;


protected:
    Intro& m_intro;
    std::shared_ptr<VoxelRenderer> m_voxelRenderer;


    void drawImpl(const Camera& camera) const override;
};

