#include <VOG/Application/Application.hpp>

#include <iostream>
#include <stdexcept>

int
main(int argc, const char** argv)
{
    {
        VOG::Application::Application app(
            "My window", 1280, 720, {}, {}, {argv, static_cast<std::size_t>(argc)});

        app.run();
    }

    return 0;
}
