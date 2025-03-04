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

using json = nlohmann::json;

/**
 * @file Application.cpp
 * @brief Implements the Application class for managing the main application lifecycle.
 */

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

void Application::fetchLLMs() {
    // Execute the "ollama list" command and capture its output
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen("ollama list", "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("Failed to run ollama list command");
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }

    // Parse the output to extract LLM name and size
    std::istringstream stream(result);
    std::string line;
    // Skip header line
    std::getline(stream, line);
    
    while (std::getline(stream, line)) {
        std::istringstream lineStream(line);
        std::string name, tag, size;
        
        // Format is: NAME TAG SIZE MODIFIED
        if (lineStream >> name >> tag >> size) {
            llms.emplace_back(name, size);
        }
    }
}

void Application::startLLM(const std::string& llmName) {
    if (isLLMRunning) {
        stopLLM();
    }
    
    // Check if Ollama server is running
    CURL* curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:11434/api/tags");
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        
        if (res != CURLE_OK) {
            std::cerr << "Error: Ollama server not running at localhost:11434" << std::endl;
            return;
        }
    }
    
    currentLLM = llmName;
    isLLMRunning = true;
    showPromptWindow = true;
    
    // Reset conversation
    conversationHistory = "";
    
    // Send an empty prompt to load the LLM into memory
    std::cout << "Loading " << llmName << " into memory..." << std::endl;
    
    // Use a separate thread to avoid blocking the UI
    std::thread([this, llmName]() {
        try {
            // Create a temporary file for the JSON request
            std::string tempRequestFile = "/tmp/ollama_request.json";
            
            // Create a minimal JSON payload just to load the model
            json payload = {
                {"model", llmName},
                {"prompt", " "}, // Use a space instead of empty string to ensure it loads
                {"stream", false}
            };
            
            // Write the payload to the temp file
            std::ofstream requestFile(tempRequestFile);
            if (!requestFile.is_open()) {
                std::cerr << "Failed to open temp request file" << std::endl;
                return;
            }
            requestFile << payload.dump();
            requestFile.close();
            
            // Build the curl command
            std::string curlCmd = "curl -s -X POST -H \"Content-Type: application/json\" -d @" + 
                                 tempRequestFile + " http://localhost:11434/api/generate";
            
            std::cout << "Executing load command: " << curlCmd << std::endl;
            
            // Execute the command
            int result = system(curlCmd.c_str());
            if (result != 0) {
                std::cerr << "Failed to load model, error code: " << result << std::endl;
            } else {
                std::cout << llmName << " loaded successfully" << std::endl;
            }
            
            // Clean up temp file
            std::remove(tempRequestFile.c_str());
            
        } catch (const std::exception& e) {
            std::cerr << "Exception in model loading: " << e.what() << std::endl;
        }
    }).detach();
}

void Application::stopLLM() {
    if (isLLMRunning) {
        stopRequested = true;
        if (llmFuture.valid()) {
            llmFuture.wait();
        }

        // send an empty prompt to unload the LLM from memory with the 'keep_alive' attribute set
        // to 0 to force it to unload
        sendPrompt("", false);

        isLLMRunning = false;
        currentLLM = "";
        stopRequested = false;
        isWaitingForResponse = false; // Reset waiting flag
        
        // Close the Prompt window when stopping the LLM
        showPromptWindow = false;
    }
}

void Application::sendPrompt(const std::string& prompt, bool keepAlive) {
    try {
        if (!isLLMRunning) return;
        
        // Set waiting flag to true (only for non-empty prompts)
        if (!prompt.empty()) {
            isWaitingForResponse = true;
            
            // Add user prompt to conversation history
            std::string userMessage = "User: " + prompt + "\n";
            {
                std::lock_guard<std::mutex> lock(responseMutex);
                conversationHistory += userMessage;
            }
        }
        
        // Get file context (only for non-empty prompts)
        std::string fileContext;
        std::string combinedPrompt = prompt;
        
        if (!prompt.empty()) {
            fileContext = contextManager->getAllFilesContents();
            
            // Create a combined prompt with file context
            if (!fileContext.empty()) {
                combinedPrompt = "I have the following files in my context:\n\n" + 
                                fileContext + 
                                "\n\nBased on these files, please answer the following question:\n" + 
                                prompt;
            }
        }
        
        if (!prompt.empty()) {
            std::cout << "Sending prompt to " << currentLLM << " with file context" << std::endl;
        } else {
            std::cout << "Sending control message to " << currentLLM << " (keep_alive=" << keepAlive << ")" << std::endl;
        }
        
        // Start streaming response in a separate thread
        stopRequested = false;
        
        // Use a safer approach to launch the async task
        try {
            llmFuture = std::async(std::launch::async, 
                [this, llm = currentLLM, p = combinedPrompt, ka = keepAlive]() {
                    try {
                        this->streamLLMResponse(llm, p, ka);
                    } catch (const std::exception& e) {
                        std::cerr << "Exception in async task: " << e.what() << std::endl;
                    }
                    // Set waiting flag to false when done (only for non-empty prompts)
                    if (!p.empty()) {
                        this->isWaitingForResponse = false;
                    }
                });
        } catch (const std::exception& e) {
            std::cerr << "Failed to launch async task: " << e.what() << std::endl;
            if (!prompt.empty()) {
                isWaitingForResponse = false; // Reset waiting flag
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Exception in sendPrompt: " << e.what() << std::endl;
        if (!prompt.empty()) {
            isWaitingForResponse = false; // Reset waiting flag
        }
    }
}

void Application::streamLLMResponse(const std::string& llmName, const std::string& prompt, bool keepAlive) {
    try {
        // Create a temporary file for the JSON request
        std::string tempRequestFile = "/tmp/ollama_request.json";
        
        // Create the JSON payload with keep_alive attribute
        json payload = {
            {"model", llmName},
            {"prompt", prompt},
            {"stream", true},  // Enable streaming mode
        };

        // Only add keep_alive if it is NOT 0
        if (!keepAlive) {
            payload["keep_alive"] = 0;
        }
        
        // Write the payload to the temp file
        std::ofstream requestFile(tempRequestFile);
        if (!requestFile.is_open()) {
            std::cerr << "Failed to open temp request file" << std::endl;
            return;
        }
        requestFile << payload.dump();
        requestFile.close();
        
        // Build the curl command with streaming output and disable buffering
        std::string curlCmd = "curl -N -s -X POST -H \"Content-Type: application/json\" -d @" + 
                             tempRequestFile + " http://localhost:11434/api/generate";
        
        std::cout << "Executing streaming command: " << curlCmd << std::endl;
        
        // For empty prompts (control messages), we don't need to process the response
        if (prompt.empty()) {
            // Just execute the command and return
            int result = system(curlCmd.c_str());
            if (result != 0) {
                std::cerr << "Failed to execute curl command, error code: " << result << std::endl;
            }
            std::remove(tempRequestFile.c_str());
            return;
        }
        
        // Open a pipe to read the output in real-time with line buffering
        FILE* pipe = popen(curlCmd.c_str(), "r");
        if (!pipe) {
            std::cerr << "Failed to open pipe for curl command" << std::endl;
            std::remove(tempRequestFile.c_str());
            return;
        }
        
        // Disable buffering on the pipe
        setvbuf(pipe, NULL, _IONBF, 0);
        
        // Add LLM prefix to conversation history (in a thread-safe way)
        {
            std::lock_guard<std::mutex> lock(responseMutex);
            conversationHistory += llmName + ": ";
            
            // Signal that UI needs update
            uiNeedsUpdate = true;
        }
        
        // Read the output character by character
        char c;
        std::string currentLine;
        std::string accumulatedChunks;
        int chunkCounter = 0;
        
        while (!stopRequested && (c = fgetc(pipe)) != EOF) {
            if (c == '\n') {
                // Process complete line
                if (!currentLine.empty()) {
                    try {
                        auto response = json::parse(currentLine);
                        
                        if (response.contains("response") && response["response"].is_string()) {
                            std::string chunk = response["response"].get<std::string>();
                            accumulatedChunks += chunk;
                            chunkCounter++;
                            
                            // Update UI less frequently to reduce blocking
                            if (chunkCounter >= 5) {
                                // Update conversation history with accumulated chunks
                                std::lock_guard<std::mutex> lock(responseMutex);
                                conversationHistory += accumulatedChunks;
                                accumulatedChunks.clear();
                                chunkCounter = 0;
                                
                                // Signal that UI needs update
                                uiNeedsUpdate = true;
                            }
                        }
                        
                        // Check if this is the done message
                        if (response.contains("done") && response["done"].get<bool>()) {
                            // Make sure to add any remaining chunks
                            if (!accumulatedChunks.empty()) {
                                std::lock_guard<std::mutex> lock(responseMutex);
                                conversationHistory += accumulatedChunks;
                                uiNeedsUpdate = true;
                            }
                            break;
                        }
                    } catch (const std::exception& e) {
                        // Ignore parsing errors for incomplete lines
                    }
                }
                currentLine.clear();
            } else {
                currentLine += c;
            }
            
            // Sleep less to process chunks faster
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        
        // Add newline after complete response
        {
            std::lock_guard<std::mutex> lock(responseMutex);
            conversationHistory += "\n\n";
            uiNeedsUpdate = true;
        }
        
        // Close the pipe and clean up
        pclose(pipe);
        std::remove(tempRequestFile.c_str());
        
    } catch (const std::exception& e) {
        std::cerr << "Exception in streamLLMResponse: " << e.what() << std::endl;
    }
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