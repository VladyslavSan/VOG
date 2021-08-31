cmake_minimum_required(VERSION 3.10.0)

utility_declare_project(VOG.Application)

utility_add_target(
    LIBRARY
    LINK_PUBLIC VOG.Common VOG.Engine
    LINK_PRIVATE sfml-window
    FOLDER VOG
)
