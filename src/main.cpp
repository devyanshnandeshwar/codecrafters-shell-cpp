#include <iostream>
#include <string>
#include <sstream>
#include <sys/stat.h>
#include <vector>
#include <sys/wait.h>

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
        bool found = false;
        if (path_env)
        {
          std::string path_var(path_env);
          std::istringstream path_stream(path_var);
          std::string dir;
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
        }
        if (!found)
        {
          std::cout << arg << ": not found" << std::endl;
        }
      }
      else
      {
        std::cout << "type: missing argument" << std::endl;
      }
    }
    else
    {
      // Parse command and arguments into a vector
      std::vector<std::string> tokens;
      std::istringstream iss2(input);
      std::string token;
      while (iss2 >> token)
      {
        tokens.push_back(token);
      }
      if (tokens.empty())
        continue;

      // Search PATH for executable
      char *path_env = std::getenv("PATH");
      std::string exec_path;
      bool found = false;
      if (path_env)
      {
        std::string path_var(path_env);
        std::istringstream path_stream(path_var);
        std::string dir;
        while (std::getline(path_stream, dir, ':'))
        {
          std::string full_path = dir + "/" + tokens[0];
          struct stat sb;
          if (stat(full_path.c_str(), &sb) == 0 && sb.st_mode & S_IXUSR)
          {
            exec_path = full_path;
            found = true;
            break;
          }
        }
      }
      if (!found)
      {
        std::cout << input << ": command not found" << std::endl;
        continue;
      }

      // Prepare arguments for execv
      std::vector<char *> argv;
      for (auto &str : tokens)
        argv.push_back(&str[0]);
      argv.push_back(nullptr);

      pid_t pid = fork();
      if (pid == 0)
      {
        // Child process
        execv(exec_path.c_str(), argv.data());
        // If execv fails
        std::cerr << "Failed to execute " << exec_path << std::endl;
        exit(1);
      }
      else if (pid > 0)
      {
        // Parent process
        int status;
        waitpid(pid, &status, 0);
      }
      else
      {
        std::cerr << "Failed to fork" << std::endl;
      }
    }
  }
  return 0;
}
