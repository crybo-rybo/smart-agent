/**
 * @file ModelInterface.cpp
 * @brief Interface that allows other aspects of the program to load / unload the downloaded models.
 *        Furthermore, allows for interaction with the current loaded model.
 */

#include "ModelInterface.h"
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <unistd.h>

const uint32_t DEFAULT_CTX = 2048;

// Given the name of an LLM - this method will attempt to launch that LLM and load it into memory
ModelInterface::ModelInterface(std::string model_path) :
 m_modelPath(model_path),
 m_model(0),
 m_vocab(0),
 m_context(0),
 m_prevLength(0),
 m_isLoaded(false)
{
   // Initialize all of the llama-cpp content that is not dependent on the model
   ggml_backend_load_all();

   // Use dafault model params - fine tune later
   m_modelParams = llama_model_default_params();

   // Initialize context parameters - get a grip on these...
   m_contextParams = llama_context_default_params();
   m_contextParams.n_ctx = DEFAULT_CTX;
   m_contextParams.n_batch = DEFAULT_CTX;

   // Initialize the sampler - again, dig in more here....
   m_sampler = llama_sampler_chain_init(llama_sampler_chain_default_params());
   llama_sampler_chain_add(m_sampler, llama_sampler_init_min_p(0.05f, 1));
   llama_sampler_chain_add(m_sampler, llama_sampler_init_temp(0.8f));
   llama_sampler_chain_add(m_sampler, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
}

// Default destructor
ModelInterface::~ModelInterface()
{

}

// Returns the value of the m_isLoaded flag
const bool ModelInterface::isLoaded() const
{
   return m_isLoaded;
}

// This method will load the model and finish setting up any llama-cpp attributes specific to the model
bool ModelInterface::load()
{
   #ifdef _DEBUG
      std::cout << "Loading " << m_modelPath << " into memory..." << std::endl;
   #endif

   // Load the model from the path using the model parameters
   m_model = llama_model_load_from_file(m_modelPath.c_str(), m_modelParams);
   if(!m_model)
   {
      std::cerr << "Error : failed to load model @ " << m_modelPath << std::endl;
      return false; // Make use of std::expected.....
   }

   // Get the model vocab
   m_vocab = llama_model_get_vocab(m_model);

   // Initialize the context from the model using the context params
   m_context = llama_init_from_model(m_model, m_contextParams);
   if(!m_context)
   {
      std::cerr << "Error : failed to initialize the model context!" << std::endl;
      return false; // Make use of std::expected...
   }

   m_isLoaded = true;

   return true;
}

// This method will add a file to the models context
bool ModelInterface::addFileToContext(std::string file_path)
{
   return true;
}

// This method will send the request to the Ollama API to remove the model
void ModelInterface::unload()
{
   #ifdef _DEBUG
      std::cout << "Unloading " << m_llmName << std::endl;
   #endif
   if(m_isLoaded == true)
   {
      llama_sampler_free(m_sampler);
      llama_free(m_context);
      llama_model_free(m_model);
   }
   m_isLoaded = false;
}

// This method will send a system prompt to the model with the provided
// text
void ModelInterface::sendPrompt(const int writeFd, std::string prompt, std::string role /* User*/)
{
   // Add raw prompt to the llama messages vector with the user role
   m_messages.push_back({role.c_str(), strdup(prompt.c_str())});

   // Generate the formatted prompt for generation
   std::string genPrompt = formatPrompt();

   // Generate response
   generateResponse(writeFd, genPrompt);
}

// This method will take the llama messages vector, apply the prompt template, and isolate the
// prompt for response generation
std::string ModelInterface::formatPrompt()
{
   // create a default template
   const char* dTempl = llama_model_chat_template(m_model, nullptr);

   int newLen = llama_chat_apply_template(dTempl, m_messages.data(), m_messages.size(), true, m_formattedPrompt.data(), m_formattedPrompt.size());
   // Determine if we need to reformat the formatted prompt to accomodate the new prompt size
   if(newLen > (int)m_formattedPrompt.size())
   {
      m_formattedPrompt.resize(newLen);
      newLen = llama_chat_apply_template(dTempl, m_messages.data(), m_messages.size(), true, m_formattedPrompt.data(), m_formattedPrompt.size());
   }
   // Validate formatted prompt
   if(newLen < 0)
   {
      #ifdef _DEBUG
         std::cout << "Error Sizing Formatted Prompt..." << std::endl;
      #endif
      return "";
   }

   // Remove previous messages to isolate the new prompt
   std::string isolatedPrompt(m_formattedPrompt.begin() + m_prevLength, m_formattedPrompt.begin() + newLen);

   return isolatedPrompt;
}

//
// This method will take a formatted llama prompt and generate an output
// from the model
//
void ModelInterface::generateResponse(const int writeFd, const std::string& fPrompt)
{
   // Check if this is the first prompt
   const bool isFirst = llama_get_kv_cache_used_cells(m_context) == 0;

   // Tokenize the prompt
   const int nPromptTokens = -llama_tokenize(m_vocab, fPrompt.c_str(), fPrompt.size(), NULL, 0, isFirst, true);
   std::vector<llama_token> promptTokens(nPromptTokens);
   if(llama_tokenize(m_vocab, fPrompt.c_str(), fPrompt.size(), promptTokens.data(), promptTokens.size(), isFirst, true) < 0)
   {
      #ifdef _DEBUG
         std::cout << "Failed to tokenize prompt..." << std::endl;
      #endif
      return; // TODO - use C++ 23 exception handling
   }

   // Prepare a batch for the prompt
   llama_batch batch = llama_batch_get_one(promptTokens.data(), promptTokens.size());
   llama_token newTokenId;
   while(true)
   {
      // Check if we have enough space in the context to evaluate batch
      int nContext = llama_n_ctx(m_context);
      int nContextUsed = llama_get_kv_cache_used_cells(m_context);
      if(nContextUsed + batch.n_tokens > nContext)
      {
         #ifdef _DEBUG
            std::cout << "Context size exceeded..." << std::endl;
         #endif
         break;
      }

      // Decode the batch
      if(llama_decode(m_context, batch))
      {
         #ifdef _DEBUG
            std::cout << "Failed to decode batch..." << std::endl;
         #endif
         break; // TODO - what to do with response here...
      }

      // Sample the next token
      newTokenId = llama_sampler_sample(m_sampler, m_context, -1);

      // If we are at the end of the generation break from generation
      if(llama_vocab_is_eog(m_vocab, newTokenId))
      {
         break;
      }

      // Convert the token to a string and add it to the response
      char buf[256];
      int n = llama_token_to_piece(m_vocab, newTokenId, buf, sizeof(buf), 0, true);
      if(n < 0)
      {
         #ifdef _DEBUG
            std::cout << "Failed to convert token to piece..." << std::endl;
         #endif
         break; // TODO - what to do with response here...
      }
      for(int i = 0; i < n; ++i)
      {
         // Write each character to the pipe
         if(write(writeFd, &buf[i], 1) == -1)
         {
            #ifdef _DEBUG
               std::cout << "Model Interface write to pipe failed..." << std::endl;
            #endif
         }
      }
      // Prepare the next batch with the sampled token
      batch = llama_batch_get_one(&newTokenId, 1);
   }

   close(writeFd); // close the pipe
}