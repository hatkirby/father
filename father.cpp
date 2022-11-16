#include <mastodonpp/mastodonpp.hpp>
#include <random>
#include <yaml-cpp/yaml.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <algorithm>
#include <set>
#include <list>
#include <iterator>
#include <verbly.h>
#include <hkutil/string.h>
#include <json.hpp>
#include "timeline.h"

// Sync followers every 4 hours.
const int CHECK_FOLLOWERS_EVERY = 4 * 60 / 5;

verbly::word findWordOfType(
  verbly::database& database,
  std::string form,
  verbly::part_of_speech partOfSpeech)
{
  std::vector<verbly::word> isThing = database.words(
    (verbly::notion::partOfSpeech == partOfSpeech)
    && (verbly::form::text == form)).all();

  if (isThing.empty())
  {
    return {};
  } else {
    return isThing.front();
  }
}

std::set<std::string> getPaginatedList(
  mastodonpp::Connection& connection,
  mastodonpp::API::endpoint_type endpoint,
  std::string account_id)
{
  std::set<std::string> result;

  mastodonpp::parametermap parameters;
  for (;;)
  {
    parameters["id"] = account_id;

    auto answer = connection.get(endpoint, parameters);
    if (!answer)
    {
      if (answer.curl_error_code == 0)
      {
        std::cout << "HTTP status: " << answer.http_status << std::endl;
      }
      else
      {
        std::cout << "libcurl error " << std::to_string(answer.curl_error_code)
             << ": " << answer.error_message << std::endl;
      }
      return {};
    }

    parameters = answer.next();
    if (parameters.empty()) break;

    nlohmann::json body = nlohmann::json::parse(answer.body);
    for (const auto& item : body)
    {
      result.insert(item["id"].get<std::string>());
    }
  }

  return result;
}

int main(int argc, char** argv)
{
  std::random_device randomDevice;
  std::mt19937 rng{randomDevice()};

  if (argc != 2)
  {
    std::cout << "usage: father [configfile]" << std::endl;
    return -1;
  }

  std::string configfile(argv[1]);
  YAML::Node config = YAML::LoadFile(configfile);

  verbly::database database(config["verbly_datafile"].as<std::string>());

  mastodonpp::Instance instance{
    config["mastodon_instance"].as<std::string>(),
    config["mastodon_token"].as<std::string>()};
  mastodonpp::Connection connection{instance};

  nlohmann::json account_details;
  {
    const mastodonpp::parametermap parameters {};
    auto answer = connection.get(mastodonpp::API::v1::accounts_verify_credentials, parameters);
    if (!answer)
    {
      if (answer.curl_error_code == 0)
      {
        std::cout << "HTTP status: " << answer.http_status << std::endl;
      }
      else
      {
        std::cout << "libcurl error " << std::to_string(answer.curl_error_code)
             << ": " << answer.error_message << std::endl;
      }
      return 1;
    }
    std::cout << answer.body << std::endl;
    account_details = nlohmann::json::parse(answer.body);
  }

  timeline home_timeline(mastodonpp::API::v1::timelines_home);
  home_timeline.poll(connection); // just ignore the results

  auto startedTime = std::chrono::system_clock::now();

  std::set<std::string> friends;
  int followerTimeout = 0;

  for (;;)
  {
    if (followerTimeout == 0)
    {
      // Sync friends with followers.
      try
      {
        friends = getPaginatedList(
          connection,
          mastodonpp::API::v1::accounts_id_following,
          account_details["id"].get<std::string>());

        std::set<std::string> followers = getPaginatedList(
          connection,
          mastodonpp::API::v1::accounts_id_followers,
          account_details["id"].get<std::string>());

        std::list<std::string> oldFriends;
        std::set_difference(
          std::begin(friends),
          std::end(friends),
          std::begin(followers),
          std::end(followers),
          std::back_inserter(oldFriends));

        std::set<std::string> newFollowers;
        std::set_difference(
          std::begin(followers),
          std::end(followers),
          std::begin(friends),
          std::end(friends),
          std::inserter(newFollowers, std::begin(newFollowers)));

        for (const std::string& f : oldFriends)
        {
          const mastodonpp::parametermap parameters {{"id", f}};
          auto answer = connection.post(mastodonpp::API::v1::accounts_id_unfollow, parameters);
          if (!answer)
          {
            if (answer.curl_error_code == 0)
            {
              std::cout << "HTTP status: " << answer.http_status << std::endl;
            }
            else
            {
              std::cout << "libcurl error " << std::to_string(answer.curl_error_code)
                   << ": " << answer.error_message << std::endl;
            }
          }
        }

        for (const std::string& f : newFollowers)
        {
          const mastodonpp::parametermap parameters {{"id", f}, {"reblogs", "false"}};
          auto answer = connection.post(mastodonpp::API::v1::accounts_id_follow, parameters);
          if (!answer)
          {
            if (answer.curl_error_code == 0)
            {
              std::cout << "HTTP status: " << answer.http_status << std::endl;
            }
            else
            {
              std::cout << "libcurl error " << std::to_string(answer.curl_error_code)
                   << ": " << answer.error_message << std::endl;
            }
          }
        }
      } catch (const std::exception& error)
      {
        std::cout << "Error while syncing followers: " << error.what()
          << std::endl;
      }

      followerTimeout = CHECK_FOLLOWERS_EVERY;
    }

    followerTimeout--;

    try
    {
      // Poll the timeline.
      std::list<nlohmann::json> posts = home_timeline.poll(connection);

      for (const nlohmann::json& post : posts)
      {
        if (
          // Only monitor people you are following
          friends.count(post["account"]["id"].get<std::string>())
          // Ignore retweets
          && post["reblog"].is_null())
        {
          std::string post_content = post["content"].get<std::string>();
          std::string::size_type pos;
          while ((pos = post_content.find("<")) != std::string::npos)
          {
            std::string prefix = post_content.substr(0, pos);
            std::string rest = post_content.substr(pos);
            std::string::size_type right_pos = rest.find(">");
            if (right_pos == std::string::npos) {
              post_content = prefix;
            } else {
              post_content = prefix + rest.substr(right_pos);
            }
          }

          std::vector<std::string> tokens =
            hatkirby::split<std::vector<std::string>>(post_content, " ");

          std::vector<std::string> canonical;
          for (std::string token : tokens)
          {
            std::string canonStr;
            for (char ch : token)
            {
              if (std::isalpha(ch))
              {
                canonStr += std::tolower(ch);
              }
            }

            canonical.push_back(canonStr);
          }

          std::vector<std::string>::iterator imIt =
            std::find(std::begin(canonical), std::end(canonical), "im");

          if (imIt != std::end(canonical))
          {
            imIt++;

            if (imIt != std::end(canonical))
            {
              verbly::token name;

              verbly::word firstAdverb = findWordOfType(
                database,
                *imIt,
                verbly::part_of_speech::adverb);

              if (firstAdverb.isValid())
              {
                std::vector<std::string>::iterator adjIt = imIt;
                adjIt++;

                if (adjIt != std::end(canonical))
                {
                  verbly::word secondAdjective = findWordOfType(
                    database,
                    *adjIt,
                    verbly::part_of_speech::adjective);

                  if (secondAdjective.isValid())
                  {
                    name << firstAdverb;
                    name << secondAdjective;
                  }
                }
              }

              if (name.isEmpty())
              {
                verbly::word firstAdjective = findWordOfType(
                  database,
                  *imIt,
                  verbly::part_of_speech::adjective);

                if (firstAdjective.isValid())
                {
                  name = firstAdjective;
                }
              }

              if ((!name.isEmpty())
                && (std::bernoulli_distribution(1.0/10.0)(rng)))
              {
                verbly::token action = {
                  "Hi",
                  verbly::token::punctuation(",",
                    verbly::token::capitalize(
                      verbly::token::casing::title_case,
                      name)),
                  "I'm Dad."};

                std::string result = "@" + post["account"]["acct"].get<std::string>() + " " + action.compile();

                mastodonpp::parametermap parameters{
                  {"status", result},
                  {"in_reply_to_id", post["id"].get<std::string>()}};

                auto answer{connection.post(mastodonpp::API::v1::statuses, parameters)};
                if (!answer)
                {
                  if (answer.curl_error_code == 0)
                  {
                    std::cout << "HTTP status: " << answer.http_status << std::endl;
                  }
                  else
                  {
                    std::cout << "libcurl error " << std::to_string(answer.curl_error_code)
                         << ": " << answer.error_message << std::endl;
                  }
                }
              }
            }
          }
        }
      }
    } catch (const std::exception&)
    {
      // Wait out the rate limit (10 minutes here and 5 below = 15).
      std::this_thread::sleep_for(std::chrono::minutes(10));
    }

    // We can poll the timeline at most once every five minutes.
    std::this_thread::sleep_for(std::chrono::minutes(5));
  }
}
