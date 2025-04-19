/**
 * @file Application.h
 * @brief Manages the main application lifecycle, including window management, LLM interaction, and UI rendering.
 */
#ifndef APPLICATION_H
#define APPLICATION_H

#include "OpenGLRenderer.h"
#include "ContextManager.h"
#include "ModelManager.h"
#include "ModelInterface.h"
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <future>


class Application {
public:
    /**
     * @brief Constructor for the Application class
     * 
     * Initializes the application window, OpenGL renderer, ImGui, and loads available models
     */
    Application();
    
    /**
     * @brief Destructor for the Application class
     * 
     * Ensures any running LLM is properly stopped before application shutdown
     */
    ~Application();
    
    /**
     * @brief Start the application main loop
     * 
     * Entry point for running the application after initialization
     */
    void run();
    
private:
    /**
     * @brief Initialize the GLFW window
     * 
     * Sets up the GLFW window with proper OpenGL context and configuration
     */
    void initWindow();
    
    /**
     * @brief Initialize the OpenGL renderer
     * 
     * Creates and configures the OpenGL renderer with the application window
     */
    void initOpenGL();
    
    /**
     * @brief Initialize the ImGui UI framework
     * 
     * Sets up ImGui for rendering the user interface
     */
    void initImGui();
    
    /**
     * @brief Main application loop
     * 
     * Handles rendering and event processing in a continuous loop
     */
    void mainLoop();
    
    /**
     * @brief Draw the application user interface
     * 
     * Renders the complete UI including context manager, LLM selector, and conversation window
     */
    void drawUI();
    
    /**
     * @brief Draw the shutdown confirmation window
     * 
     * Displays a modal window while waiting for the LLM to shut down
     */
    void drawShutdownWindow();
    
    /**
     * @brief Fetch available LLM models
     * 
     * Queries the ModelManager to get a list of all available LLM models
     */
    void fetchLLMs();

    /**
     * @brief Start a specific LLM model
     * 
     * @param llmName The name of the LLM model to start
     * 
     * Loads and initializes the specified LLM model for use
     */
    void startLLM(const std::string& llmName);
    
    /**
     * @brief Stop the currently running LLM model
     * 
     * Unloads the current model and cleans up resources
     */
    void stopLLM();
    
    /**
     * @brief Send a prompt to the currently running LLM
     * 
     * @param prompt The text prompt to send to the LLM
     * @param keepAlive Whether to keep the LLM loaded after processing the prompt (default: true)
     * 
     * Sends the user's prompt to the LLM and initiates response streaming
     */
    void sendPrompt(const std::string& prompt, bool keepAlive = true);
    
    /**
     * @brief Stream the LLM's response to a prompt
     * 
     * @param llmName The name of the LLM model generating the response
     * @param prompt The text prompt sent to the LLM
     * @param keepAlive Whether to keep the LLM loaded after processing (default: true)
     * 
     * Handles the streaming of tokens from the LLM's response, updating the UI in real-time
     */
    void streamLLMResponse(const std::string& llmName, const std::string& prompt, bool keepAlive = true);
    
    /**
     * @brief Handle the addition of a file to the context
     * 
     * @param filePath The path of the file being added to the context
     * 
     * Processes a newly added file and sends its content to the LLM as context
     */
    void handleFileAdded(const std::string& filePath);

    // Window and rendering
    GLFWwindow* m_window;
    std::unique_ptr<OpenGLRenderer> m_renderer;
    std::unique_ptr<ContextManager> m_contextManager;

    // Model management
    ModelManager* m_modelManager;
    ModelInterface* m_currentModelInterface;

    // Window dimensions and application name
    const int m_WIDTH = 1000;
    const int m_HEIGHT = 800;
    const std::string m_APP_NAME = "Smart Agent";

    // LLM information
    std::vector<std::pair<std::string, std::string>> m_llms; // Pair of LLM name and size
    std::string m_currentLLM; // Currently running LLM
    bool m_isLLMRunning = false;
    
    // Prompt and response handling
    std::string m_userPrompt;
    std::string m_llmResponse;
    std::string m_conversationHistory;
    bool m_showPromptWindow = false;
    
    // Threading for LLM communication
    std::future<void> m_llmFuture;
    std::atomic<bool> m_stopRequested{false};
    std::mutex m_responseMutex;

    // Response tracking
    bool m_isWaitingForResponse = false;
    
    // UI update flag for smoother threading
    std::atomic<bool> m_uiNeedsUpdate{false};
    
    // Shutdown handling
    bool m_isShuttingDown = false;
    bool m_showShutdownWindow = false;
};

#endif // APPLICATION_H
