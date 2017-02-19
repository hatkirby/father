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

template <class OutputIterator>
void split(std::string input, std::string delimiter, OutputIterator out)
{
  while (!input.empty())
  {
    int divider = input.find(delimiter);
    if (divider == std::string::npos)
    {
      *out = input;
      out++;

      input = "";
    } else {
      *out = input.substr(0, divider);
      out++;

      input = input.substr(divider+delimiter.length());
    }
  }
}

template <class Container>
Container split(std::string input, std::string delimiter)
{
  Container result;

  split(input, delimiter, std::back_inserter(result));

  return result;
}

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

  twitter::auth auth;
  auth.setConsumerKey(config["consumer_key"].as<std::string>());
  auth.setConsumerSecret(config["consumer_secret"].as<std::string>());
  auth.setAccessKey(config["access_key"].as<std::string>());
  auth.setAccessSecret(config["access_secret"].as<std::string>());

  std::set<twitter::user_id> streamedFriends;

  verbly::database database(config["verbly_datafile"].as<std::string>());

  twitter::client client(auth);

  std::cout << "Starting streaming..." << std::endl;
  twitter::stream userStream(client, [&] (const twitter::notification& n) {
    if (n.getType() == twitter::notification::type::friends)
    {
      streamedFriends = n.getFriends();
    } else if (n.getType() == twitter::notification::type::follow)
    {
      streamedFriends.insert(n.getUser().getID());
    } else if (n.getType() == twitter::notification::type::unfollow)
    {
      streamedFriends.erase(n.getUser().getID());
    } else if (n.getType() == twitter::notification::type::tweet)
    {
      if (
        // Only monitor people you are following
        (streamedFriends.count(n.getTweet().getAuthor().getID()) == 1)
        // Ignore retweets
        && (!n.getTweet().isRetweet())
        // Ignore messages
        && (n.getTweet().getText().front() != '@')
      )
      {
        std::vector<std::string> tokens =
          split<std::vector<std::string>>(n.getTweet().getText(), " ");

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
                  name << verbly::token::capitalize(firstAdverb);
                  name << verbly::token::capitalize(secondAdjective);
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
                name = verbly::token::capitalize(firstAdjective);
              }
            }

            if ((!name.isEmpty())
              && (std::bernoulli_distribution(1.0/10.0)(rng)))
            {
              verbly::token action = {
                "Hi",
                verbly::token::punctuation(",", name),
                "I'm Dad."};

              std::string result =
                n.getTweet().generateReplyPrefill(client.getUser())
                + action.compile();

              if (result.length() <= 140)
              {
                try
                {
                  client.replyToTweet(result, n.getTweet());
                } catch (const twitter::twitter_error& e)
                {
                  std::cout << "Twitter error: " << e.what() << std::endl;
                }
              }
            }
          }
        }
      }
    } else if (n.getType() == twitter::notification::type::followed)
    {
      try
      {
        client.follow(n.getUser());
      } catch (const twitter::twitter_error& error)
      {
        std::cout << "Twitter error while following @"
          << n.getUser().getScreenName() << ": " << error.what() << std::endl;
      }
    }
  });

  std::this_thread::sleep_for(std::chrono::minutes(1));

  // Every once in a while, check if we've lost any followers, and if we have,
  // unfollow the people who have unfollowed us.
  for (;;)
  {
    try
    {
      std::set<twitter::user_id> friends = client.getFriends();
      std::set<twitter::user_id> followers = client.getFollowers();

      std::list<twitter::user_id> oldFriends;
      std::set_difference(
        std::begin(friends),
        std::end(friends),
        std::begin(followers),
        std::end(followers),
        std::back_inserter(oldFriends));

      for (auto f : oldFriends)
      {
        client.unfollow(f);
      }

      std::list<twitter::user_id> newFollowers;
      std::set_difference(
        std::begin(followers),
        std::end(followers),
        std::begin(friends),
        std::end(friends),
        std::back_inserter(newFollowers));

      for (auto f : newFollowers)
      {
        client.follow(f);
      }
    } catch (const twitter::twitter_error& e)
    {
      std::cout << "Twitter error: " << e.what() << std::endl;
    }

    std::this_thread::sleep_for(std::chrono::hours(4));
  }
}

