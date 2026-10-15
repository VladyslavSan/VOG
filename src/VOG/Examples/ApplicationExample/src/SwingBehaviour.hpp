#pragma once

#include <VOG/Engine/Scene/Scene.hpp>

/** Swings its object back and forth about the Z axis. */
class SwingBehaviour final : public VOG::Engine::Behaviour
{
public:
    /** @p rate in radians per second of the sine; @p maxDegrees is the largest angle. */
    SwingBehaviour(float rate, float maxDegrees);

    void update(const VOG::Engine::FrameContext& frame) override;

private:
    float mRate;
    float mMaxDegrees;
};
