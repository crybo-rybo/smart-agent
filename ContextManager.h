/**
 * @file ContextManager.h
 * @brief Manages file context for the application, including file loading, listing, and content retrieval.
 */
#ifndef CONTEXT_MANAGER_H
#define CONTEXT_MANAGER_H

#include <vector>
#include <string>
#include <functional>

class ContextManager {
public:
    // Define a callback type for file changes
    using FileChangeCallback = std::function<void(const std::string&)>;
    
    ContextManager();
    ~ContextManager();
    
    void render();
    void renderFileList();
    void addFile(const std::string& filePath);
    void removeFile(size_t index);
    void clearAll();
    std::string openFileDialog();
    
    // New methods to access file information
    std::vector<std::string> getFilePaths() const;
    std::string getFileContents(const std::string& filePath) const;
    std::string getAllFilesContents() const;
    
    // Set callback for file changes
    void setOnFileAddedCallback(FileChangeCallback callback);
    
private:
    std::string getFileNameFromPath(const std::string& filePath);
    
    std::vector<std::string> files;
    std::vector<std::string> fileNames;
    FileChangeCallback onFileAddedCallback;
};

#endif // CONTEXT_MANAGER_H
