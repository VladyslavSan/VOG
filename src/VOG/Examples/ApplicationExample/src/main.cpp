#include <VOG/Application/Application.hpp>

#include <iostream>
#include <stdexcept>

int
main()
{
    try
    {
        VOG::Application::Application app("My window", 1280, 720, {},
                                          {"VK_LAYER_KHRONOS_validation"});

        app.Run();
    }
    catch (const std::exception& ex)
    {
        std::cout << ex.what() << std::endl;
    }
    return 0;
}
