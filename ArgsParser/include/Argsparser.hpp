#include <vector>
#include <string>
#include <iostream>

#ifndef __linux__
#ifdef ARGSPARSER_EXPORTS
#define ARGSPARSER_API __declspec(dllexport)
#else
#define ARGSPARSER_API __declspec(dllimport)
#endif
class ARGSPARSER_API ArgsParser {
#else
class ArgsParser {
#endif
private:
	std::vector<std::string> options;
public:
	ArgsParser(std::vector<std::string>);
	void Parse();
};
