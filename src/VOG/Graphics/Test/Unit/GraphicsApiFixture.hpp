#pragma once

#include <VOG/Common/JSONContainer.hpp>
#include <VOG/Graphics/GraphicsProvider.hpp>
#include <gtest/gtest.h>

namespace VOG::Tests
{
class GraphicsProviderFixture : public testing::Test
{

public:
    void
    SetUp() override
    {
        Common::JSONContainer parameters;
        mGraphicsProvider = std::make_shared<Graphics::GraphicsProvider>(parameters);
    }

    void
    TearDown() override
    {
        mGraphicsProvider.reset();
    }

protected:
    std::shared_ptr<Graphics::GraphicsProvider> mGraphicsProvider;
};
} // namespace VOG::Tests