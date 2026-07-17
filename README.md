
# KemonoSearchC

![Static Badge](https://img.shields.io/badge/Platform-Linux-green)
![Website](https://img.shields.io/website?url=https%3A%2F%2Fpawchive.pw%2Fapi%2Fv1%2Fposts%2Frandom&up_message=UP&down_message=DOWN&label=API&cacheSeconds=600)

A terminal based utility for retrieving posts from the Kemono website,
allowing for filtering for specific strings in the post title.

## Prerequisites

This projects requires for CURL and CMAKE to be installed on your computer.

## Installation

First clone the repository, then:

``` bash
cd KemonoSearchC
cmake -S . -B build
cmake --build build
```

## Use

1. Run the program
2. Enter search string and filter string
3. Wait for the program to run. It will take some time because of DDoS guards.
4. Open the *index.html* file in the tools folder.
5. Within the webpage, select the outputted json file.

## Flags

- -d \<int> => Custom delay between page calls in milliseconds
- -f \<path> => Custom saves folder
- -i => Use user IDs instead of usernames. Useful if the user API is down, but reduces readability.
- -j \<path> => Custom output json path
- -s => Skips the check for previous identical searches and replaces the old save. Use if old search is out of date.
- -u => Bypasses the limit of 5000 posts per search, use only if necessary.
