#pragma once
#include <atomic>
#include <string>
#include <vector>

struct Recipe
{
	std::string name;
	std::string cuisine;
	std::string difficulty;
	int cookTimeMinutes;
	std::string image;
};

struct CommonObjects
{
	std::atomic_bool exit_flag = false;
	std::atomic_bool start_download = false;
	std::atomic_bool data_ready = false;
	std::string url;
	std::vector<Recipe> recipies;
};
