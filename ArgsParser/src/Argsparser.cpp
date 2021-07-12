#define ARGSPARSER_EXPORTS
#include <windows.hpp>

#include <Argsparser.hpp>
ArgsParser::ArgsParser(std::vector<std::string> _options) {
	options = _options;
}
void ArgsParser::Parse(std::vector<std::string> params) {
	//store each param index
	for (int i = 0; i < params.size(); i++) {
		if (ExistsInVector(options, params[i])) {
			int index = ExistsInVPair(pos, params[i]);
			if (index == -1) {
				pos.push_back(std::make_pair(params[i], i));
			}
			else {
				pos[index].second = i;
			}
		}
	}

	//loop through their index to get values
	for (int i = 0; i < pos.size(); i++) {
		int index = pos[i].second;
		std::string param = pos[i].first;
		result.push_back(std::make_pair(std::make_pair(param,""), true));
		if (index + 1 < params.size()) {
			//check if there is a value for this param
			if (!ExistsInVector(options, params[index + 1])) {
				//value
				result[result.size() - 1].first.second = params[index + 1];
			}
		}
	}

	//checking for not passed params
	for (int i = 0; i < options.size(); i++) {
		if (!ExistsInResult(result, options[i])) {
			result.push_back(std::make_pair(std::make_pair(options[i], ""), false));
		}
	}
}

std::vector<std::string> ArgsParser::ConvertParams(int argc, char** argv) {
	std::vector<std::string> r;
	r.clear();
	for (int i = 0; i < argc; i++) {
		r.push_back(std::string(argv[i]));
	}
	return r;
}

bool ArgsParser::ExistsInVector(std::vector<std::string> v, std::string s) {
	for (int i = 0; i < v.size(); i++) {
		if (v[i] == s) {
			return true;
		}
	}
	return false;
}

int ArgsParser::ExistsInVPair(std::vector<std::pair<std::string, int>> v, std::string s) {
	for (int i = 0; i < v.size(); i++) {
		if (v[i].first == s) {
			return i;
		}
	}
	return -1;
}

bool ArgsParser::ExistsInResult(std::vector<std::pair<std::pair<std::string, std::string>, bool>> v, std::string s) {
	for (int i = 0; i < v.size(); i++) {
		if (v[i].first.first == s) {
			return true;
		}
	}
	return false;
}

std::vector<std::pair<std::pair<std::string, std::string>, bool>> ArgsParser::GetResult() {
	return result;
}