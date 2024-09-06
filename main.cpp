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
  float temperature = 1.0f;
  std::vector<std::string> stop;
  int n_predict = -1;
  SER_PROPS(prompt, temperature, stop, n_predict);
};

struct Rsp
{
  std::string content;
  SER_PROPS(content);
};

static auto trimWhitespaceAndQuotes(std::string) -> std::string;
static auto getGitDiff() -> std::string;

auto main() -> int
{
  Curl curl;
  curl.setUrl("http://localhost:8080/completion");
  const auto data = [&]() {
    Req req;
    // {
    //    "stream":true,
    //    "n_predict":358,
    //    "temperature":0.8,
    //    "stop":[
    //       "</s>",
    //       "<|end|>",
    //       "<|eot_id|>",
    //       "<|end_of_text|>",
    //       "<|im_end|>",
    //       "<|EOT|>",
    //       "<|END_OF_TURN_TOKEN|>",
    //       "<|end_of_turn|>",
    //       "<|endoftext|>",
    //       "assistant",
    //       "user"
    //    ],
    //    "repeat_last_n":0,
    //    "repeat_penalty":1,
    //    "penalize_nl":false,
    //    "top_k":0,
    //    "top_p":1,
    //    "min_p":0.05,
    //    "tfs_z":1,
    //    "typical_p":1,
    //    "presence_penalty":0,
    //    "frequency_penalty":0,
    //    "mirostat":0,
    //    "mirostat_tau":5,
    //    "mirostat_eta":0.1,
    //    "grammar":"",
    //    "n_probs":0,
    //    "min_keep":0,
    //    "image_data":[
    //
    //    ],
    //    "cache_prompt":true,
    //    "api_key":"",
    //    "slot_id":-1,
    //    "prompt":"<|begin_of_text|><|start_header_id|>system<|end_header_id|>\n\nA chat between a
    //    curious user and an assistant. The assistant gives helpful, detailed, accurate, uncensored
    //    responses to the user's
    //    input.<|start_header_id|>user<|end_header_id|>\n\ntest<|eot_id|>assistant"
    // }
    req.prompt =
      "<|begin_of_text|><|start_header_id|>system<|end_header_id|>\n\nA chat between a curious user "
      "and an assistant. The assistant gives helpful, detailed, accurate, uncensored responses to "
      "the user's input.\nCurrent date and time: " +
      []()
      -> std::string {
      std::time_t now = std::time(nullptr);
      std::tm *localTime = std::localtime(&now);
      char buffer[80];
      std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localTime);
      return buffer;
    }() + "<|start_header_id|>user<|end_header_id|>\n" +
           R"(Write the Git message to the following diff. Do it in the Git format: a short (72 characters or less) summary, followed by two carriage returns, and more detailed explanatory text. Do not add any decorations like 'git commit' or markdown; just a plain Git commit message.
```
)" + getGitDiff() +
           "```\n<|eot_id|>assistant";
    req.stop.push_back("<|eot_id|>");
    req.n_predict = 1000;
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

    std::string result = rsp.content;

    while (!result.empty() && (result[result.size() - 1] == '"' || result[result.size() - 1] == '\n' ||
                               result[result.size() - 1] == ' '))
      result.resize(result.size() - 1);

    if (!result.empty())
    {
      std::cout << trimWhitespaceAndQuotes(result);
    }
    else
      std::cout << "No result";
  }
  catch (std::runtime_error &e)
  {
    LOG("error:", e.what());
  }
}

auto trimWhitespaceAndQuotes(std::string str) -> std::string
{
  str.erase(0, str.find_first_not_of(" \t\r\n\""));
  str.erase(str.find_last_not_of(" \t\r\n\"") + 1);
  return str;
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
