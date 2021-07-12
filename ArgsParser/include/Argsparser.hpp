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
	//variables
	std::vector<std::string> options;
	//result: vector<pair<pair<PARAM,VALUE>,FOUND>>
	std::vector<std::pair<std::pair<std::string, std::string>,bool>> result;
	std::vector<std::pair<std::string, int>> pos;
	//functions
	bool ExistsInVector(std::vector<std::string>, std::string);
	int ExistsInVPair(std::vector<std::pair<std::string, int>>, std::string);
	bool ExistsInResult(std::vector<std::pair<std::pair<std::string, std::string>, bool>>, std::string);
public:
	ArgsParser(std::vector<std::string>);
	void Parse(std::vector<std::string>);
	std::vector<std::string> ConvertParams(int, char**);
	std::vector<std::pair<std::pair<std::string, std::string>, bool>> GetResult();
};
