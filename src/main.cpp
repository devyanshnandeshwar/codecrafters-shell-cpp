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
    std::string cmd;
    iss >> cmd;

    if (cmd == "exit")
    {
      std::string arg;
      iss >> arg;
      if (arg.empty() || arg == "0")
      {
        exit(0);
      }
    }
    else if (cmd == "echo")
    {
      std::string rest;
      std::getline(iss, rest);
      if (!rest.empty() && rest[0] == ' ')
        rest.erase(0, 1);
      std::cout << rest << std::endl;
    }
    else
    {
      std::cout << input << ": command not found" << std::endl;
    }
  }
  return 0;
}
