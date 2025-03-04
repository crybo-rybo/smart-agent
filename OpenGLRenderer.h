#pragma once

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <string>

class OpenGLRenderer {
public:
    OpenGLRenderer(GLFWwindow* window, int width, int height);
    ~OpenGLRenderer();
    
    void initImGui();
    void beginFrame();
    void endFrame();
    
private:
    GLFWwindow* window;
    int width;
    int height;
    
    // OpenGL context version
    const char* glsl_version = "#version 150";
}; 