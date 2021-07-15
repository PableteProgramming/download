#include <string>
#include <curl/curl.h>

std::pair<int, std::string> DoRequest(std::string, std::string);
int progress_func(void*, double, double, double, double);
void UpdateProgress(std::string);
double roundoff(double, unsigned char);