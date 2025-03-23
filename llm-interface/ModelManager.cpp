/**
 * @file ModelManager.cpp
 * @brief Interacts with the Ollama API to ensure Ollama is actually running, retrieves a list of the downloaded
 *        LLMs, handles loading / unloading requested LLMs.
 */

#include "ModelManager.h"
#include <iostream>
#include <ranges>


// Retrieves the names of the LLMs currently downloaded via Ollama
//
// TODO: change in future when moving away from Ollama. Also, really should only be
// ran once
std::expected<std::vector<std::pair<std::string, std::string>>,ModelErrorType> ModelManager::fetchModels()
{
   std::vector<std::pair<std::string, std::string>> outVec;

   // Execute the 'ls -l' command in the provided '.models' directory
   std::array<char, 128> buffer;
   std::string result;
   std::string lsCmd = "ls -l " + m_modelsDir;
   // Open pipe and run command
   std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(lsCmd.c_str(), "r"), pclose);
   if(!pipe)
   {
      return std::unexpected(ModelErrorType::ModelPathError);
   }
   // Read the command output into the buffer
   while(fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr)
   {
      result += buffer.data();
   }

   // Interate over the lines of the output
   std::istringstream output(result);
   std::string line;

   // Skip the first line...
   std::getline(output, line);
   
   // Loop over the rest of the output - get the LLM name and check the map
   while(std::getline(output, line))
   {
      std::istringstream ssLine(line);
      std::string a, b, c, d, e, f, g;
      std::string llmSize;
      std::string llmName;
      
      ssLine >> a >> b >> c >> d >> llmSize >> e >> f >> g >> llmName;
      uint32_t sizeGB = std::stol(llmSize) / 1000000000;
      llmSize = std::to_string(sizeGB);

      // Add LLM Name to the vector we are going to return - used to display on GUI
      outVec.push_back({llmName, llmSize});
      
      // If there is already an entry in the Model Map - do not insert one
      // TODO - not really sure when this would be called / if it could be called multiple
      // times (during some sort of reload? i.e. program running - user downloads new model? idk)
      // so the change we already have a model in the map is questionable
      if(m_modelMap.find(llmName) == m_modelMap.end())
      {
         // No existing entry - safe to add one here
         m_modelMap.insert({llmName, new ModelInterface(m_modelsDir + llmName)}); // is this okay / safe??
      }
   }

   return outVec;
}

// This method will load a model into memory - if not already loaded
std::expected<ModelInterface*,ModelErrorType> ModelManager::loadModel(std::string_view llmName)
{
   #ifdef _DEBUG
      std::cout << "ModelManager::loadLLM entered with llmName : " << llmName << std::endl;
   #endif

   // Validate valid LLM Name provided
   if(m_modelMap.find(llmName.data()) == m_modelMap.end()|| 
      m_modelMap.at(llmName.data()) == nullptr)
   {
      return std::unexpected(ModelErrorType::ModelLoadError);
   }

   // Check if there is already a model loaded
   if(m_loadedModel != nullptr && m_loadedModel->isLoaded() == true)
   {
      // Verify the loaded model is the model that is being requested
      if(m_loadedModel->getModelPath() != llmName)
      {
         #ifdef _DEBUG
            std::cout << "Model : " << m_loadedModel->modelName() << " loaded while attempting to load " <<
               llmName << " unloading..." << std::endl;
         #endif
         
         // Model loaded is different than requested, unload it now
         m_loadedModel->unload();
         m_loadedModel = nullptr;
      }
      else
      {
         #ifdef _DEBUG
            std::cout << "Model : " << llmName.data() << " already loaded!" << std::endl;
         #endif

         // Requested model is already loaded - exit here
         return m_loadedModel;
      }
   }

   // Attempt to load the model
   if(m_modelMap.at(llmName.data())->load())
   {
      #ifdef _DEBUG
         std::cout << "Model : " << llmName.data() << " loaded!" << std::endl;
      #endif

      m_loadedModel = m_modelMap.at(llmName.data());
      return m_loadedModel;
   }
   
   #ifdef _DEBUG
      std::cout << "Model : " << llmName.data() << " failed to load!" << std::endl;
   #endif

   // If we reach this point - the model failed to load
   m_loadedModel = nullptr;
   return std::unexpected(ModelErrorType::ModelLoadError);
}

// This method will unload the current loaded model - if any
void ModelManager::unloadModel()
{
   if(m_loadedModel != nullptr && m_loadedModel->isLoaded() == true)
   {
      m_loadedModel->unload();
      m_loadedModel = nullptr;
   }
}