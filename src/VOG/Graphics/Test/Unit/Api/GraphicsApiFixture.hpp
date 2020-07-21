#pragma once

#include <VOG/Common/JSONContainer.hpp>
#include <VOG/Graphics/Api/GraphicsProvider.hpp>
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
        m_graphicsProvider = std::make_shared<Graphics::Api::GraphicsProvider>(parameters);
    }

    void
    TearDown() override
    {
        m_graphicsProvider.reset();
    }

protected:
    std::shared_ptr<Graphics::Api::GraphicsProvider> m_graphicsProvider;
};
} // namespace VOG::Tests