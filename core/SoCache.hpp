#pragma once

// The cache of the shared objects of the game coordinator of Deadlock: game/citadel/cache_<account id>.soc. The game saves there
// what the coordinator sends on the login (CMsgSOCacheSubscribed): the field 2 is the cache, its field 4 is an object
// {1: type id, 2: the data}. The type 104 is CSOGameAccountClient (the numbers of its fields are from the descriptor in client.dll,
// 2026-10-16) - the wins, the bans of the matchmaking, the low priority.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace SoCache
{
	constexpr int kAccountObjectType = 104;

	// CSOGameAccountClient::EFlags
	constexpr uint64_t kFlagDeveloper = 1;
	constexpr uint64_t kFlagAccountBanned = 64;

	struct Account_t
	{
		bool Valid = false;			// the object of the account was found
		uint32_t AccountId = 0;		// the id of the file name (cache_<id>.soc)
		int64_t FileTime = 0;		// the time of the file (unix): the data are as of the last login of the account into the game
		std::filesystem::path Path;

		uint64_t Flags = 0;
		uint32_t Wins = 0;
		uint32_t Losses = 0;
		uint32_t Kills = 0;
		uint32_t MostPlayedHero = 0;
		uint32_t MmBanUntil = 0;		// unix time
		uint32_t CommsBanUntil = 0;
		uint32_t LowPriorityGames = 0;	// the wins left in the low priority queue
		uint32_t ReportBanUntil = 0;
		uint32_t PriorityTokens = 0;
		uint32_t PriorityTokenProgress = 0;
		uint32_t PriorityTokensEarned = 0;
		uint32_t BrawlWins = 0;
		uint32_t BrawlLosses = 0;
		uint32_t BrawlKills = 0;
		uint32_t LastMatchTime = 0;
		uint32_t Privacy = 0;
	};

	// one file; false - not read or no object of the account in it
	auto Read( const std::filesystem::path& Path , uint32_t AccountId , Account_t& Out ) -> bool;

	// every cache_<id>.soc in the folder game\citadel of Deadlock
	auto Scan( const std::filesystem::path& CitadelDir ) -> std::vector<Account_t>;
}
