#pragma once

#include <VOG/Common/JSONContainer.hpp>
#include <VOG/Graphics/GraphicsProvider.hpp>

#include <gtest/gtest.h>

namespace VOG::Tests
{
class GraphicsProviderFixture : public testing::Test
{
public:
    GraphicsProviderFixture()
        : mGraphicsProvider{Graphics::GraphicsProvider::create({})}
    {
    }

protected:
    std::shared_ptr<Graphics::GraphicsProvider> mGraphicsProvider;
};
} // namespace VOG::Tests