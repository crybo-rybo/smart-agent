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
   // Retrieves the singleton instance - Meyers singleton approach
   static ModelManager* getInstance()
   {
      static std::unique_ptr<ModelManager> instance = std::unique_ptr<ModelManager>(new ModelManager);
      return instance.get();
   }

   // Sets the directory of the models
   inline void setModelDirectory(std::string path)
   {
      m_modelsDir = path;
   }

   // Returns the models found in the provided models directory
   std::expected<std::vector<std::pair<std::string, std::string>>,ModelErrorType> fetchModels();

   // This method will load a model into memory - if not already loaded
   std::expected<ModelInterface*,ModelErrorType> loadModel(std::string_view modelName);

   // This method will unload the running model - if any
   void unloadModel();

protected:
   
   // Default constructor
   ModelManager() : m_loadedModel(nullptr)
   {
      
   }

private:


   // Prevent cloning
   ModelManager(const ModelManager& rhs) = delete;
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