cmake_minimum_required(VERSION 3.11.0)

include(FetchContent)

FetchContent_Declare(
    sfml
    GIT_REPOSITORY  https://github.com/SFML/SFML.git
    GIT_TAG         5ba65805688f86093464f6dc32b8a768a76dedb1 # free some "stable" commit
)
FetchContent_MakeAvailable(sfml)