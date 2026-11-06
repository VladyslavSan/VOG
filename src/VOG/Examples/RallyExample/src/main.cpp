#include <VOG/Application/Application.hpp>
#include <VOG/Engine/Renderer/Renderer.hpp>
#include <VOG/Engine/Scene/Scene.hpp>
#include <VOG/Math/Math.hpp>

#include <boost/program_options.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <format>
#include <iostream>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <typeinfo>
#include <utility>
#include <vector>

#include "Assets.hpp"
#include "Behaviours.hpp"
#include "Game.hpp"

namespace po = boost::program_options;

namespace
{
using namespace VOG;

constexpr std::uint32_t gWindowWidth  = 1280u;
constexpr std::uint32_t gWindowHeight = 720u;

constexpr auto gImporterPoll    = std::chrono::milliseconds{5};
constexpr auto gImporterTimeout = std::chrono::seconds{10};

constexpr Math::Vector4f gCourtPlaceholderTint{0.07f, 0.17f, 0.17f, 1.0f};
constexpr Math::Vector4f gAmber{1.0f, 0.72f, 0.25f, 1.0f};
constexpr Math::Vector4f gViolet{0.85f, 0.55f, 1.0f, 1.0f};
constexpr Math::Vector4f gSky{0.4f, 0.75f, 1.0f, 1.0f};
constexpr Math::Vector4f gGhostTint{1.0f, 0.8f, 0.5f, 0.35f};
constexpr Math::Vector4f gPipLitTint{1.0f, 0.9f, 0.4f, 0.95f};
constexpr Math::Vector4f gPipDarkTint{1.0f, 1.0f, 1.0f, 0.18f};

/** Prints the message of an exception, and of the exceptions nested in it. */
void
printException(std::ostream& stream, const std::exception& exception, std::uint8_t level = 0)
{
    stream << std::string(std::size_t{4} * level, ' ') << "Exception: " << typeid(exception).name()
           << '\n'
           << std::string(std::size_t{4} * level, ' ') << exception.what() << '\n';

    try
    {
        std::rethrow_if_nested(exception);
    }
    catch (const std::exception& nested)
    {
        printException(stream, nested, level + 1);
    }
    catch (...) // NOLINT(bugprone-empty-catch)
    {
        // A nested exception that is not a std::exception has nothing to print.
    }
}

/** The texture of @p image, made on the calling thread, which may be any. */
Engine::Texture
makeTexture(Engine::Renderer&   renderer,
            std::string         name,
            const Rally::Image& image,
            Engine::Filter      filter)
{
    return renderer.createTexture({.name   = std::move(name),
                                   .pixels = image.pixels,
                                   .extent = image.extent,
                                   .filter = filter});
}

/** A material of the shader Textured: the texture times the tint. */
Engine::Material
makeMaterial(Engine::Renderer&          renderer,
             std::string                name,
             const Engine::Texture&     texture,
             const Math::Vector4f&      tint,
             const Engine::RenderState& state = {})
{
    return renderer.createMaterial({.name       = std::move(name),
                                    .shader     = renderer.getShader("Textured"),
                                    .parameters = {{"tint", tint}, {"albedo", texture}},
                                    .state      = state});
}

/**
 * Makes the court of the game on a worker thread and hands it to the main thread: the court is
 * built from pixels, copied to the GPU in the background, and swapped for the placeholder when the
 * copy has finished, so the game starts at once and the swap does not stall a frame. A failure
 * reaches the main thread as a task that throws, which ends run().
 */
void
importCourt(const std::stop_token& stop, Application::Application& app, Engine::Scene& scene)
{
    try
    {
        Engine::Renderer&     renderer = app.getRenderer();
        const Rally::Image    image    = Rally::makeCourt();
        const Engine::Texture texture =
            makeTexture(renderer, "court", image, Engine::Filter::eNearest);

        const auto deadline = std::chrono::steady_clock::now() + gImporterTimeout;
        while (!texture.isResident() && !stop.stop_requested())
        {
            if (std::chrono::steady_clock::now() > deadline)
            {
                throw std::runtime_error{"The court texture was not copied to the GPU in time."};
            }
            std::this_thread::sleep_for(gImporterPoll);
        }

        const Engine::Material material =
            makeMaterial(renderer, "court", texture, Math::Vector4f{1.0f});
        app.post(
            [&scene, material]
            {
                Rally::findOrThrow(scene, "court").getComponent<Engine::MeshRenderer>().material =
                    material;
            });
    }
    catch (...)
    {
        app.post(
            [failure = std::current_exception()]
            {
                std::rethrow_exception(failure);
            });
    }
}

int
play(const po::variables_map& options)
{
    Application::Application app({
        .title    = "Rally",
        .size     = {.width = gWindowWidth, .height = gWindowHeight},
        .renderer = {.shaderPath = options["shader-storage-path"].as<std::string>(),
                     .validation = options.contains("validation"),
                     // Any thread may call it. A message is a mistake of the game or of the engine.
                     .onValidationMessage =
                         [](std::string_view message)
                     {
                         std::cerr << "Vulkan: " << message << '\n';
                     }},
    });

    Engine::Renderer& renderer = app.getRenderer();
    Engine::Scene&    scene    = app.getScene();

    const Engine::Mesh quad =
        renderer.createMesh({.name     = "quad",
                             .vertices = std::as_bytes(std::span{Rally::gQuadVertices}),
                             .indices  = Rally::gQuadIndices,
                             .layout   = Rally::quadLayout()});

    const Engine::Texture white =
        makeTexture(renderer, "white", Rally::makeWhitePixel(), Engine::Filter::eLinear);
    const Engine::Texture stripe =
        makeTexture(renderer, "paddle", Rally::makePaddle(), Engine::Filter::eNearest);
    const Engine::Texture bumperStripes =
        makeTexture(renderer, "bumper", Rally::makeBumper(), Engine::Filter::eNearest);
    const Engine::Texture ball =
        makeTexture(renderer, "ball", Rally::makeBall(), Engine::Filter::eLinear);

    // Blended materials do not write depth, so the trail, the ball and the marks can overlap.
    const Engine::RenderState blended{.blend = Engine::BlendMode::eAlpha,
                                      .depth = Engine::DepthMode::eTestOnly};

    const Engine::Material court =
        makeMaterial(renderer, "court placeholder", white, gCourtPlaceholderTint);
    const Engine::Material left   = makeMaterial(renderer, "left paddle", stripe, gAmber);
    const Engine::Material right  = makeMaterial(renderer, "right paddle", stripe, gSky);
    const Engine::Material bumper = makeMaterial(renderer, "bumper", bumperStripes, gViolet);
    const Engine::Material ballMat =
        makeMaterial(renderer, "ball", ball, Math::Vector4f{1.0f}, blended);
    const Engine::Material ghost = makeMaterial(renderer, "ghost", ball, gGhostTint, blended);
    const Engine::Material lit   = makeMaterial(renderer, "lit pip", white, gPipLitTint, blended);
    const Engine::Material dark  = makeMaterial(renderer, "dark pip", white, gPipDarkTint, blended);

    // A mesh that a material cannot be drawn with is a mistake to find now, not in a frame.
    for (const Engine::Material* material :
         {&court, &left, &right, &bumper, &ballMat, &ghost, &lit, &dark})
    {
        if (const auto compatible = renderer.checkDraw(quad, *material); !compatible)
        {
            throw std::runtime_error{compatible.error()};
        }
    }

    Rally::buildGame(scene,
                     {.quad   = quad,
                      .court  = court,
                      .left   = left,
                      .right  = right,
                      .bumper = bumper,
                      .ball   = ballMat,
                      .ghost  = ghost,
                      .lit    = lit,
                      .dark   = dark},
                     options.contains("frames") ? options["frames"].as<int>() : 0);

    // After the application: joined before it goes, so that nothing creates a resource while the
    // renderer is destroyed.
    std::jthread importer{[&app, &scene](const std::stop_token& stop)
                          {
                              importCourt(stop, app, scene);
                          }};

    std::cout << "Rally: W/S and Up/Down move the paddles, space serves, escape quits.\n";
    app.run();

    return EXIT_SUCCESS;
}
} // namespace

int
main(int argc, const char** argv) // NOLINT(bugprone-exception-escape)
{
    try
    {
        po::options_description description = {};
        description.add_options()("shader-storage-path",
                                  po::value<std::string>()->required(),
                                  "Path to the shader directory (Name.vert.glsl, Name.frag.glsl "
                                  "and include/).")("validation",
                                                    "Turn on the Vulkan validation layer.")(
            "frames",
            po::value<int>(),
            "Quit after this many frames (to check that the game runs).");

        po::variables_map options;
        try
        {
            po::store(po::parse_command_line(argc, argv, description), options);
            po::notify(options);
        }
        catch (const po::error& error)
        {
            std::cerr << "Error: " << error.what() << '\n';

            return EXIT_FAILURE;
        }

        return play(options);
    }
    catch (const std::exception& exception)
    {
        printException(std::cerr, exception);

        return EXIT_FAILURE;
    }
}
