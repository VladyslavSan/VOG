#include <VOG/Application/Application.hpp>

#include <boost/program_options.hpp>

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace po = boost::program_options;

// prints the explanatory string of an exception. If the exception is nested,
// recurses to print the explanatory of the exception it holds
void
printException(std::ostream& stream, const std::exception& exception, std::uint8_t level = 0)
{
    // Exception message can be multiline.
    const auto errorString = std::string_view{exception.what()};

    stream << std::string(4u * level, ' ') << "Exception: " << typeid(exception).name() << '\n';

    std::size_t lastIndex = 0u;
    for (;;)
    {
        std::size_t idx    = errorString.find('\n', lastIndex);
        std::size_t end    = idx - lastIndex;
        auto        substr = errorString.substr(lastIndex, end);

        stream << std::string(4u * level, ' ') << substr << '\n';
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
    catch (...)
    {
    }
}

int
main(int argc, const char** argv)
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

        app.run();
    }
    catch (const std::exception& ex)
    {
        printException(std::cerr, ex);
    }

    return 0;
}
