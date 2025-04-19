/**
 * @file ModelConstants.h
 * @brief Contains constants that may be reused for accessing / envoking models
 */
#ifndef MODEL_CONSTANTS_H
#define MODEL_CONSTANTS_H

enum class ModelErrorType
{
   ModelPathError,
   ModelLoadError,
   SendPromptError,
   ModelResponseError,
   MODEL_LOAD_ERROR,
   MODEL_DIRECTORY_NOT_SET,
   MODEL_DIRECTORY_DOES_NOT_EXIST,
   MODEL_PATH_ERROR, 
   MODEL_NOT_FOUND
};

enum class PromptRoleType
{
   UserRole,
   SystemRole
};


#endif