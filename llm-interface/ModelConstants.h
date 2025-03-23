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
   ModelResponseError
};

enum class PromptRoleType
{
   UserRole,
   SystemRole
};


#endif