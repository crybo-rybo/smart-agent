/**
 * @file OpenGLRenderer.cpp
 * @brief Handles OpenGL rendering setup and ImGui integration for the application's graphical interface.
 */
#include "OpenGLRenderer.h"
#include <stdexcept>
#include <iostream>

OpenGLRenderer::OpenGLRenderer(GLFWwindow* window, int width, int height) 
    : window(window), width(width), height(height) {
    // Make the window's context current
    glfwMakeContextCurrent(window);
    
    // Enable vsync
    glfwSwapInterval(1);
}

OpenGLRenderer::~OpenGLRenderer() {
    // Cleanup ImGui
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void OpenGLRenderer::initImGui() {
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    
    // Setup dark theme for Dear ImGui
    ImGui::StyleColorsDark();
    
    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);
}

void OpenGLRenderer::beginFrame() {
    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    
    // Clear the framebuffer
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void OpenGLRenderer::endFrame() {
    // Render ImGui
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    
    // Swap front and back buffers
    glfwSwapBuffers(window);
} 