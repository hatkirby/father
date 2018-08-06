#include <twitter.h>
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

  twitter::auth auth(
    config["consumer_key"].as<std::string>(),
    config["consumer_secret"].as<std::string>(),
    config["access_key"].as<std::string>(),
    config["access_secret"].as<std::string>());

  auto startedTime = std::chrono::system_clock::now();

  twitter::client client(auth);

  std::set<twitter::user_id> friends;
  int followerTimeout = 0;

  for (;;)
  {
    if (followerTimeout == 0)
    {
      // Sync friends with followers.
      try
      {
        friends = client.getFriends();

        std::set<twitter::user_id> followers = client.getFollowers();

        std::list<twitter::user_id> oldFriends;
        std::set_difference(
          std::begin(friends),
          std::end(friends),
          std::begin(followers),
          std::end(followers),
          std::back_inserter(oldFriends));

        std::list<twitter::user_id> newFollowers;
        std::set_difference(
          std::begin(followers),
          std::end(followers),
          std::begin(friends),
          std::end(friends),
          std::back_inserter(newFollowers));

        for (twitter::user_id f : oldFriends)
        {
          client.unfollow(f);
        }

        for (twitter::user_id f : newFollowers)
        {
          client.follow(f);
        }
      } catch (const twitter::twitter_error& error)
      {
        std::cout << "Twitter error while syncing followers: " << error.what()
          << std::endl;
      }

      followerTimeout = CHECK_FOLLOWERS_EVERY;
    }

    followerTimeout--;

    try
    {
      // Poll the timeline.
      std::list<twitter::tweet> tweets = client.getHomeTimeline().poll();

      for (twitter::tweet& tweet : tweets)
      {
        auto createdTime =
          std::chrono::system_clock::from_time_t(tweet.getCreatedAt());

        if (
          // Only monitor people you are following
          friends.count(tweet.getAuthor().getID())
          // Ignore tweets from before the bot started up
          && createdTime > startedTime
          // Ignore retweets
          && !tweet.isRetweet()
          // Ignore messages
          && tweet.getText().front() != '@')
        {
          std::vector<std::string> tokens =
            hatkirby::split<std::vector<std::string>>(tweet.getText(), " ");

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

                std::string result =
                  tweet.generateReplyPrefill(client.getUser())
                  + action.compile();

                std::cout << result << std::endl;

                if (result.length() <= 140)
                {
                  try
                  {
                    client.replyToTweet(result, tweet);
                  } catch (const twitter::twitter_error& e)
                  {
                    std::cout << "Twitter error while tweeting: " << e.what()
                      << std::endl;
                  }
                }
              }
            }
          }
        }
      }
    } catch (const twitter::rate_limit_exceeded&)
    {
      // Wait out the rate limit (10 minutes here and 5 below = 15).
      std::this_thread::sleep_for(std::chrono::minutes(10));
    }

    // We can poll the timeline at most once every five minutes.
    std::this_thread::sleep_for(std::chrono::minutes(5));
  }
}
