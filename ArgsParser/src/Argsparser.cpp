#define ARGSPARSER_EXPORTS
#include <windows.hpp>

#include <Argsparser.hpp>
ArgsParser::ArgsParser(std::vector<std::string> _options) {
	options = _options;
}
void ArgsParser::Parse() {
	for (int i = 0; i < options.size(); i++) {
		std::cout << options[i] << std::endl;
	}
}