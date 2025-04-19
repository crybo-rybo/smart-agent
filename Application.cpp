/**
 * @file Application.cpp
 * @brief Manages the main application lifecycle, including window management, LLM interaction, and UI rendering.
 */
#include "Application.h"
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <fstream>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <fcntl.h>
#include <filesystem>

const std::string MODELS_DIR = "/Users/conorrybacki/.models/";

/**
 * @brief Constructor for the Application class
 * 
 * Initializes the application window, OpenGL renderer, ImGui, and loads available models
 */
Application::Application() : 
m_window(nullptr),
m_renderer(nullptr),
m_contextManager(nullptr),
m_modelManager(nullptr) 
{
    initWindow();
    initOpenGL();
    initImGui();
    m_contextManager = std::make_unique<ContextManager>();
    m_modelManager = ModelManager::getInstance();
    m_modelManager->setModelDirectory(MODELS_DIR);
    fetchLLMs();
}

/**
 * @brief Destructor for the Application class
 * 
 * Ensures any running LLM is properly stopped before application shutdown
 */
Application::~Application() {
  // Make sure to stop any running LLM
  if (m_isLLMRunning) 
  {
    stopLLM();
  }
}

/**
 * @brief Fetch available LLM models
 * 
 * Queries the ModelManager to get a list of all available LLM models
 */
void Application::fetchLLMs() 
{
  auto fetchResp = m_modelManager->fetchModels();
  if(fetchResp.has_value())
  {
      for(const auto& mod : fetchResp.value())
      {
          m_llms.emplace_back(mod);
      }
  }
  else
  {
    std::cout << "Error fetching model names!" << std::endl;
  }
}

/**
 * @brief Start a specific LLM model
 * 
 * @param llmName The name of the LLM model to start
 * 
 * Loads and initializes the specified LLM model for use
 */
void Application::startLLM(const std::string& llmName) 
{
  // Start the model specified by the name selected - note this call will
  // handle appending model directory path and ".gguf" to model name
  auto loadResp = m_modelManager->loadModel(llmName);
  if(loadResp.has_value())
  {
    m_currentModelInterface = loadResp.value();
    m_currentLLM = llmName;
    m_isLLMRunning = true;
    m_showPromptWindow = true;
  }
  else
  {
    #ifdef _DEBUG
      std::cout << "Error loading model : " << llmName << std::endl;
    #endif
  }
}

/**
 * @brief Stop the currently running LLM model
 * 
 * Unloads the current model and cleans up resources
 */
void Application::stopLLM() 
{
  if(m_isLLMRunning)
  {
    m_modelManager->unloadModel();
    m_currentModelInterface = nullptr;
    m_currentLLM = "";
    m_isLLMRunning = false;
  }
}

/**
 * @brief Send a prompt to the currently running LLM
 * 
 * @param prompt The text prompt to send to the LLM
 * @param keepAlive Whether to keep the LLM loaded after processing the prompt (default: true)
 * 
 * Sends the user's prompt to the LLM and initiates response streaming
 */
void Application::sendPrompt(const std::string& prompt, bool keepAlive) 
{
    #ifdef _DEBUG
      std::cout << "Application::sendPrompt entered with prompt : " << prompt << std::endl;
    #endif

    // Add the prompt to the chat history
    {
        std::lock_guard<std::mutex> gLock(m_responseMutex);
        m_conversationHistory += "User: ";
        m_conversationHistory += prompt;
        m_conversationHistory += "\n";
    }

    // In an async thread - send the prompt to the LLM and stream the response
    std::thread([this, prompt, keepAlive]() {
      streamLLMResponse(m_currentLLM, prompt, keepAlive);
    }).detach();
}

/**
 * @brief Stream the LLM's response to a prompt
 * 
 * @param llmName The name of the LLM model generating the response
 * @param prompt The text prompt sent to the LLM
 * @param keepAlive Whether to keep the LLM loaded after processing (default: true)
 * 
 * Handles the streaming of tokens from the LLM's response, updating the UI in real-time
 */
void Application::streamLLMResponse(const std::string& llmName, const std::string& prompt, bool keepAlive)
{
    m_isWaitingForResponse = true;
    #ifdef _DEBUG
      std::cout << "Application::streamLLMResponse entered with llmName : " << llmName << " and prompt : " << prompt << std::endl;
    #endif
    int pipeFd[2]; // 0 -> read end, 1 -> write end

    // Create the pipe
    if(pipe(pipeFd) == -1)
    {
        #ifdef _DEBUG
            std::cout << "Pipe creation failed..." << std::endl;
        #endif
        return;
    }

    // Make read end of the pipe non blocking
    if(fcntl(pipeFd[0], F_SETFL, O_NONBLOCK) == -1)
    {
        #ifdef _DEBUG
            std::cout << "Failed to set pipe read non-blocking..." << std::endl;
        #endif
        return;
    }
    {
        std::lock_guard<std::mutex> gLock(m_responseMutex);
        m_conversationHistory += llmName + ": ";
    }
    // Send the prompt and generate the response in a separate thread, passing it the pipe FD
    // for writing
    int writeFd = pipeFd[1];
    std::thread modelThread([this, prompt, writeFd]()
    {
        m_currentModelInterface->sendPrompt(writeFd, prompt, "User");
    });
    char buffer;
    while(true)
    {
        ssize_t bytesRead = read(pipeFd[0], &buffer, 1);
        if(bytesRead > 0)
        {
            // add the character to the response
            {
                std::lock_guard<std::mutex> gLock(m_responseMutex);
                m_conversationHistory += buffer;
            }
        }
        else if(bytesRead == -1 && errno == EAGAIN)
        {
            // Because we are non-blocking there is nothing in buffer - wait a little
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        else if(bytesRead == 0)
        {
            // Pipe closed by child - end of response
            break;
        }
        else
        {
            // Some Error Occured...
            #ifdef _DEBUG
                std::cout << "Error reading response buffer..." << std::endl;
            #endif
            break;
        }
    }

    // Clean up pipe and join the child
    close(pipeFd[0]);
    modelThread.join();
    m_isWaitingForResponse = false;
}

/**
 * @brief Initialize the GLFW window
 * 
 * Sets up the GLFW window with proper OpenGL context and configuration
 */
void Application::initWindow() {
    // Initialize GLFW
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }
    
    // Set OpenGL version
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    
    // Create window
    m_window = glfwCreateWindow(m_WIDTH, m_HEIGHT, m_APP_NAME.c_str(), nullptr, nullptr);
    if (!m_window) {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window");
    }
}

/**
 * @brief Initialize the OpenGL renderer
 * 
 * Creates and configures the OpenGL renderer with the application window
 */
void Application::initOpenGL() {
    m_renderer = std::make_unique<OpenGLRenderer>(m_window, m_WIDTH, m_HEIGHT);
}

/**
 * @brief Initialize the ImGui UI framework
 * 
 * Sets up ImGui for rendering the user interface
 */
void Application::initImGui() {
    m_renderer->initImGui();
}

/**
 * @brief Start the application main loop
 * 
 * Entry point for running the application after initialization
 */
void Application::run() {
    mainLoop();
}

/**
 * @brief Main application loop
 * 
 * Handles rendering and event processing in a continuous loop,
 * processes window events, renders UI, and manages shutdown sequences.
 */
void Application::mainLoop() {
    // Set a higher frame rate for smoother updates when streaming
    glfwSwapInterval(0); // Disable vsync for more frequent updates
    
    while (!glfwWindowShouldClose(m_window) || m_showShutdownWindow) {
        glfwPollEvents();
        
        // Check if the window close button was clicked and we need to start shutdown
        if (glfwWindowShouldClose(m_window) && !m_isShuttingDown && m_isLLMRunning) {
            // Start the shutdown process
            m_isShuttingDown = true;
            m_showShutdownWindow = true;
            
            // Stop the LLM in a separate thread to avoid blocking
            std::thread([this]() {
                this->stopLLM();
                this->m_showShutdownWindow = false;
            }).detach();
        }
        
        m_renderer->beginFrame();
        
        // Draw the main UI if not shutting down
        if (!m_isShuttingDown) {
            drawUI();
        }
        
        // Draw the shutdown window if needed
        if (m_showShutdownWindow) {
            drawShutdownWindow();
        }
        
        m_renderer->endFrame();
        
        // Sleep based on whether we need to update the UI
        if (m_uiNeedsUpdate || m_showShutdownWindow) {
            m_uiNeedsUpdate = false;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS
        }
    }
}

/**
 * @brief Draw the application user interface
 * 
 * Renders the complete UI including context manager, LLM selector,
 * and conversation window. Handles all user interactions with the interface.
 */
void Application::drawUI() {
    // Set up the main window to cover the entire application window
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(m_WIDTH, m_HEIGHT));
    
    // Make the main window act as a container only, with no visible elements
    ImGuiWindowFlags mainWindowFlags = 
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize | 
        ImGuiWindowFlags_NoMove | 
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBackground |  // Make background transparent
        ImGuiWindowFlags_NoBringToFrontOnFocus; // Don't bring to front when clicked
    
    ImGui::Begin("##MainContainer", nullptr, mainWindowFlags);
    
    // Calculate reasonable initial sizes and positions for sub-windows
    float padding = 10.0f;
    float topWindowHeight = m_HEIGHT * 0.35f;  // Height for top windows
    float contextWidth = m_WIDTH * 0.5f - padding * 1.5f;  // Width for Context window
    float llmsWidth = m_WIDTH * 0.5f - padding * 1.5f;     // Width for LLMs window
    
    // Store the initial prompt window position and size for first-time setup
    static bool promptWindowInitialized = false;
    static ImVec2 promptWindowPos(padding, padding + topWindowHeight + padding);
    static ImVec2 promptWindowSize(m_WIDTH - padding * 2, m_HEIGHT - topWindowHeight - padding * 3);
    
    // Context Manager UI - positioned at the top left
    ImGui::SetNextWindowPos(ImVec2(padding, padding));
    ImGui::SetNextWindowSize(ImVec2(contextWidth, topWindowHeight));
    
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_None;
    ImGui::Begin("Context", nullptr, windowFlags);
    ImGui::Text("Context");
    ImGui::SameLine();
    if (ImGui::Button("+")) {
        std::string filePath = m_contextManager->openFileDialog();
        if (!filePath.empty()) {
            m_contextManager->addFile(filePath);
        }
    }
    float windowWidth = ImGui::GetWindowWidth();
    float buttonWidth = ImGui::CalcTextSize("Clear All").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SameLine(windowWidth - buttonWidth - ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::Button("Clear All")) {
        m_contextManager->clearAll();
    }
    ImGui::Separator();
    m_contextManager->renderFileList();
    ImGui::End(); // End Context window

    // LLMs UI - positioned at the top right
    ImGui::SetNextWindowPos(ImVec2(padding + contextWidth + padding, padding));
    ImGui::SetNextWindowSize(ImVec2(llmsWidth, topWindowHeight));
    
    ImGui::Begin("Installed LLMs", nullptr, windowFlags);
    for (const auto& llm : m_llms) {
        ImGui::Text("Name: %s, Size: %s", llm.first.c_str(), llm.second.c_str());
        
        ImGui::SameLine(ImGui::GetWindowWidth() - buttonWidth - ImGui::GetStyle().ItemSpacing.x);
        
        // Show Run/Stop button
        std::string buttonLabel = (m_isLLMRunning && m_currentLLM == llm.first) ? "Stop##" + llm.first : "Run##" + llm.first;
        if (ImGui::Button(buttonLabel.c_str())) {
            if (m_isLLMRunning && m_currentLLM == llm.first) {
                stopLLM();
            } else {
                startLLM(llm.first);
            }
        }
    }
    ImGui::End(); // End LLMs window

    // Prompt window - show only when an LLM is running
    if (m_showPromptWindow) {
        // Use a fixed title without the loading indicator
        const char* windowTitle = "Prompt";
        
        // Set initial position and size only the first time
        if (!promptWindowInitialized) {
            ImGui::SetNextWindowPos(promptWindowPos);
            ImGui::SetNextWindowSize(promptWindowSize);
            promptWindowInitialized = true;
        }
        
        // Set size constraints to prevent window from becoming too small
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(400, 300),  // Minimum size
            ImVec2(FLT_MAX, FLT_MAX)  // Maximum size (unlimited)
        );
        
        // Make the prompt window resizable and movable, but don't auto-focus it
        ImGuiWindowFlags promptFlags = ImGuiWindowFlags_None | ImGuiWindowFlags_NoFocusOnAppearing;
        
        // Begin the window with fixed title
        ImGui::Begin(windowTitle, &m_showPromptWindow, promptFlags);
        
        // Save the current position and size only if the user is actively moving/resizing
        if (ImGui::IsWindowFocused() && (ImGui::IsMouseDragging(0) || ImGui::IsMouseDragging(1))) {
            promptWindowPos = ImGui::GetWindowPos();
            promptWindowSize = ImGui::GetWindowSize();
        }
        
        // Show loading indicator inside the window instead of in the title
        if (m_isWaitingForResponse) {
            static int frame = 0;
            frame = (frame + 1) % 4;
            const char* spinChars[] = {"|", "/", "-", "\\"};
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), 
                              "Loading %s", spinChars[frame]);
            ImGui::SameLine();
        }
        
        // Show file context status
        std::vector<std::string> contextFiles = m_contextManager->getFilePaths();
        if (!contextFiles.empty()) {
            if (m_isWaitingForResponse) {
                ImGui::SameLine();
            }
            ImGui::TextColored(ImVec4(0.0f, 0.8f, 0.0f, 1.0f), 
                              "Using %zu file(s) as context", contextFiles.size());
        }
        
        if (m_isWaitingForResponse || !contextFiles.empty()) {
            ImGui::Separator();
        }
        
        // Calculate the height for the conversation history
        float inputHeight = 30.0f; // Approximate height of input area
        float statusHeight = (m_isWaitingForResponse || !contextFiles.empty()) ? 40.0f : 0.0f;
        float historyHeight = ImGui::GetContentRegionAvail().y - inputHeight - statusHeight;
        
        // Display conversation history in a scrollable area
        ImGui::BeginChild("ConversationHistory", ImVec2(0, historyHeight), true);
        {
            std::lock_guard<std::mutex> lock(m_responseMutex);
            // Display the conversation history in Green color
            std::stringstream ss(m_conversationHistory);
            std::string line;
            while(std::getline(ss, line))
            {
                if(line.find("User:") != std::string::npos)
                {
                    // Color the user history in green
                    ImGui::TextColored(ImVec4(0.0f, 0.0f, 0.0f, 0.0f), "%s", line.c_str());
                }
                else
                {
                    // Color the LLM history in light blue color
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%s", line.c_str());
                }
            }
        }
        
        // Auto-scroll to bottom
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);
        
        ImGui::EndChild();
        
        // Input area for user prompt
        ImGui::PushItemWidth(-1);
        
        // Create a buffer for the input text
        static char inputBuffer[1024] = "";
        
        // Use InputTextWithHint with the buffer
        if (ImGui::InputTextWithHint("##prompt", "Enter your prompt here...", 
                                    inputBuffer, IM_ARRAYSIZE(inputBuffer), 
                                    ImGuiInputTextFlags_EnterReturnsTrue)) {
            m_userPrompt = inputBuffer;
            sendPrompt(m_userPrompt);
            m_userPrompt.clear();
            inputBuffer[0] = '\0'; // Clear the buffer
        }
        
        ImGui::PopItemWidth();
        
        ImGui::SameLine();
        if (ImGui::Button("Send") && inputBuffer[0] != '\0') {
            m_userPrompt = inputBuffer;
            sendPrompt(m_userPrompt);
            m_userPrompt.clear();
            inputBuffer[0] = '\0'; // Clear the buffer
        }
        
        ImGui::End(); // End Prompt window
    } else {
        // Reset initialization flag when window is closed
        promptWindowInitialized = false;
    }

    ImGui::End(); // End main container window
}

/**
 * @brief Draw the shutdown confirmation window
 * 
 * Displays a modal window with an animated loading indicator
 * while waiting for the LLM to completely shut down.
 */
void Application::drawShutdownWindow() {
    // Center the shutdown window on the screen
    ImVec2 center = ImVec2(m_WIDTH * 0.5f, m_HEIGHT * 0.5f);
    ImVec2 windowSize = ImVec2(300, 100);
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(windowSize);
    
    // Create a modal-style window
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | 
                            ImGuiWindowFlags_NoMove | 
                            ImGuiWindowFlags_NoCollapse | 
                            ImGuiWindowFlags_NoSavedSettings |
                            ImGuiWindowFlags_AlwaysAutoResize |
                            ImGuiWindowFlags_NoTitleBar;
    
    ImGui::Begin("##ShutdownWindow", nullptr, flags);
    
    // Create a growing dots animation
    static int frame = 0;
    frame = (frame + 1) % 24; // Slower animation
    
    // Calculate number of dots (1-4)
    int numDots = (frame / 6) + 1;
    std::string dots;
    for (int i = 0; i < numDots; i++) {
        dots += ".";
    }
    
    // Create the message with variable dots
    std::string message = "Waiting for " + m_currentLLM + " to shut down" + dots;
    
    // Center the text
    float textWidth = ImGui::CalcTextSize(message.c_str()).x;
    ImGui::SetCursorPosX((windowSize.x - textWidth) * 0.5f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 40); // Center vertically
    
    ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.0f, 1.0f), "%s", message.c_str());
    
    ImGui::End();
}

/**
 * @brief Handle the addition of a file to the context
 * 
 * @param filePath The path of the file being added to the context
 * 
 * Processes a newly added file and sends its content to the LLM as context.
 * Adds the file to the current LLM's context when available.
 */
void Application::handleFileAdded(const std::string& filePath) {
    if (!m_isLLMRunning || !m_currentModelInterface) {
        return; // No LLM running, can't send file
    }
    
    #ifdef _DEBUG
      std::cout << "File added to context: " << filePath << std::endl;
    #endif
    
    // Get the file content
    std::string fileContent = m_contextManager->getFileContents(filePath);
    
    // Create a prompt that indicates this is just for context
    std::string fileName = std::filesystem::path(filePath).filename().string();
    std::string contextPrompt = "File added for context: " + fileName + "\n\n";
    contextPrompt += fileContent;
    
    // Send file to model
    int pipeFd[2];
    if(pipe(pipeFd) == -1) {
        #ifdef _DEBUG
            std::cout << "Pipe creation failed when adding file to context" << std::endl;
        #endif
        return;
    }
    
    // Send the file context to the model in a separate thread
    std::thread([this, contextPrompt, pipeFd]() {
        m_currentModelInterface->sendPrompt(pipeFd[1], contextPrompt, "System");
        close(pipeFd[1]); // Close write end when done
    }).detach();
    
    // Close read end since we don't need the response
    close(pipeFd[0]);
}