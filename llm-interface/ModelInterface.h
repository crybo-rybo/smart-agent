/**
 * @file ModelInterface.h
 * @brief Interface that allows other aspects of the program to load / unload the downloaded models.
 *        Furthermore, allows for interaction with the current loaded model.
 */
#ifndef MODEL_INTERFACE_H
#define MODEL_INTERFACE_H

#include "ModelConstants.h"
#include "llama.h"
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <expected>

class ModelInterface
{
public:
   // Given the name of an LLM - this method will attempt to launch that LLM and load it into memory
   ModelInterface(std::string model_path);

   // Default destructor
   ~ModelInterface();

   // Returns the value of the m_isLoaded flag
   const bool isLoaded() const;

   // Returns the name of the model this interface is for
   inline const std::string getModelPath() const
   {
      return m_modelPath;
   }

   // This method will send the initial command to the Ollama to load the model into memory
   bool load();

   // This method will send the request to the Ollama API to remove the model
   void unload();

   // This method will add a file to the models context
   bool addFileToContext(std::string file_path);

   // This method will send a prompt to the LLM and stream the response through the provided pipe
   // Options for role are "System" and "User"
   void sendPrompt(const int writeFd, std::string prompt, std::string role = "User");

   // This method will take the llama messages vector, apply the prompt template, and isolate the
   // prompt for response generation
   std::string formatPrompt();

   // This method will take a formatted llama prompt and generate an output
   // from the model
   void generateResponse(const int writeFd, const std::string& fPrompt);

private:

   //
   // llama-cpp specific attributes
   //
   llama_model_params m_modelParams;
   llama_model* m_model;
   const llama_vocab* m_vocab;
   llama_context_params m_contextParams;
   llama_context* m_context;
   llama_sampler* m_sampler;
   std::vector<llama_chat_message> m_messages;
   std::vector<char> m_formattedPrompt;
   int m_prevLength;

   // This is the name of the model this interface is for
   std::string m_modelPath;
   // This attribute contains the last 'context' KV string returned from the model
   // after a prompt
   std::string m_lastContext;
   // Attribute indicating if the model is currently loaded
   bool m_isLoaded;
};


#endif