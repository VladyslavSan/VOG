#include <VOG/Application/Application.hpp>

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

// prints the explanatory string of an exception. If the exception is nested,
// recurses to print the explanatory of the exception it holds
void
print_exception(std::ostream& stream, const std::exception& e, std::uint8_t level = 0)
{
    // Exception message can be multiline.
    const auto errorString = std::string_view{e.what()};

    stream << std::string(4u * level, ' ') << "Exception: " << typeid(e).name() << std::endl;

    std::size_t lastIndex = 0u;
    for (;;)
    {
        std::size_t idx    = errorString.find('\n', lastIndex);
        std::size_t end    = idx - lastIndex;
        auto        substr = errorString.substr(lastIndex, end);

        stream << std::string(4u * level, ' ') << substr << std::endl;
        lastIndex = idx + 1;

        if (idx == std::string_view::npos || lastIndex >= errorString.size())
        {
            break;
        }
    }

    try
    {
        std::rethrow_if_nested(e);
    }
    catch (const std::exception& nestedException)
    {
        print_exception(stream, nestedException, level + 1);
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
        VOG::Application::Application app(
            "My window", 1280, 720, {}, {}, {argv, static_cast<std::size_t>(argc)});

        app.run();
    }
    catch (const std::exception& ex)
    {
        print_exception(std::cerr, ex);
    }

    return 0;
}
