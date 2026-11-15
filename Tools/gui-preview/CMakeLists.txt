# Renders the proposed panel to a PNG with JUCE. Test tooling only; not part of the plug-in.
#   cmake -S Tools/gui-preview -B build/gui-preview -DCMAKE_BUILD_TYPE=Release && cmake --build build/gui-preview --target PanelPreview
#   PanelPreview out.png [scale, 2 = the reference images' pixels] [e | b palette] [side]
cmake_minimum_required(VERSION 3.22)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
project(PanelPreview VERSION 0.1 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../deps/JUCE" juce)
juce_add_console_app(PanelPreview PRODUCT_NAME "PanelPreview")
target_sources(PanelPreview PRIVATE preview.cpp)
target_compile_definitions(PanelPreview PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(PanelPreview PRIVATE juce::juce_gui_basics juce::juce_graphics juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
