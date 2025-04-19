/**
 * @file ModelManager.h
 * @brief Interacts with the Ollama API to ensure Ollama is actually running, retrieves a list of the downloaded
 *        LLMs, handles loading / unloading requested LLMs.
 */
#ifndef MODEL_MANAGER_H
#define MODEL_MANAGER_H

#include <string>
#include <map>
#include <thread>
#include <mutex>
#include <memory>
#include <expected>
#include "ModelConstants.h"
#include "ModelInterface.h"

class ModelManager
{
public:
   /**
    * @brief Retrieves the singleton instance of the ModelManager
    * 
    * @return ModelManager* Pointer to the singleton instance
    * 
    * Uses the Meyers singleton approach to ensure thread-safe lazy initialization
    */
   static ModelManager* getInstance()
   {
      static std::unique_ptr<ModelManager> instance = std::unique_ptr<ModelManager>(new ModelManager);
      return instance.get();
   }

   /**
    * @brief Sets the directory path where LLM models are stored
    * 
    * @param path The filesystem path to the directory containing the models
    */
   inline void setModelDirectory(std::string path)
   {
      m_modelsDir = path;
   }

   /**
    * @brief Retrieves a list of all available LLM models in the models directory
    * 
    * @return std::expected<std::vector<std::pair<std::string, std::string>>,ModelErrorType>
    *         Vector of pairs containing model names and sizes on success,
    *         or ModelErrorType on failure
    */
   std::expected<std::vector<std::pair<std::string, std::string>>,ModelErrorType> fetchModels();

   /**
    * @brief Loads a specific model into memory if not already loaded
    * 
    * @param modelName The name of the model to load
    * @return std::expected<ModelInterface*,ModelErrorType>
    *         Pointer to the loaded ModelInterface on success,
    *         or ModelErrorType on failure
    */
   std::expected<ModelInterface*,ModelErrorType> loadModel(std::string_view modelName);

   /**
    * @brief Unloads the currently loaded model if one is active
    * 
    * Checks if a model is currently loaded and, if so, unloads it
    * and resets the m_loadedModel pointer to nullptr
    */
   void unloadModel();

protected:
   
   /**
    * @brief Default constructor for ModelManager
    * 
    * Initializes the loaded model pointer to nullptr
    */
   ModelManager() : m_loadedModel(nullptr)
   {
      m_modelMap.clear();
      m_modelsDir = "";
   }

private:
   /**
    * @brief Copy constructor (deleted)
    * 
    * @param rhs The ModelManager to copy from
    * 
    * Deleted to prevent copying of the singleton
    */
   ModelManager(const ModelManager& rhs) = delete;
   
   /**
    * @brief Assignment operator (deleted)
    * 
    * @param rhs The ModelManager to assign from
    * @return ModelManager& Reference to this ModelManager
    * 
    * Deleted to prevent assignment of the singleton
    */
   ModelManager& operator=(const ModelManager& rhs) = delete;

   // Map that maps the name of the LLM to the instance of ModelInterface that
   // controls the interaction with the LLM
   //
   // Note - currently limits single LLM instance per LLM type on the system
   std::map<std::string, ModelInterface*> m_modelMap;

   // Pointer to the model that is currently loaded in memory
   ModelInterface* m_loadedModel;

   // This holds the path to the directory to search for models
   std::string m_modelsDir;
};


#endif