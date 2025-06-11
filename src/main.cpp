#include <iostream>
#include <string>
#include <sstream>
#include <sys/stat.h>
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
    else if (cmd == "type")
    {
      std::string arg;
      iss >> arg;
      if (arg == "echo" || arg == "exit" || arg == "type")
      {
        std::cout << arg << " is a shell builtin" << std::endl;
      }
      else if (!arg.empty())
      {
        char *path_env = std::getenv("PATH");
        if (path_env)
        {
          std::string path_var(path_env);
          std::istringstream path_stream(path_var);
          std::string dir;
          bool found = false;
          while (std::getline(path_stream, dir, ':'))
          {
            std::string full_path = dir + "/" + arg;
            struct stat sb;
            if (stat(full_path.c_str(), &sb) == 0 && sb.st_mode & S_IXUSR)
            {
              std::cout << arg << " is " << full_path << std::endl;
              found = true;
              break;
            }
          }
          if (!found)
          {
            std::cout << arg << ": not found" << std::endl;
          }
          else
          {
            std::cout << arg << ": not found" << std::endl;
          }
        }
      }
      else
      {
        std::cout << "type: missing argument" << std::endl;
      }
    }
    else
    {
      std::cout << input << ": command not found" << std::endl;
    }
  }
  return 0;
}
