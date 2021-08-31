cmake_minimum_required(VERSION 3.10.0)

utility_declare_project(VOG.Engine)

utility_add_target(
    LIBRARY
    LINK_PUBLIC VOG.Common
    LINK_PRIVATE VOG.Graphics
    FOLDER VOG
)