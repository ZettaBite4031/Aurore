#include <iostream>
#include <string>
#include <string_view>

int main(int argc, char** argv) {
	if (argc < 2) return 64;

	const std::string_view mode{ argv[1] };

	if (mode == "capture") {
		std::cout << "stdout-text\n";
		std::cerr << "stderr-text\n";
		return 7;
	}

	if (mode == "arguments") {
		for (int index{ 2 }; index < argc; ++index) 
			std::cout << argv[index] << '\n';
		return 0;
	}

	if (mode == "stdout-flood") {
		std::cout << std::string(4096, 'x');
		return 0;
	}

	return 65;
}
