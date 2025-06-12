#include <fcntl.h>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

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
      std::vector<std::string> tokens;
      std::string current;
      bool in_single_quote = false;
      bool in_double_quote = false;
      // Start after "echo " (find position after first space)
      size_t start = input.find("echo");
      if (start != std::string::npos)
        start += 4;
      while (start < input.size() && std::isspace(input[start]))
        ++start;
      for (size_t i = start; i < input.size(); ++i)
      {
        char c = input[i];
        if (in_single_quote)
        {
          if (c == '\'')
            in_single_quote = false;
          else
            current += c;
        }
        else if (in_double_quote)
        {
          if (c == '"')
            in_double_quote = false;
          else if (c == '\\' && i + 1 < input.size() &&
                   (input[i + 1] == '"' || input[i + 1] == '\\' || input[i + 1] == '$' || input[i + 1] == '\n'))
          {
            current += input[i + 1];
            ++i;
          }
          else
            current += c;
        }
        else
        {
          if (c == '\'')
            in_single_quote = true;
          else if (c == '"')
            in_double_quote = true;
          else if (c == '\\' && i + 1 < input.size())
          {
            current += input[i + 1];
            ++i;
          }
          else if (std::isspace(c))
          {
            if (!current.empty())
            {
              tokens.push_back(current);
              current.clear();
            }
          }
          else
            current += c;
        }
      }
      if (!current.empty())
        tokens.push_back(current);

      // Handle output redirection for echo
      std::string redirect_file, redirect_stderr_file, append_file;
      bool append_mode = false;
      for (size_t i = 0; i < tokens.size(); ++i)
      {
        if ((tokens[i] == ">" || tokens[i] == "1>") && i + 1 < tokens.size())
        {
          redirect_file = tokens[i + 1];
          append_mode = false;
          tokens.erase(tokens.begin() + i, tokens.begin() + i + 2);
          i -= 1;
        }
        else if ((tokens[i] == ">>" || tokens[i] == "1>>") && i + 1 < tokens.size())
        {
          redirect_file = tokens[i + 1];
          append_mode = true;
          tokens.erase(tokens.begin() + i, tokens.begin() + i + 2);
          i -= 1;
        }
        else if (tokens[i] == "2>" && i + 1 < tokens.size())
        {
          redirect_stderr_file = tokens[i + 1];
          tokens.erase(tokens.begin() + i, tokens.begin() + i + 2);
          i -= 1;
        }
      }

      int saved_stdout = -1;
      int fd = -1;
      if (!redirect_file.empty())
      {
        int flags = O_WRONLY | O_CREAT | (append_mode ? O_APPEND : O_TRUNC);
        fd = open(redirect_file.c_str(), flags, 0644);
        if (fd < 0)
        {
          std::cerr << "Failed to open file for redirection: " << redirect_file << std::endl;
          return 1;
        }
        saved_stdout = dup(1);
        dup2(fd, 1);
      }

      int saved_stderr = -1;
      int fd_err = -1;
      if (!redirect_stderr_file.empty())
      {
        fd_err = open(redirect_stderr_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd_err < 0)
        {
          std::cerr << "Failed to open file for stderr redirection: " << redirect_stderr_file << std::endl;
          if (fd != -1)
          {
            fflush(stdout);
            dup2(saved_stdout, 1);
            close(fd);
            close(saved_stdout);
          }
          return 1;
        }
        saved_stderr = dup(2);
        dup2(fd_err, 2);
      }

      // Print the rest joined by spaces
      for (size_t i = 0; i < tokens.size(); ++i)
      {
        if (i > 0)
          std::cout << " ";
        std::cout << tokens[i];
      }
      std::cout << std::endl;

      if (fd != -1)
      {
        fflush(stdout);
        dup2(saved_stdout, 1);
        close(fd);
        close(saved_stdout);
      }
      if (fd_err != -1)
      {
        fflush(stderr);
        dup2(saved_stderr, 2);
        close(fd_err);
        close(saved_stderr);
      }
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
      std::string current;
      bool in_single_quote = false;
      bool in_double_quote = false;
      for (size_t i = 0; i < input.size(); ++i)
      {
        char c = input[i];
        if (in_single_quote)
        {
          if (c == '\'')
            in_single_quote = false;
          else
            current += c;
        }
        else if (in_double_quote)
        {
          if (c == '"')
            in_double_quote = false;
          else if (c == '\\' && i + 1 < input.size() &&
                   (input[i + 1] == '"' || input[i + 1] == '\\' || input[i + 1] == '$' || input[i + 1] == '\n'))
          {
            current += input[i + 1];
            ++i;
          }
          else
            current += c;
        }
        else
        {
          if (c == '\'')
            in_single_quote = true;
          else if (c == '"')
            in_double_quote = true;
          else if (c == '\\' && i + 1 < input.size())
          {
            current += input[i + 1];
            ++i;
          }
          else if (std::isspace(c))
          {
            if (!current.empty())
            {
              tokens.push_back(current);
              current.clear();
            }
          }
          else
            current += c;
        }
      }
      if (!current.empty())
        tokens.push_back(current);
      if (tokens.empty())
        continue;

      // Handle output redirection
      int redirect_fd = -1;
      std::string redirect_file;
      bool append_mode = false;
      for (size_t i = 0; i < tokens.size(); ++i)
      {
        if ((tokens[i] == ">" || tokens[i] == "1>") && i + 1 < tokens.size())
        {
          redirect_file = tokens[i + 1];
          append_mode = false;
          // Remove the redirection operator and filename from tokens
          tokens.erase(tokens.begin() + i, tokens.begin() + i + 2);
          break;
        }
        else if ((tokens[i] == ">>" || tokens[i] == "1>>") && i + 1 < tokens.size())
        {
          redirect_file = tokens[i + 1];
          append_mode = true;
          tokens.erase(tokens.begin() + i, tokens.begin() + i + 2);
          break;
        }
      }

      // After handling > and 1> redirection, add:
      std::string redirect_stderr_file;
      for (size_t i = 0; i < tokens.size(); ++i)
      {
        if (tokens[i] == "2>" && i + 1 < tokens.size())
        {
          redirect_stderr_file = tokens[i + 1];
          tokens.erase(tokens.begin() + i, tokens.begin() + i + 2);
          break;
        }
      }

      pid_t pid = fork();
      if (pid == 0)
      {
        // Child process

        // Build argv
        std::vector<char *> argv;
        for (size_t i = 0; i < tokens.size(); ++i)
        {
          argv.push_back(const_cast<char *>(tokens[i].c_str()));
        }
        argv.push_back(nullptr);

        // Find exec_path
        std::string exec_path;
        if (tokens[0].find('/') == std::string::npos)
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
            std::cerr << tokens[0] << ": command not found" << std::endl;
            exit(1);
          }
        }
        else
        {
          exec_path = tokens[0];
        }

        if (!redirect_file.empty())
        {
          int flags = O_WRONLY | O_CREAT | (append_mode ? O_APPEND : O_TRUNC);
          int fd = open(redirect_file.c_str(), flags, 0644);
          if (fd < 0)
          {
            std::cerr << "Failed to open file for redirection: " << redirect_file << std::endl;
            exit(1);
          }
          dup2(fd, 1); // Redirect stdout to file
          close(fd);
        }
        if (!redirect_stderr_file.empty())
        {
          int fd_err = open(redirect_stderr_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
          if (fd_err < 0)
          {
            std::cerr << "Failed to open file for stderr redirection: " << redirect_stderr_file << std::endl;
            exit(1);
          }
          dup2(fd_err, 2); // Redirect stderr to file
          close(fd_err);
        }
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