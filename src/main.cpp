#include <fcntl.h>
#include <iostream>
#include <readline/history.h>
#include <readline/readline.h>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include <dirent.h>
#include <algorithm>

char *builtin_generator(const char *text, int state)
{
  static const char *builtins[] = {"echo", "exit", "history", nullptr};
  static int list_index, len;
  if (!state)
  {
    list_index = 0;
    len = strlen(text);
  }
  const char *name;
  while ((name = builtins[list_index++]))
  {
    if (strncmp(name, text, len) == 0)
    {
      // Allocate a new string for readline to use
      char *completion = (char *)malloc(strlen(name) + 2);
      strcpy(completion, name);
      // strcat(completion, ""); // Add a space after completion
      return completion;
    }
  }
  return nullptr;
}

char **builtin_completion(const char *text, int start, int end)
{
  // Only complete at the start of the line (first word)
  if (start != 0)
    return nullptr;
  rl_attempted_completion_over = 1;
  char **matches = rl_completion_matches(text, builtin_generator);
  if (!matches || !matches[0])
  {
    // No matches, ring bell
    std::cout << "\a" << std::flush;
    return nullptr;
  }
  return matches;
}

char *external_command_generator(const char *text, int state)
{
  static std::vector<std::string> matches;
  static size_t match_index;
  if (!state)
  {
    matches.clear();
    match_index = 0;
    // Get PATH
    const char *path_env = getenv("PATH");
    if (!path_env)
      return nullptr;
    std::string path_var(path_env);
    std::istringstream path_stream(path_var);
    std::string dir;
    size_t len = strlen(text);
    while (std::getline(path_stream, dir, ':'))
    {
      DIR *dp = opendir(dir.c_str());
      if (!dp)
        continue;
      struct dirent *entry;
      while ((entry = readdir(dp)))
      {
        std::string name(entry->d_name);
        if (name.compare(0, len, text) == 0)
        {
          std::string full_path = dir + "/" + name;
          struct stat sb;
          if (stat(full_path.c_str(), &sb) == 0 && sb.st_mode & S_IXUSR && !(sb.st_mode & S_IFDIR))
          {
            matches.push_back(name);
          }
        }
      }
      closedir(dp);
    }
    // Remove duplicates
    std::sort(matches.begin(), matches.end());
    matches.erase(std::unique(matches.begin(), matches.end()), matches.end());
  }
  if (match_index < matches.size())
  {
    std::string result = matches[match_index++];
    return strdup(result.c_str());
  }
  return nullptr;
}

char **command_completion(const char *text, int start, int end)
{
  if (start != 0)
    return nullptr;
  rl_attempted_completion_over = 1;

  // Try builtins first
  char **builtin_matches = rl_completion_matches(text, builtin_generator);
  bool has_builtin = (builtin_matches && builtin_matches[0]);

  // Try external commands
  char **external_matches = rl_completion_matches(text, external_command_generator);
  bool has_external = (external_matches && external_matches[0]);

  // If neither, ring bell
  if (!has_builtin && !has_external)
  {
    std::cout << "\a" << std::flush;
    return nullptr;
  }

  // Merge both sets of matches
  std::vector<char *> all_matches;
  if (has_builtin)
  {
    for (int i = 0; builtin_matches[i]; ++i)
      all_matches.push_back(builtin_matches[i]);
    free(builtin_matches);
  }
  if (has_external)
  {
    for (int i = 0; external_matches[i]; ++i)
      all_matches.push_back(external_matches[i]);
    free(external_matches);
  }
  all_matches.push_back(nullptr);

  // Copy to char** for readline
  char **result = (char **)malloc(sizeof(char *) * all_matches.size());
  for (size_t i = 0; i < all_matches.size(); ++i)
    result[i] = all_matches[i];
  return result;
}

int main()
{
  // Flush after every std::cout / std:cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  rl_attempted_completion_function = command_completion;

  int last_appended_history = 0;

  // --- Load history from HISTFILE on startup ---
  const char *histfile = std::getenv("HISTFILE");
  if (histfile && histfile[0] != '\0')
  {
    read_history(histfile);
  }

  while (true)
  {
    char *input_c = readline("$ ");
    if (!input_c)
      break;

    std::string input(input_c);
    add_history(input_c);
    free(input_c);

    // --- PIPELINE HANDLING: must come before parsing cmd ---
    size_t pipe_pos = input.find('|');
    if (pipe_pos != std::string::npos)
    {
      // --- Split input into pipeline stages ---
      std::vector<std::string> stages;
      size_t start = 0;
      while (true)
      {
        size_t pos = input.find('|', start);
        if (pos == std::string::npos)
        {
          stages.push_back(input.substr(start));
          break;
        }
        stages.push_back(input.substr(start, pos - start));
        start = pos + 1;
      }

      // --- Tokenize each stage ---
      auto trim = [](std::string &s)
      {
        size_t start = s.find_first_not_of(" \t");
        size_t end = s.find_last_not_of(" \t");
        if (start == std::string::npos)
        {
          s = "";
          return;
        }
        s = s.substr(start, end - start + 1);
      };
      auto tokenize = [](const std::string &s)
      {
        std::vector<std::string> tokens;
        std::string current;
        bool in_single_quote = false, in_double_quote = false;
        for (size_t i = 0; i < s.size(); ++i)
        {
          char c = s[i];
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
            else if (c == '\\' && i + 1 < s.size() &&
                     (s[i + 1] == '"' || s[i + 1] == '\\' || s[i + 1] == '$' || s[i + 1] == '\n'))
            {
              current += s[++i];
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
            else if (c == '\\' && i + 1 < s.size())
            {
              current += s[++i];
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
        return tokens;
      };

      std::vector<std::vector<std::string>> pipeline_tokens;
      for (auto &stage : stages)
      {
        trim(stage);
        pipeline_tokens.push_back(tokenize(stage));
      }

      int n = pipeline_tokens.size();
      std::vector<int> pfd(2 * (n - 1)); // pipes: [read, write, read, write, ...]
      for (int i = 0; i < n - 1; ++i)
      {
        if (pipe(&pfd[2 * i]) == -1)
        {
          std::cerr << "Failed to create pipe" << std::endl;
          return 1;
        }
      }

      std::vector<pid_t> pids;
      for (int i = 0; i < n; ++i)
      {
        pid_t pid = fork();
        if (pid == 0)
        {
          // Set up stdin
          if (i > 0)
          {
            dup2(pfd[2 * (i - 1)], 0);
          }
          // Set up stdout
          if (i < n - 1)
          {
            dup2(pfd[2 * i + 1], 1);
          }
          // Close all pipe fds in child
          for (int j = 0; j < 2 * (n - 1); ++j)
            close(pfd[j]);

          // --- Built-in handling ---
          auto &tokens = pipeline_tokens[i];
          if (!tokens.empty() && tokens[0] == "echo")
          {
            for (size_t k = 1; k < tokens.size(); ++k)
            {
              if (k > 1)
                std::cout << " ";
              std::cout << tokens[k];
            }
            std::cout << std::endl;
            exit(0);
          }
          else if (!tokens.empty() && tokens[0] == "type")
          {
            if (tokens.size() < 2)
              std::cout << "type: missing argument" << std::endl;
            else if (tokens[1] == "echo" || tokens[1] == "exit" || tokens[1] == "type" || tokens[1] == "history")
              std::cout << tokens[1] << " is a shell builtin" << std::endl;
            else
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
                  std::string full_path = dir + "/" + tokens[1];
                  struct stat sb;
                  if (stat(full_path.c_str(), &sb) == 0 && sb.st_mode & S_IXUSR)
                  {
                    std::cout << tokens[1] << " is " << full_path << std::endl;
                    found = true;
                    break;
                  }
                }
              }
              if (!found)
                std::cout << tokens[1] << ": not found" << std::endl;
            }
            exit(0);
          }
          // --- External command ---
          if (!tokens.empty())
          {
            std::vector<char *> argv;
            for (auto &t : tokens)
              argv.push_back(const_cast<char *>(t.c_str()));
            argv.push_back(nullptr);
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
            execv(exec_path.c_str(), argv.data());
            std::cerr << "Failed to execute " << exec_path << std::endl;
            exit(1);
          }
          exit(0);
        }
        else if (pid > 0)
        {
          pids.push_back(pid);
        }
        else
        {
          std::cerr << "Failed to fork" << std::endl;
          // Close all pipes
          for (int j = 0; j < 2 * (n - 1); ++j)
            close(pfd[j]);
          return 1;
        }
      }
      // Parent closes all pipe fds
      for (int j = 0; j < 2 * (n - 1); ++j)
        close(pfd[j]);
      // Wait for all children
      for (pid_t pid : pids)
      {
        int status;
        waitpid(pid, &status, 0);
      }
      continue;
    }
    else
    {
      // Now parse cmd and handle builtins as before
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
        std::string redirect_file, redirect_stderr_file;
        bool append_mode = false;
        bool append_stderr_mode = false;
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
            append_stderr_mode = false;
            tokens.erase(tokens.begin() + i, tokens.begin() + i + 2);
            i -= 1;
          }
          else if (tokens[i] == "2>>" && i + 1 < tokens.size())
          {
            redirect_stderr_file = tokens[i + 1];
            append_stderr_mode = true;
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
          int flags = O_WRONLY | O_CREAT | (append_stderr_mode ? O_APPEND : O_TRUNC);
          fd_err = open(redirect_stderr_file.c_str(), flags, 0644);
          if (fd_err < 0)
          {
            std::cerr << "Failed to open file for stderr redirection: " << redirect_stderr_file << std::endl;
            // restore stdout if needed
            if (fd != -1)
            {
              fflush(stdout);
              dup2(saved_stdout, 1);
              close(fd);
              close(saved_stdout);
            }
            return 1; // or exit(1) in child
          }
          saved_stderr = dup(2);
          dup2(fd_err, 2);
        }

        // Print the rest joined by spaces (no extra space if no args)
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
        if (arg == "echo" || arg == "exit" || arg == "type" || arg == "history")
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
      else if (cmd == "history")
      {
        std::string arg1, arg2;
        iss >> arg1 >> arg2;
        if (arg1 == "-r" && !arg2.empty())
        {
          read_history(arg2.c_str());
          continue;
        }
        if (arg1 == "-w" && !arg2.empty())
        {
          write_history(arg2.c_str());
          last_appended_history = history_length;
          continue;
        }
        if (arg1 == "-a" && !arg2.empty())
        {
          // Append new history entries to file
          HIST_ENTRY **hist_list = history_list();
          if (hist_list)
          {
            FILE *f = fopen(arg2.c_str(), "a");
            if (f)
            {
              int total = 0;
              while (hist_list[total])
                ++total;
              for (int i = last_appended_history; i < total; ++i)
              {
                fprintf(f, "%s\n", hist_list[i]->line);
              }
              fclose(f);
              last_appended_history = total;
            }
          }
          continue;
        }

        int n = -1;
        if (!arg1.empty() && arg1 != "-r" && arg1 != "-w")
        {
          try
          {
            n = std::stoi(arg1);
          }
          catch (...)
          {
            n = -1;
          }
        }
        HIST_ENTRY **hist_list = history_list();
        if (hist_list)
        {
          int total = 0;
          while (hist_list[total])
            ++total;
          int start = 0;
          if (n > 0 && n < total)
            start = total - n;
          for (int i = start; i < total; ++i)
          {
            std::cout << "    " << (i + 1) << "  " << hist_list[i]->line << std::endl;
          }
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
        bool append_stderr_mode = false;
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
          else if (tokens[i] == "2>>" && i + 1 < tokens.size())
          {
            redirect_stderr_file = tokens[i + 1];
            append_stderr_mode = true;
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
            int flags = O_WRONLY | O_CREAT | (append_stderr_mode ? O_APPEND : O_TRUNC);
            int fd_err = open(redirect_stderr_file.c_str(), flags, 0644);
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
  }

  // --- Save history to HISTFILE on exit ---

  return 0;
}