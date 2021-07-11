#ifdef __linux__
void hello();
#else
#ifdef ARGSPARSER_EXPORTS
#define ARGSPARSER_API __declspec(dllexport)
#else
#define ARGSPARSER_API __declspec(dllimport)
#endif
ARGSPARSER_API void hello();
#endif
