/**
 * @file ModelManager.cpp
 * @brief Interacts with the file system to find and manage LLM models, handling loading/unloading
 */

#include "ModelManager.h"
#include <iostream>
#include <filesystem>
#include <algorithm>

/**
 * @brief Retrieves a list of all available LLM models in the models directory
 * 
 * @return std::expected<std::vector<std::pair<std::string, std::string>>,ModelErrorType>
 *         Vector of pairs containing model names and sizes on success,
 *         or ModelErrorType on failure
 */
std::expected<std::vector<std::pair<std::string, std::string>>, ModelErrorType> ModelManager::fetchModels()
{
   // Check if the model directory is set
   if (m_modelsDir.empty())
   {
      return std::unexpected(ModelErrorType::MODEL_DIRECTORY_NOT_SET);
   }

   // Check if the directory exists
   if (!std::filesystem::exists(m_modelsDir))
   {
      return std::unexpected(ModelErrorType::MODEL_DIRECTORY_DOES_NOT_EXIST);
   }

   std::vector<std::pair<std::string, std::string>> modelList;

   try
   {
      // Iterate through the directory to find models (files that end with .gguf)
      for (const auto& entry : std::filesystem::directory_iterator(m_modelsDir))
      {
         if (entry.is_regular_file() && entry.path().extension() == ".gguf")
         {
            std::string modelName = entry.path().filename().string();
            
            // Just show the raw file size in bytes - simpler code, worse UX
            auto fileSize = entry.file_size();
            // Convert bytes to GB with 2 decimal places
            double sizeGB = static_cast<double>(fileSize) / (1024 * 1024 * 1024);
            char sizeBuffer[32];
            std::snprintf(sizeBuffer, sizeof(sizeBuffer), "%.2f", sizeGB);
            std::string sizeStr = sizeBuffer;
            
            modelList.push_back({modelName, sizeStr});
         }
      }
   }
   catch (const std::exception& e)
   {
      // Handle any filesystem errors
      return std::unexpected(ModelErrorType::MODEL_PATH_ERROR);
   }

   // Sort the model list alphabetically by name
   std::sort(modelList.begin(), modelList.end(), 
             [](const auto& a, const auto& b) { return a.second < b.second; });

   return modelList;
}

// This method will load a model into memory - if not already loaded
/**
 * @brief Loads a specific model into memory if not already loaded
 * 
 * @param modelName The name of the model to load
 * @return std::expected<ModelInterface*,ModelErrorType>
 *         Pointer to the loaded ModelInterface on success,
 *         or ModelErrorType on failure
 */
std::expected<ModelInterface*, ModelErrorType> ModelManager::loadModel(std::string_view modelName)
{
   // If a model is already loaded, unload it first
   if (m_loadedModel != nullptr)
   {
      unloadModel();
   }

   // Check if the model directory is set
   if (m_modelsDir.empty())
   {
      return std::unexpected(ModelErrorType::MODEL_DIRECTORY_NOT_SET);
   }

   // Build the full path to the model
   std::string modelPath = m_modelsDir + "/" + std::string(modelName);

   // Check if the model file exists
   if (!std::filesystem::exists(modelPath))
   {
      return std::unexpected(ModelErrorType::MODEL_NOT_FOUND);
   }

   // Check if we already have an interface for this model
   auto iter = m_modelMap.find(std::string(modelName));
   if (iter != m_modelMap.end())
   {
      // We already have an interface, just load the model
      ModelInterface* modelInterface = iter->second;
      
      if (!modelInterface->isLoaded())
      {
         if (!modelInterface->load())
         {
            return std::unexpected(ModelErrorType::MODEL_LOAD_ERROR);
         }
      }
      
      m_loadedModel = modelInterface;
      return m_loadedModel;
   }
   else
   {
      // Create a new model interface
      try
      {
         ModelInterface* modelInterface = new ModelInterface(modelPath);
         
         // Try to load the model
         if (!modelInterface->load())
         {
            delete modelInterface;
            return std::unexpected(ModelErrorType::MODEL_LOAD_ERROR);
         }
         
         // Add to the map and set as loaded model
         m_modelMap[std::string(modelName)] = modelInterface;
         m_loadedModel = modelInterface;
         
         return m_loadedModel;
      }
      catch (const std::exception& e)
      {
         return std::unexpected(ModelErrorType::MODEL_LOAD_ERROR);
      }
   }
}

// This method will unload the current loaded model - if any
/**
 * @brief Unloads the currently loaded model if one is active
 * 
 * Checks if a model is currently loaded and, if so, unloads it
 * and resets the m_loadedModel pointer to nullptr
 */
void ModelManager::unloadModel()
{
   if (m_loadedModel != nullptr)
   {
      // Call unload on the model interface
      m_loadedModel->unload();
      m_loadedModel = nullptr;
   }
}