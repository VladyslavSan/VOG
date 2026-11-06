# Fails if a header under HEADER_DIRECTORY names a Graphics or Vulkan type: `Graphics::`, `vk::`, a
# name that starts with `Vk`, or an include of VOG/Graphics. Prose may say "Vulkan".
file(GLOB_RECURSE headers ${HEADER_DIRECTORY}/*.hpp)
if(NOT headers)
    message(FATAL_ERROR "No header under ${HEADER_DIRECTORY}: the check would pass for nothing.")
endif()

set(found FALSE)
foreach(header IN LISTS headers)
    file(STRINGS ${header} lines)
    foreach(line IN LISTS lines)
        if(line MATCHES "Graphics::|vk::|(^|[^A-Za-z_])Vk[A-Z]|VOG/Graphics")
            message(SEND_ERROR "${header}: ${line}")
            set(found TRUE)
        endif()
    endforeach()
endforeach()

if(found)
    message(FATAL_ERROR "A public Engine header names a Graphics or Vulkan type.")
endif()
