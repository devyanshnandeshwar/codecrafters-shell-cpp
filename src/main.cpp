#include <iostream>
#include <string>
#include <sstream>

int main()
{
  // Flush after every std::cout / std:cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  while (true)
  {
    std::cout << "$ ";

    std::string input;
    std::getline(std::cin, input);

    // parsing command and argument
    std::istringstream iss(input);
    std::string cmd, arg;
    iss >> cmd >> arg;

    if (cmd == "exit" && (arg.empty() || arg == "0"))
    {
      exit(0);
    }
    else
    {
      std::cout << input << ": command not found" << std::endl;
    }
  }
  return 0;
}
