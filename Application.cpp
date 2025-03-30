/**
 * @file Application.cpp
 * @brief Manages the main application lifecycle, including window management, LLM interaction, and UI rendering.
 */
#include "Application.h"
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <array>
#include <cstdio>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <unistd.h>
#include <fcntl.h>

using json = nlohmann::json;

const std::string MODELS_DIR = "/Users/conorrybacki/.models/";

// Callback function for CURL to write response data
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* s) {
    size_t newLength = size * nmemb;
    try {
        s->append((char*)contents, newLength);
        return newLength;
    } catch(std::bad_alloc& e) {
        return 0;
    }
}

Application::Application() : window(nullptr) {
    initWindow();
    initOpenGL();
    initImGui();
    contextManager = std::make_unique<ContextManager>();
    m_modelManager = ModelManager::getInstance();
    m_modelManager->setModelDirectory(MODELS_DIR);
    fetchLLMs();
    
    // Initialize CURL
    curl_global_init(CURL_GLOBAL_ALL);
}

Application::~Application() {
    // Make sure to stop any running LLM
    if (isLLMRunning) {
        // If we're not already in the shutdown process, show the shutdown window
        if (!isShuttingDown) {
            isShuttingDown = true;
            showShutdownWindow = true;
            
            // Create a temporary window for shutdown
            GLFWwindow* tempWindow = window;
            
            // Draw the shutdown window at least once
            renderer->beginFrame();
            drawShutdownWindow();
            renderer->endFrame();
            
            // Stop the LLM
            stopLLM();
        }
    }
    
    // Clean up CURL
    curl_global_cleanup();
}

void Application::fetchLLMs() 
{
  auto fetchResp = m_modelManager->fetchModels();
  if(fetchResp.has_value())
  {
      for(const auto& mod : fetchResp.value())
      {
          llms.emplace_back(mod);
      }
  }
  else
  {
    std::cout << "Error fetching model names!" << std::endl;
  }
}

void Application::startLLM(const std::string& llmName) 
{
  // Start the model specified by the name selected - note this call will
  // handle appending model directory path and ".gguf" to model name
  auto loadResp = m_modelManager->loadModel(llmName);
  if(loadResp.has_value())
  {
    m_currentModelInterface = loadResp.value();
    currentLLM = llmName;
    isLLMRunning = true;
    showPromptWindow = true;
  }
  else
  {
    #ifdef _DEBUG
      std::cout << "Error loading model : " << llmName << std::endl;
    #endif
  }
}

void Application::stopLLM() 
{
  isLLMRunning = false;
  m_modelManager->unloadModel();
  m_currentModelInterface = nullptr;
  currentLLM = "";
  showPromptWindow = false;
}

void Application::sendPrompt(const std::string& prompt, bool keepAlive) 
{
    #ifdef _DEBUG
      std::cout << "Application::sendPrompt entered with prompt : " << prompt << std::endl;
    #endif

    // Add the prompt to the chat history
    {
        std::lock_guard<std::mutex> gLock(responseMutex);
        conversationHistory += "User: ";
        conversationHistory += prompt;
        conversationHistory += "\n";
    }

    // In an async thread - send the prompt to the LLM and stream the response
    std::thread([this, prompt, keepAlive]() {
      streamLLMResponse(currentLLM, prompt, keepAlive);
    }).detach();
}

void Application::streamLLMResponse(const std::string& llmName, const std::string& prompt, bool keepAlive)
{
    isWaitingForResponse = true;
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
        std::lock_guard<std::mutex> gLock(responseMutex);
        conversationHistory += llmName + ": ";
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
                std::lock_guard<std::mutex> gLock(responseMutex);
                conversationHistory += buffer;
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
    isWaitingForResponse = false;
}

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
    window = glfwCreateWindow(WIDTH, HEIGHT, APP_NAME.c_str(), nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window");
    }
}

void Application::initOpenGL() {
    renderer = std::make_unique<OpenGLRenderer>(window, WIDTH, HEIGHT);
}

void Application::initImGui() {
    renderer->initImGui();
}

void Application::run() {
    mainLoop();
}

void Application::mainLoop() {
    // Set a higher frame rate for smoother updates when streaming
    glfwSwapInterval(0); // Disable vsync for more frequent updates
    
    while (!glfwWindowShouldClose(window) || showShutdownWindow) {
        glfwPollEvents();
        
        // Check if the window close button was clicked and we need to start shutdown
        if (glfwWindowShouldClose(window) && !isShuttingDown && isLLMRunning) {
            // Start the shutdown process
            isShuttingDown = true;
            showShutdownWindow = true;
            
            // Stop the LLM in a separate thread to avoid blocking
            std::thread([this]() {
                this->stopLLM();
                this->showShutdownWindow = false;
            }).detach();
        }
        
        renderer->beginFrame();
        
        // Draw the main UI if not shutting down
        if (!isShuttingDown) {
            drawUI();
        }
        
        // Draw the shutdown window if needed
        if (showShutdownWindow) {
            drawShutdownWindow();
        }
        
        renderer->endFrame();
        
        // Sleep based on whether we need to update the UI
        if (uiNeedsUpdate || showShutdownWindow) {
            uiNeedsUpdate = false;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS
        }
    }
}

void Application::drawUI() {
    // Set up the main window to cover the entire application window
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(WIDTH, HEIGHT));
    
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
    float topWindowHeight = HEIGHT * 0.35f;  // Height for top windows
    float contextWidth = WIDTH * 0.5f - padding * 1.5f;  // Width for Context window
    float llmsWidth = WIDTH * 0.5f - padding * 1.5f;     // Width for LLMs window
    
    // Store the initial prompt window position and size for first-time setup
    static bool promptWindowInitialized = false;
    static ImVec2 promptWindowPos(padding, padding + topWindowHeight + padding);
    static ImVec2 promptWindowSize(WIDTH - padding * 2, HEIGHT - topWindowHeight - padding * 3);
    
    // Context Manager UI - positioned at the top left
    ImGui::SetNextWindowPos(ImVec2(padding, padding));
    ImGui::SetNextWindowSize(ImVec2(contextWidth, topWindowHeight));
    
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_None;
    ImGui::Begin("Context", nullptr, windowFlags);
    ImGui::Text("Context");
    ImGui::SameLine();
    if (ImGui::Button("+")) {
        std::string filePath = contextManager->openFileDialog();
        if (!filePath.empty()) {
            contextManager->addFile(filePath);
        }
    }
    float windowWidth = ImGui::GetWindowWidth();
    float buttonWidth = ImGui::CalcTextSize("Clear All").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SameLine(windowWidth - buttonWidth - ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::Button("Clear All")) {
        contextManager->clearAll();
    }
    ImGui::Separator();
    contextManager->renderFileList();
    ImGui::End(); // End Context window

    // LLMs UI - positioned at the top right
    ImGui::SetNextWindowPos(ImVec2(padding + contextWidth + padding, padding));
    ImGui::SetNextWindowSize(ImVec2(llmsWidth, topWindowHeight));
    
    ImGui::Begin("Installed LLMs", nullptr, windowFlags);
    for (const auto& llm : llms) {
        ImGui::Text("Name: %s, Size: %s", llm.first.c_str(), llm.second.c_str());
        
        ImGui::SameLine(ImGui::GetWindowWidth() - buttonWidth - ImGui::GetStyle().ItemSpacing.x);
        
        // Show Run/Stop button
        std::string buttonLabel = (isLLMRunning && currentLLM == llm.first) ? "Stop##" + llm.first : "Run##" + llm.first;
        if (ImGui::Button(buttonLabel.c_str())) {
            if (isLLMRunning && currentLLM == llm.first) {
                stopLLM();
            } else {
                startLLM(llm.first);
            }
        }
    }
    ImGui::End(); // End LLMs window

    // Prompt window - show only when an LLM is running
    if (showPromptWindow) {
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
        ImGui::Begin(windowTitle, &showPromptWindow, promptFlags);
        
        // Save the current position and size only if the user is actively moving/resizing
        if (ImGui::IsWindowFocused() && (ImGui::IsMouseDragging(0) || ImGui::IsMouseDragging(1))) {
            promptWindowPos = ImGui::GetWindowPos();
            promptWindowSize = ImGui::GetWindowSize();
        }
        
        // Show loading indicator inside the window instead of in the title
        if (isWaitingForResponse) {
            static int frame = 0;
            frame = (frame + 1) % 4;
            const char* spinChars[] = {"|", "/", "-", "\\"};
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), 
                              "Loading %s", spinChars[frame]);
            ImGui::SameLine();
        }
        
        // Show file context status
        std::vector<std::string> contextFiles = contextManager->getFilePaths();
        if (!contextFiles.empty()) {
            if (isWaitingForResponse) {
                ImGui::SameLine();
            }
            ImGui::TextColored(ImVec4(0.0f, 0.8f, 0.0f, 1.0f), 
                              "Using %zu file(s) as context", contextFiles.size());
        }
        
        if (isWaitingForResponse || !contextFiles.empty()) {
            ImGui::Separator();
        }
        
        // Calculate the height for the conversation history
        float inputHeight = 30.0f; // Approximate height of input area
        float statusHeight = (isWaitingForResponse || !contextFiles.empty()) ? 40.0f : 0.0f;
        float historyHeight = ImGui::GetContentRegionAvail().y - inputHeight - statusHeight;
        
        // Display conversation history in a scrollable area
        ImGui::BeginChild("ConversationHistory", ImVec2(0, historyHeight), true);
        {
            std::lock_guard<std::mutex> lock(responseMutex);
            ImGui::TextWrapped("%s", conversationHistory.c_str());
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
            userPrompt = inputBuffer;
            sendPrompt(userPrompt);
            userPrompt.clear();
            inputBuffer[0] = '\0'; // Clear the buffer
        }
        
        ImGui::PopItemWidth();
        
        ImGui::SameLine();
        if (ImGui::Button("Send") && inputBuffer[0] != '\0') {
            userPrompt = inputBuffer;
            sendPrompt(userPrompt);
            userPrompt.clear();
            inputBuffer[0] = '\0'; // Clear the buffer
        }
        
        ImGui::End(); // End Prompt window
    } else {
        // Reset initialization flag when window is closed
        promptWindowInitialized = false;
    }

    ImGui::End(); // End main container window
}

void Application::drawShutdownWindow() {
    // Center the shutdown window on the screen
    ImVec2 center = ImVec2(WIDTH * 0.5f, HEIGHT * 0.5f);
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
    std::string message = "Waiting for " + currentLLM + " to shut down" + dots;
    
    // Center the text
    float textWidth = ImGui::CalcTextSize(message.c_str()).x;
    ImGui::SetCursorPosX((windowSize.x - textWidth) * 0.5f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 40); // Center vertically
    
    ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.0f, 1.0f), "%s", message.c_str());
    
    ImGui::End();
}