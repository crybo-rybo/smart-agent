/**
 * @file Application.h
 * @brief Declares the Application class for managing the main application lifecycle.
 */
#pragma once

#include "OpenGLRenderer.h"
#include "ContextManager.h"
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <future>


class Application {
public:
    Application();
    ~Application();
    
    void run();
    
private:
    void initWindow();
    void initOpenGL();
    void initImGui();
    void mainLoop();
    void drawUI();
    void drawShutdownWindow();  // New method for shutdown window
    
    // New method to fetch LLMs
    void fetchLLMs();

    // New methods for LLM interaction
    void startLLM(const std::string& llmName);
    void stopLLM();
    void sendPrompt(const std::string& prompt, bool keepAlive = true);
    void streamLLMResponse(const std::string& llmName, const std::string& prompt, bool keepAlive = true);

    GLFWwindow* window;
    std::unique_ptr<OpenGLRenderer> renderer;
    std::unique_ptr<ContextManager> contextManager;
    
    const int WIDTH = 800;
    const int HEIGHT = 600;
    const std::string APP_NAME = "Smart Agent";

    // Store LLM information
    std::vector<std::pair<std::string, std::string>> llms; // Pair of LLM name and size
    std::string currentLLM; // Currently running LLM
    bool isLLMRunning = false;
    
    // Prompt and response handling
    std::string userPrompt;
    std::string llmResponse;
    std::string conversationHistory;
    bool showPromptWindow = false;
    
    // Threading for LLM communication
    std::future<void> llmFuture;
    std::atomic<bool> stopRequested{false};
    std::mutex responseMutex;

    // Add a new member variable to track when we're waiting for a response
    bool isWaitingForResponse = false;
    
    // UI update flag for smoother threading
    std::atomic<bool> uiNeedsUpdate{false};
    
    // Shutdown handling
    bool isShuttingDown = false;
    bool showShutdownWindow = false;
};
