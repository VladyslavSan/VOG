#include <VOG/Application/Application.hpp>
#include <VOG/Engine/Renderer/Renderer.hpp>
#include <VOG/Engine/Scene/Scene.hpp>
#include <VOG/Math/Math.hpp>

#include <boost/program_options.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include "SwingBehaviour.hpp"

namespace po = boost::program_options;

namespace
{
struct VertexData
{
    VOG::Math::Vector2f position;
    VOG::Math::Vector4f color;
};

// clang-format off
constexpr VertexData gVertexData[] = {
  // Red bottom left square
  {{-1.0, -1.0}, {1.0, 0.0, 0.0, 1.0}},
  {{-1.0,  0.0}, {1.0, 0.0, 0.0, 1.0}},
  {{ 0.0,  0.0}, {1.0, 0.0, 0.0, 1.0}},
  {{ 0.0, -1.0}, {1.0, 0.0, 0.0, 1.0}},
  // Green top right square
  {{ 0.0,  0.0}, {0.0, 1.0, 0.0, 1.0}},
  {{ 0.0,  1.0}, {0.0, 1.0, 0.0, 1.0}},
  {{ 1.0,  1.0}, {0.0, 1.0, 0.0, 1.0}},
  {{ 1.0,  0.0}, {0.0, 1.0, 0.0, 1.0}},
};

constexpr std::uint32_t gIndexData[] = {
  0, 1, 2, 2, 3, 0, // Red
  4, 5, 6, 4, 6, 7, // Green
};
// clang-format on

/** Rate at which the quads swing back and forth, in radians per second. */
constexpr float gRotationRate = 1.6f;

constexpr float gQuadHalfSizePixels = 300.0f;
constexpr float gQuadDistance       = 20.0f;
constexpr float gMaxRotationDegrees = 90.0f;
} // namespace

// prints the explanatory string of an exception. If the exception is nested,
// recurses to print the explanatory of the exception it holds
void
printException(std::ostream& stream, const std::exception& exception, std::uint8_t level = 0)
{
    // Exception message can be multiline.
    const auto errorString = std::string_view{exception.what()};

    const std::string indent(std::size_t{4} * level, ' ');

    stream << indent << "Exception: " << typeid(exception).name() << '\n';

    std::size_t lastIndex = 0u;
    for (;;)
    {
        std::size_t idx    = errorString.find('\n', lastIndex);
        std::size_t end    = idx - lastIndex;
        auto        substr = errorString.substr(lastIndex, end);

        stream << indent << substr << '\n';
        lastIndex = idx + 1;

        if (idx == std::string_view::npos || lastIndex >= errorString.size())
        {
            break;
        }
    }

    try
    {
        std::rethrow_if_nested(exception);
    }
    catch (const std::exception& nestedException)
    {
        printException(stream, nestedException, level + 1);
    }
    catch (...) // NOLINT(bugprone-empty-catch)
    {
        // A nested exception that is not a std::exception has nothing to print.
    }
}

int
main(int argc, const char** argv) // NOLINT(bugprone-exception-escape)
{
    try
    {
        po::options_description description = {};
        description.add_options()("shader-storage-path",
                                  po::value<std::string>()->required(),
                                  "Path to the shader directory containing config.json.");

        constexpr int kWindowWidth  = 1280;
        constexpr int kWindowHeight = 720;

        po::variables_map varMap;
        try
        {
            po::store(po::parse_command_line(argc, argv, description), varMap);

            // Required check happens here
            po::notify(varMap);
        }
        catch (const po::required_option& ex)
        {
            std::cerr << "Error: " << ex.what() << '\n';
            return 1;
        }
        catch (const po::error& ex)
        {
            std::cerr << "Error: " << ex.what() << '\n';
            return 1;
        }

        VOG::Application::Application app({
            .title             = "My window",
            .width             = kWindowWidth,
            .height            = kWindowHeight,
            .shaderStoragePath = varMap["shader-storage-path"].as<std::string>(),
        });

        constexpr float kNearPlane = 1.0f;
        constexpr float kFarPlane  = 200.0f;

        VOG::Engine::Renderer& renderer = app.getRenderer();
        VOG::Engine::Scene&    scene    = app.getScene();

        const auto quads    = renderer.createMesh({
            .vertices   = std::as_bytes(std::span{gVertexData}),
            .indices    = gIndexData,
            .stride     = sizeof(VertexData),
            .attributes = {{VOG::Engine::VertexFormat::eFloat2, 0u},
                           {VOG::Engine::VertexFormat::eFloat4, offsetof(VertexData, color)}},
        });
        const auto material = renderer.createMaterial({.shader = "WorldSpace"});

        scene.createGameObject().addComponent<VOG::Engine::Camera>(
            VOG::Engine::Camera{.nearPlane = kNearPlane, .farPlane = kFarPlane});

        VOG::Engine::GameObject quadObject = scene.createGameObject();
        quadObject.getTransform().position = {0.0f, 0.0f, gQuadDistance};
        quadObject.getTransform().scale    = {gQuadHalfSizePixels, gQuadHalfSizePixels, 1.0f};
        quadObject.addComponent<VOG::Engine::MeshRenderer>(
            VOG::Engine::MeshRenderer{.mesh = quads, .material = material});
        quadObject.addBehaviour<SwingBehaviour>(gRotationRate, gMaxRotationDegrees);

        app.run();
    }
    catch (const std::exception& ex)
    {
        printException(std::cerr, ex);
        return EXIT_FAILURE;
    }

    return 0;
}
