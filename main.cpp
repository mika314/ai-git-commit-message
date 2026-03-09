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

static size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::string *userp)
{
  userp->append((char *)contents, size * nmemb);
  return size * nmemb;
}

struct Model
{
  SER_PROPS(id);
  std::string id;
};

struct ModelsRsp
{
  SER_PROPS(data);
  std::vector<Model> data;
};

struct Choice
{
  Rsp message;
  SER_PROPS(message);
};

struct ChatRsp
{
  std::vector<Choice> choices;
  SER_PROPS(choices);
};

static std::string getCurrentModel()
{
  std::string jsonResponse;
  CURL *curl = curl_easy_init();
  if (!curl)
  {
    return "";
  }

  std::string url = "http://localhost:8080/v1/models";
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &jsonResponse);

  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK)
  {
    fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
  }

  curl_easy_cleanup(curl);

  auto tmp = json::Root{jsonResponse};
  auto rsp = ModelsRsp{};
  jsonDeser(tmp.root(), rsp);

  if (!rsp.data.empty())
  {
    return rsp.data[0].id;
  }

  return "";
}

struct ChatMessage
{
  SER_PROPS(role, content);
  std::string role;
  std::string content;
};

struct ChatReq
{
  SER_PROPS(messages, temperature, stop);
  std::vector<ChatMessage> messages;
  float temperature = 0.0f;
  std::vector<std::string> stop;
};

auto main() -> int
{
  Curl curl;
  const auto currentModel = getCurrentModel();
  const bool useChat = currentModel != "Meta-Llama-3.1-8B-Instruct-Q5_K_M.gguf" &&
                       currentModel != "Llama-3.2-3B-Instruct.Q5_K_M.gguf";

  const auto data = [&]() {
    const auto systemMsg =
      R"(You are an expert Git assistant who writes clear and concise commit messages based on provided diffs. Follow these guidelines strictly:

- **Do Not Include** any introductory or concluding text (e.g., "Here is the Git commit message:").
- **Summary Line**: Begin with a short, imperative sentence (max 50 characters), e.g., "Fix null pointer exception on startup".
- **Use Specific Verbs**: Use verbs like "Fix", "Add", "Update", "Remove", "Refactor", etc.
- **Separate Summary and Body**: Add a blank line between the summary and the detailed description.
- **Detailed Description**: Simply state what was changed. Do not guess intent or explain why a change was done if it is not clear from the diff. Do not provide extra details that the reader of the commit can infer from the diff easily, for example, file name. If summary provides enough information, omit the body part.
- **Formatting**: Do not include any markdown, bullet points, or references to 'git commit'.

**Examples of Good Commit Messages:**

```
Fix crash on file upload

Resolve an issue where the application crashes when users upload a file larger than 5MB due to memory allocation errors.
```

```
Implement JWT-based authentication
```

Current date and time: )" +
      []() -> std::string {
      std::time_t now = std::time(nullptr);
      std::tm *localTime = std::localtime(&now);
      char buffer[80];
      std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localTime);
      return buffer;
    }();
    if (useChat)
    {
      curl.setUrl("http://localhost:8080/v1/chat/completions");
      ChatReq req;
      req.messages.push_back({"system", systemMsg});

      req.messages.push_back(
        {"user",
         R"(Please write a Git commit message for the following diff. Remember to follow the guidelines strictly and **do not include any additional text outside the commit message**.

```diff
)" + getGitDiff() +
           R"(
```
)"});
      req.stop.push_back("<|eot_id|>");
      auto ss = std::ostringstream{};
      jsonSer(ss, req);
      return ss.str();
    }
    else
    {
      curl.setUrl("http://localhost:8080/completion");
      Req req;
      req.prompt = systemMsg +
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
    }
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
  if (useChat)
  {
    /*
    {
      "choices": [
        {
          "message": {
            "content": "Add March 2026 blood pressure readings\n\nUpdate the blood pressure log..."
          }
        }
      ],
    }
    */
    try
    {
      auto tmp = json::Root{rsp};
      auto rsp = ChatRsp{};
      jsonDeser(tmp.root(), rsp);
      if (rsp.choices.empty())
      {
        std::cout << "No result";
        return 0;
      }

      auto result = rsp.choices[0].message.content;

      while (!result.empty() && (result[result.size() - 1] == '"' || result[result.size() - 1] == '\n' ||
                                 result[result.size() - 1] == ' '))
        result.resize(result.size() - 1);

      if (result.empty())
      {
        std::cout << "No result";
        return 0;
      }
      std::cout << result;
    }
    catch (std::runtime_error &e)
    {
      LOG("error:", e.what());
    }
  }
  else
  {
    try
    {
      auto tmp = json::Root{rsp};
      auto rsp = Rsp{};
      jsonDeser(tmp.root(), rsp);

      auto result = rsp.content;

      while (!result.empty() && (result[result.size() - 1] == '"' || result[result.size() - 1] == '\n' ||
                                 result[result.size() - 1] == ' '))
        result.resize(result.size() - 1);

      if (result.empty())
      {
        std::cout << "No result";
        return 0;
      }
      std::cout << result;
    }
    catch (std::runtime_error &e)
    {
      LOG("error:", e.what());
    }
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
