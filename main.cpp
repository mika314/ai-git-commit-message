#include "curl.hpp"
#include "pstream.h"
#include <ctime>
#include <iostream>
#include <json-ser/json-ser.hpp>
#include <json/json.hpp>
#include <log/log.hpp>
#include <optional>
#include <ser/macro.hpp>

struct Req
{
  std::string prompt;
  float temperature = 0.0f;
  std::vector<std::string> stop;
  int n_predict = 250;
  SER_PROPS(prompt, temperature, stop, n_predict);
};

struct Rsp
{
  std::string content;
  SER_PROPS(content);
};

static auto getGitDiff() -> std::string;

auto main() -> int
{
  Curl curl;
  curl.setUrl("http://localhost:8080/completion");
  const auto data = [&]() {
    Req req;
    req.prompt =
      R"(<|begin_of_text|><|start_header_id|>system<|end_header_id|>
You are an expert Git assistant who writes clear and concise commit messages based on provided diffs. Follow these guidelines strictly:

- **Do Not Include** any introductory or concluding text (e.g., "Here is the Git commit message:").
- **Summary Line**: Begin with a short, imperative sentence (max 50 characters), e.g., "Fix null pointer exception on startup".
- **Use Specific Verbs**: Use verbs like "Fix", "Add", "Update", "Remove", "Refactor", etc.
- **Separate Summary and Body**: Add a blank line between the summary and the detailed description.
- **Detailed Description**: Explain the **what** and **why**, not the **how**. Provide context or reasons for the change.
- **Formatting**: Do not include any markdown, bullet points, or references to 'git commit'.
- **Language**: Use clear, professional language without jargon.

**Examples of Good Commit Messages:**

```
Fix crash on file upload

Resolved an issue where the application would crash when users uploaded a file larger than 5MB due to memory allocation errors.
```

```
Add user authentication module

Implemented JWT-based authentication to secure user endpoints and prevent unauthorized access.
```

Current date and time: )" +
      []() -> std::string {
      std::time_t now = std::time(nullptr);
      std::tm *localTime = std::localtime(&now);
      char buffer[80];
      std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localTime);
      return buffer;
    }() +
                R"(
<|start_header_id|>user<|end_header_id|>
Please write a Git commit message for the following diff. Remember to follow the guidelines strictly and **do not include any additional text outside the commit message**.

```diff
)" + getGitDiff() +
                R"(
```
<|eot_id|>assistant
)";
    req.stop.push_back("<|eot_id|>");
    auto ss = std::ostringstream{};
    jsonSer(ss, req);
    return ss.str();
  }();
  curl.setPostFields(data);
  std::string rsp;
  curl.setWriteFunc([&](const char *data, size_t sz) {
    rsp += std::string_view{data, sz};
    return sz;
  });
  curl.setHeaders({"Content-Type: application/json"});
  auto r = curl.perform();
  if (r != CURLE_OK)
    LOG("error:", r);
  try
  {
    auto tmp = json::Root{rsp};
    auto rsp = Rsp{};
    jsonDeser(tmp.root(), rsp);

    auto result = rsp.content;

    while (!result.empty() && (result[result.size() - 1] == '"' || result[result.size() - 1] == '\n' ||
                               result[result.size() - 1] == ' '))
      result.resize(result.size() - 1);

    if (!result.empty())
      std::cout << result;
    else
      std::cout << "No result";
  }
  catch (std::runtime_error &e)
  {
    LOG("error:", e.what());
  }
}

auto getGitDiff() -> std::string
{
  redi::ipstream st("git diff --staged");
  std::string line;
  std::string str;
  while (std::getline(st, line) && str.size() < 1'000'000)
    str += line + "\n";
  if (str.empty() || str[str.size() - 1] != '\n')
    str += "\n";
  return str;
}

