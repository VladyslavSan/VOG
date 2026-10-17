#include "SwingBehaviour.hpp"

#include <VOG/Math/Math.hpp>

#include <cmath>

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
SwingBehaviour::SwingBehaviour(float rate, float maxDegrees)
    : mRate{rate}
    , mMaxDegrees{maxDegrees}
{
}

void
SwingBehaviour::update(const VOG::Engine::FrameContext& frame)
{
    const float timeFactor = std::sin(static_cast<float>(frame.timeSeconds) * mRate);

    getGameObject().getTransform().rotation = glm::angleAxis(glm::radians(timeFactor * mMaxDegrees),
                                                             VOG::Math::Vector3f{0.0f, 0.0f, 1.0f});
}
