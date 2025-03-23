/**
 * @file ContextManager.cpp
 * @brief Manages file context for the application, including file loading, listing, and content retrieval.
 */
#include "ContextManager.h"
#include <imgui.h>
#include <filesystem>
#include <algorithm>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#else
#include <gtk/gtk.h>
#endif

ContextManager::ContextManager() {
#ifndef _WIN32
    if (!gtk_init_check(nullptr, nullptr)) {
        throw std::runtime_error("Failed to initialize GTK");
    }
#endif
    // Initialize callback as empty
    onFileAddedCallback = nullptr;
}

ContextManager::~ContextManager() {
}

void ContextManager::render() {
    ImGui::BeginChild("Context", ImVec2(0, 0), true);
    
    // Put "Context" text and "+" button on the same line
    ImGui::Text("Context");
    ImGui::SameLine();
    if (ImGui::Button("+")) {
        std::string filePath = openFileDialog();
        if (!filePath.empty()) {
            addFile(filePath);
        }
    }
    
    // Move to the right side for the Clear All button (this will be handled in Application.cpp)
    
    ImGui::Separator();
    
    for (size_t i = 0; i < fileNames.size(); i++) {
        ImGui::Text("%s", fileNames[i].c_str());
        ImGui::SameLine();
        
        std::string buttonLabel = "Remove##" + std::to_string(i);
        if (ImGui::Button(buttonLabel.c_str())) {
            removeFile(i);
            // Exit the loop early since we modified the vector
            break;
        }
    }
    
    ImGui::EndChild();
}

void ContextManager::addFile(const std::string& filePath) {
    files.push_back(filePath);
    fileNames.push_back(getFileNameFromPath(filePath));
    
    // Notify callback if set
    if (onFileAddedCallback) {
        onFileAddedCallback(filePath);
    }
}

void ContextManager::removeFile(size_t index) {
    if (index < files.size()) {
        files.erase(files.begin() + index);
        fileNames.erase(fileNames.begin() + index);
    }
}

void ContextManager::clearAll() {
    files.clear();
    fileNames.clear();
}

void ContextManager::setOnFileAddedCallback(FileChangeCallback callback) {
    onFileAddedCallback = callback;
}

std::string ContextManager::getFileNameFromPath(const std::string& filePath) {
    return std::filesystem::path(filePath).filename().string();
}

std::string ContextManager::openFileDialog() {
    std::string filePath;
    
#ifdef _WIN32
    OPENFILENAMEA ofn;
    char szFile[260] = {0};
    
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = nullptr;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = nullptr;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    
    if (GetOpenFileNameA(&ofn)) {
        filePath = ofn.lpstrFile;
    }
#else
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Open File",
                                                   nullptr,
                                                   GTK_FILE_CHOOSER_ACTION_OPEN,
                                                   "_Cancel", GTK_RESPONSE_CANCEL,
                                                   "_Open", GTK_RESPONSE_ACCEPT,
                                                   nullptr);
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        filePath = filename;
        g_free(filename);
    }
    
    gtk_widget_destroy(dialog);
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }
#endif
    
    return filePath;
}

void ContextManager::renderFileList() {
    for (size_t i = 0; i < fileNames.size(); i++) {
        // Display file name
        ImGui::Text("%s", fileNames[i].c_str());
        
        // Calculate position for the Remove button to align with Clear All
        float windowWidth = ImGui::GetWindowWidth();
        float buttonWidth = ImGui::CalcTextSize("Remove").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SameLine(windowWidth - buttonWidth - ImGui::GetStyle().ItemSpacing.x);
        
        // Create and handle Remove button
        std::string buttonLabel = "Remove##" + std::to_string(i);
        if (ImGui::Button(buttonLabel.c_str())) {
            removeFile(i);
            // Exit the loop early since we modified the vector
            break;
        }
    }
}

std::vector<std::string> ContextManager::getFilePaths() const {
    return files;
}

std::string ContextManager::getFileContents(const std::string& filePath) const {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        return "Error: Could not open file " + filePath;
    }
    
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    return content;
}

std::string ContextManager::getAllFilesContents() const {
    std::string allContents;
    
    for (size_t i = 0; i < files.size(); i++) {
        allContents += "=== File: " + fileNames[i] + " ===\n";
        allContents += getFileContents(files[i]);
        allContents += "\n\n";
    }
    
    return allContents;
}
