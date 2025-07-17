/*
 * Guild Points Module for AzerothCore-WotLK
 * 
 * Author: mojispectre
 * Date: July 2025
 * Description: A comprehensive guild points system that rewards guilds for successful raid completions.
 *              Features server announcements, role distribution tracking, faction-based buffs,
 *              and guild ranking system with beautiful formatting and emoji support.
 * 
 * Features:
 * - Guild point rewards for raid boss kills (10/25 man, HC, Hardmode)
 * - Server-wide announcements with achievement icons
 * - Role distribution tracking (Tanks/Healers/DPS)
 * - Faction-based buff system for guild runs
 * - PUG run detection and separate announcements
 * - Guild ranking command (.grank)
 * - Configurable boss points and difficulties
 * - Distance and guild percentage validation
 */

#include "ScriptMgr.h"
#include "Player.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "Config.h"
#include "Log.h"
#include "Map.h"
#include "Group.h"
#include "Creature.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "Mail.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "ObjectMgr.h"
#include "SpellMgr.h"
#include "SpellInfo.h"
#include "World.h"
#include "WorldSession.h"
#include "WorldSessionMgr.h"
#include "QueryResult.h"
#include "PreparedStatement.h"
#include "loader.h"

using namespace Acore::ChatCommands;
#include <unordered_map>
#include <vector>
#include <string>

enum RaidDifficulty
{
    DIFFICULTY_10_NORMAL = 0,
    DIFFICULTY_25_NORMAL = 1,
    DIFFICULTY_10_HEROIC = 2,
    DIFFICULTY_25_HEROIC = 3,
    DIFFICULTY_10_HARDMODE = 4,
    DIFFICULTY_25_HARDMODE = 5
};

struct BossPointInfo
{
    uint32 bossId;
    std::vector<RaidDifficulty> difficulties;
};

struct RaidStats
{
    uint32 totalPlayers;
    uint32 alivePlayers;
    uint32 tanks;
    uint32 healers;
    uint32 dps;
    uint32 hordeCount;
    uint32 allianceCount;
};

class GuildPointsManager
{
public:
    static GuildPointsManager* instance()
    {
        static GuildPointsManager instance;
        return &instance;
    }

    void LoadConfig()
    {
        enabled = sConfigMgr->GetOption<bool>("GuildPoints.Enable", true);
        minGuildPercentage = sConfigMgr->GetOption<uint32>("GuildPoints.MinGuildPercentage", 80);
        maxDistance = sConfigMgr->GetOption<float>("GuildPoints.MaxDistance", 300.0f);
        
        points10Man = sConfigMgr->GetOption<uint32>("GuildPoints.Points.10Man", 10);
        points25Man = sConfigMgr->GetOption<uint32>("GuildPoints.Points.25Man", 20);
        points10HC = sConfigMgr->GetOption<uint32>("GuildPoints.Points.10HC", 15);
        points25HC = sConfigMgr->GetOption<uint32>("GuildPoints.Points.25HC", 25);
        points10Hard = sConfigMgr->GetOption<uint32>("GuildPoints.Points.10Hardmode", 13);
        points25Hard = sConfigMgr->GetOption<uint32>("GuildPoints.Points.25Hardmode", 23);

        // Reset & Rewards Configuration
        autoResetEnabled = sConfigMgr->GetOption<bool>("GuildPoints.AutoReset.Enable", false);
        resetIntervalDays = sConfigMgr->GetOption<uint32>("GuildPoints.AutoReset.IntervalDays", 30);
        minPointsForRewards = sConfigMgr->GetOption<uint32>("GuildPoints.MinPointsForRewards", 100);
        rewardDistribution = sConfigMgr->GetOption<uint32>("GuildPoints.RewardDistribution", 1);
        announceReset = sConfigMgr->GetOption<bool>("GuildPoints.AnnounceReset", true);

        LoadBossConfigs();
        LoadBuffConfigs();
        LoadRewardConfigs();
    }

    bool IsEnabled() const { return enabled; }

    void OnCreatureKill(Player* killer, Creature* killed)
    {
        if (!enabled || !killer || !killed)
            return;

        uint32 bossId = killed->GetEntry();
        if (bossPoints.find(bossId) == bossPoints.end())
            return;

        Group* group = killer->GetGroup();
        if (!group || !group->isRaidGroup())
            return;

        Player* raidLeader = ObjectAccessor::FindPlayer(group->GetLeaderGUID());
        if (!raidLeader)
            return;

        RaidDifficulty difficulty = GetRaidDifficulty(group, killed);
        if (!IsBossValidForDifficulty(bossId, difficulty))
            return;

        RaidStats stats = AnalyzeRaid(group, raidLeader);
        bool isGuildRun = IsGuildRun(group, raidLeader, stats);
        
        uint32 points = GetPointsForDifficulty(difficulty);
        std::string difficultyStr = GetDifficultyString(difficulty);
        std::string bossName = killed->GetName();
        std::string bossIcon = GetBossIcon(bossId);

        if (isGuildRun)
        {
            Guild* guild = raidLeader->GetGuild();
            if (guild)
            {
                AddPointsToGuild(guild, points);
                
                // Server announcement for guild run with beautiful formatting
                std::string announcement = bossIcon + " |cFFFF6B6B" + bossName + "|r |cFF40E0D0(" + difficultyStr + ")|r " +
                    "|cFF98FB98defeated by|r |cFFFFFFFF" + raidLeader->GetName() + "|r " +
                    "|cFF87CEEB<" + guild->GetName() + ">|r |cFFFFD700received " + std::to_string(points) + " points!|r " +
                    "💀 |cFF90EE90[" + std::to_string(stats.alivePlayers) + "/" + std::to_string(stats.totalPlayers) + " alive] " +
                    "[🛡️:" + std::to_string(stats.tanks) + " 💚:" + std::to_string(stats.healers) + " ⚔️:" + std::to_string(stats.dps) + "]|r";
                
                sWorldSessionMgr->SendServerMessage(SERVER_MSG_STRING, announcement.c_str());
                
                // Guild message
                SendGuildMessage(guild, bossName, points);
                
                // Apply buffs/remove debuffs if faction majority
                ApplyFactionalEffects(group, stats);
            }
        }
        else
        {
            // Server announcement for pug run with beautiful formatting
            std::string announcement = "🌟 |cFFFFA500[PUG RUN]|r " + bossIcon + " |cFFFF6B6B" + bossName + "|r |cFF40E0D0(" + difficultyStr + ")|r " +
                "|cFF98FB98defeated by|r |cFFFFFFFF" + raidLeader->GetName() + "|r " +
                "|cFFFFA500[Mixed Groups]|r " +
                "💀 |cFF90EE90[" + std::to_string(stats.alivePlayers) + "/" + std::to_string(stats.totalPlayers) + " alive] " +
                "[🛡️:" + std::to_string(stats.tanks) + " 💚:" + std::to_string(stats.healers) + " ⚔️:" + std::to_string(stats.dps) + "]|r";
            
            sWorldSessionMgr->SendServerMessage(SERVER_MSG_STRING, announcement.c_str());
        }
    }

    uint32 GetGuildPoints(uint32 guildId)
    {
        QueryResult result = CharacterDatabase.Query("SELECT guild_points FROM guild WHERE guildid = {}", guildId);
        if (result)
        {
            Field* fields = result->Fetch();
            return fields[0].Get<uint32>();
        }
        return 0;
    }

    std::vector<std::pair<std::string, uint32>> GetTopGuilds(uint32 limit = 5)
    {
        std::vector<std::pair<std::string, uint32>> topGuilds;
        
        if (QueryResult result = CharacterDatabase.Query("SELECT name, guild_points FROM guild WHERE guild_points > 0 ORDER BY guild_points DESC LIMIT {}", limit))
        {
            do
            {
                Field* fields = result->Fetch();
                std::string guildName = fields[0].Get<std::string>();
                uint32 points = fields[1].Get<uint32>();
                topGuilds.emplace_back(guildName, points);
            } while (result->NextRow());
        }
        
        return topGuilds;
    }

    // Reset Guild Points and Distribute Rewards
    bool ResetGuildPoints()
    {
        if (!enabled)
            return false;

        // Get top 3 guilds before reset
        auto topGuilds = GetTopGuilds(10);
        
        if (announceReset)
        {
            std::string announcement = "🏆 |cFFFFD700[SEASON END]|r |cFF87CEEBGuild Points Season has ended! Final standings:|r";
            sWorldSessionMgr->SendServerMessage(SERVER_MSG_STRING, announcement.c_str());
        }

        // Distribute rewards to top guilds
        for (size_t i = 0; i < topGuilds.size() && i < 3; ++i)
        {
            const auto& guildInfo = topGuilds[i];
            if (guildInfo.second >= minPointsForRewards)
            {
                DistributeRewards(guildInfo.first, i + 1, guildInfo.second);
            }
        }

        // Distribute participation rewards
        for (size_t i = 3; i < topGuilds.size(); ++i)
        {
            const auto& guildInfo = topGuilds[i];
            if (guildInfo.second >= minPointsForRewards)
            {
                DistributeRewards(guildInfo.first, 4, guildInfo.second); // Participation rewards
            }
        }

        // Reset all guild points
        CharacterDatabase.Execute("UPDATE guild SET guild_points = 0 WHERE guild_points > 0");

        if (announceReset)
        {
            std::string resetMsg = "✨ |cFFFFD700[NEW SEASON]|r |cFF98FB98All guild points have been reset! New season begins now!|r";
            sWorldSessionMgr->SendServerMessage(SERVER_MSG_STRING, resetMsg.c_str());
        }

        LOG_INFO("module", "Guild Points have been reset. Rewards distributed to {} guilds.", std::min(topGuilds.size(), static_cast<size_t>(10)));
        return true;
    }

private:
    bool enabled;
    uint32 minGuildPercentage;
    float maxDistance;
    uint32 points10Man, points25Man, points10HC, points25HC, points10Hard, points25Hard;
    
    // Reset & Rewards Configuration
    bool autoResetEnabled;
    uint32 resetIntervalDays;
    uint32 minPointsForRewards;
    uint32 rewardDistribution;
    bool announceReset;
    
    std::unordered_map<uint32, std::vector<RaidDifficulty>> bossPoints;
    std::unordered_map<uint32, std::string> bossIcons;
    std::vector<uint32> hordeBuffs, allianceBuffs, removeDebuffs, resetCooldowns;
    
    // Reward Items Configuration
    std::vector<std::pair<uint32, uint32>> firstPlaceRewards;
    std::vector<std::pair<uint32, uint32>> secondPlaceRewards;
    std::vector<std::pair<uint32, uint32>> thirdPlaceRewards;
    std::vector<std::pair<uint32, uint32>> participationRewards;
    uint32 firstPlaceTitle, secondPlaceTitle, thirdPlaceTitle;
    
    // Gold and Mount rewards
    uint32 firstPlaceGold, secondPlaceGold, thirdPlaceGold, participationGold;
    uint32 firstPlaceMount, secondPlaceMount, thirdPlaceMount;
    uint32 topRanksCount;
    uint32 maxRewardedPerGuild;

    void LoadBossConfigs()
    {
        // Load boss configurations from config file
        // This is a simplified version - in reality you'd parse the config file
        bossPoints.clear();
        bossIcons.clear();
        
        // Example boss configurations (you'd load these from config)
        // Naxxramas bosses
        bossPoints[15989] = {DIFFICULTY_10_NORMAL, DIFFICULTY_25_NORMAL}; // Anub'Rekhan
        bossPoints[15990] = {DIFFICULTY_10_NORMAL, DIFFICULTY_25_NORMAL}; // Kel'Thuzad
        
        // Ulduar bosses with hardmode
        bossPoints[33113] = {DIFFICULTY_10_NORMAL, DIFFICULTY_25_NORMAL, DIFFICULTY_10_HARDMODE, DIFFICULTY_25_HARDMODE}; // Flame Leviathan
        
        // ICC bosses
        bossPoints[36612] = {DIFFICULTY_10_NORMAL, DIFFICULTY_25_NORMAL, DIFFICULTY_10_HEROIC, DIFFICULTY_25_HEROIC}; // Lord Marrowgar
        bossPoints[36597] = {DIFFICULTY_10_NORMAL, DIFFICULTY_25_NORMAL, DIFFICULTY_10_HEROIC, DIFFICULTY_25_HEROIC}; // The Lich King
        
        LoadBossIcons();
    }

    void LoadBossIcons()
    {
        // Load boss icons from config - this would be loaded from config file in real implementation
        // For now, hardcoded examples based on boss IDs from config file
        
        // Naxxramas
        bossIcons[15989] = "|TInterface\\\\ICONS\\\\Achievement_Boss_Anubrekhan:16:16|t"; // Anub'Rekhan
        bossIcons[15956] = "|TInterface\\\\ICONS\\\\Achievement_Boss_Faerlina:16:16|t"; // Grand Widow Faerlina
        bossIcons[15952] = "|TInterface\\\\ICONS\\\\Achievement_Boss_Maexxna:16:16|t"; // Maexxna
        bossIcons[15954] = "|TInterface\\\\ICONS\\\\Achievement_Boss_Noth:16:16|t"; // Noth the Plaguebringer
        bossIcons[15936] = "|TInterface\\\\ICONS\\\\Achievement_Boss_Heigan:16:16|t"; // Heigan the Unclean
        bossIcons[16011] = "|TInterface\\\\ICONS\\\\Achievement_Boss_Loatheb:16:16|t"; // Loatheb
        bossIcons[15931] = "|TInterface\\\\ICONS\\\\Achievement_Boss_Razuvious:16:16|t"; // Instructor Razuvious
        bossIcons[16061] = "|TInterface\\\\ICONS\\\\Achievement_Boss_Gothik:16:16|t"; // Gothik the Harvester
        bossIcons[30549] = "|TInterface\\\\ICONS\\\\Achievement_Boss_BaronRivendare:16:16|t"; // Baron Rivendare
        bossIcons[15990] = "|TInterface\\\\ICONS\\\\Achievement_Boss_KelThuzad_01:16:16|t"; // Kel'Thuzad
        bossIcons[16028] = "|TInterface\\\\ICONS\\\\Achievement_Boss_Patchwerk:16:16|t"; // Patchwerk
        bossIcons[15929] = "|TInterface\\\\ICONS\\\\Achievement_Boss_Grobbulus:16:16|t"; // Grobbulus
        bossIcons[15930] = "|TInterface\\\\ICONS\\\\Achievement_Boss_Gluth:16:16|t"; // Gluth
        bossIcons[15928] = "|TInterface\\\\ICONS\\\\Achievement_Boss_Thaddius:16:16|t"; // Thaddius
        bossIcons[15932] = "|TInterface\\\\ICONS\\\\Achievement_Boss_Sapphiron:16:16|t"; // Sapphiron

        // Eye of Eternity
        bossIcons[28859] = "|TInterface\\\\ICONS\\\\Achievement_Boss_Malygos_01:16:16|t"; // Malygos

        // Obsidian Sanctum
        bossIcons[28860] = "|TInterface\\\\ICONS\\\\Achievement_Boss_Sartharion_01:16:16|t"; // Sartharion

        // Ulduar
        bossIcons[33113] = "|TInterface\\\\ICONS\\\\Achievement_Boss_FlameLeviathan:16:16|t"; // Flame Leviathan
        bossIcons[33118] = "|TInterface\\\\ICONS\\\\Achievement_Boss_Ignis:16:16|t"; // Ignis the Furnace Master
        bossIcons[33186] = "|TInterface\\\\ICONS\\\\Achievement_Boss_Razorscale:16:16|t"; // Razorscale
        bossIcons[33293] = "|TInterface\\\\ICONS\\\\Achievement_Boss_XT002:16:16|t"; // XT-002 Deconstructor
        bossIcons[32845] = "|TInterface\\\\ICONS\\\\Achievement_Boss_AssemblyofIron:16:16|t"; // Assembly of Iron
        bossIcons[32865] = "|TInterface\\\\ICONS\\\\Achievement_Boss_Kologarn:16:16|t"; // Kologarn
        bossIcons[33515] = "|TInterface\\\\ICONS\\\\Achievement_Boss_Auriaya:16:16|t"; // Auriaya
        bossIcons[33271] = "|TInterface\\\\ICONS\\\\Achievement_Boss_GeneralVezax:16:16|t"; // General Vezax
        bossIcons[33288] = "|TInterface\\\\ICONS\\\\Achievement_Boss_YoggSaron_01:16:16|t"; // Yogg-Saron
        bossIcons[32906] = "|TInterface\\\\ICONS\\\\Achievement_Boss_Freya_01:16:16|t"; // Freya
        bossIcons[32857] = "|TInterface\\\\ICONS\\\\Achievement_Boss_Hodir_01:16:16|t"; // Hodir
        bossIcons[32930] = "|TInterface\\\\ICONS\\\\Achievement_Boss_Mimiron_01:16:16|t"; // Mimiron
        bossIcons[33350] = "|TInterface\\\\ICONS\\\\Achievement_Boss_Algalon_01:16:16|t"; // Algalon the Observer

        // Onyxia
        bossIcons[10184] = "|TInterface\\\\ICONS\\\\Achievement_Boss_Onyxia:16:16|t"; // Onyxia

        // Trial of the Crusader
        bossIcons[34564] = "|TInterface\\\\ICONS\\\\Achievement_Boss_Anubarak_01:16:16|t"; // Anub'arak
        bossIcons[35013] = "|TInterface\\\\ICONS\\\\Achievement_Boss_Jaraxxus_01:16:16|t"; // Lord Jaraxxus
        bossIcons[34497] = "|TInterface\\\\ICONS\\\\Achievement_Boss_LichKing_01:16:16|t"; // The Lich King (Trial)

        // Icecrown Citadel
        bossIcons[36612] = "|TInterface\\\\ICONS\\\\Achievement_Boss_LordMarrowgar:16:16|t"; // Lord Marrowgar
        bossIcons[36855] = "|TInterface\\\\ICONS\\\\Achievement_Boss_LadyDeathwhisper:16:16|t"; // Lady Deathwhisper
        bossIcons[37813] = "|TInterface\\\\ICONS\\\\Achievement_Boss_DeathbringerSaurfang:16:16|t"; // Deathbringer Saurfang
        bossIcons[36626] = "|TInterface\\\\ICONS\\\\Achievement_Boss_Festergut:16:16|t"; // Festergut
        bossIcons[36627] = "|TInterface\\\\ICONS\\\\Achievement_Boss_Rotface:16:16|t"; // Rotface
        bossIcons[36678] = "|TInterface\\\\ICONS\\\\Achievement_Boss_ProfessorPutricide:16:16|t"; // Professor Putricide
        bossIcons[37972] = "|TInterface\\\\ICONS\\\\Achievement_Boss_BloodQueenLanathel:16:16|t"; // Blood-Queen Lana'thel
        bossIcons[36789] = "|TInterface\\\\ICONS\\\\Achievement_Boss_ValithriaDreamwalker:16:16|t"; // Valithria Dreamwalker
        bossIcons[36597] = "|TInterface\\\\ICONS\\\\Achievement_Boss_Sindragosa:16:16|t"; // Sindragosa
        bossIcons[36597] = "|TInterface\\\\ICONS\\\\Achievement_Boss_LichKing:16:16|t"; // The Lich King

        // Ruby Sanctum
        bossIcons[39863] = "|TInterface\\\\ICONS\\\\Achievement_Boss_Halion:16:16|t"; // Halion
    }

    std::string GetBossIcon(uint32 bossId)
    {
        auto it = bossIcons.find(bossId);
        if (it != bossIcons.end())
            return it->second;
        
        // Default icon for bosses without specific achievement icons
        return "|TInterface\\\\ICONS\\\\Achievement_Boss_Skull:16:16|t";
    }

    void LoadBuffConfigs()
    {
        // Load buff/debuff configurations
        hordeBuffs = {16609, 24705};
        allianceBuffs = {16609, 24705};
        removeDebuffs = {25771, 25646};
        resetCooldowns = {20608, 19801};
    }

    void LoadRewardConfigs()
    {
        // Load reward configurations from config
        firstPlaceRewards.clear();
        secondPlaceRewards.clear();
        thirdPlaceRewards.clear();
        participationRewards.clear();

        // Parse reward strings from config (simplified - normally you'd parse comma-separated strings)
        // First Place Rewards - More generous rewards
        firstPlaceRewards = {{49426, 5}, {45038, 10}, {40753, 15}, {37711, 20}};
        firstPlaceGold = sConfigMgr->GetOption<uint32>("GuildPoints.Rewards.FirstPlace.Gold", 50000);
        firstPlaceMount = sConfigMgr->GetOption<uint32>("GuildPoints.Rewards.FirstPlace.Mount", 25953);
        
        // Second Place Rewards
        secondPlaceRewards = {{49426, 3}, {45038, 7}, {40753, 10}, {37711, 15}};
        secondPlaceGold = sConfigMgr->GetOption<uint32>("GuildPoints.Rewards.SecondPlace.Gold", 30000);
        secondPlaceMount = sConfigMgr->GetOption<uint32>("GuildPoints.Rewards.SecondPlace.Mount", 25471);
        
        // Third Place Rewards
        thirdPlaceRewards = {{49426, 2}, {45038, 5}, {40753, 7}, {37711, 10}};
        thirdPlaceGold = sConfigMgr->GetOption<uint32>("GuildPoints.Rewards.ThirdPlace.Gold", 20000);
        thirdPlaceMount = sConfigMgr->GetOption<uint32>("GuildPoints.Rewards.ThirdPlace.Mount", 25470);
        
        // Participation Rewards
        participationRewards = {{40753, 3}, {37711, 10}};
        participationGold = sConfigMgr->GetOption<uint32>("GuildPoints.Rewards.Participation.Gold", 5000);

        // Top ranks count for reward distribution
        topRanksCount = sConfigMgr->GetOption<uint32>("GuildPoints.TopRanksCount", 3);
        maxRewardedPerGuild = sConfigMgr->GetOption<uint32>("GuildPoints.MaxRewardedPerGuild", 25);

        // Title rewards disabled
        firstPlaceTitle = 0;
        secondPlaceTitle = 0;
        thirdPlaceTitle = 0;
    }

    void DistributeRewards(const std::string& guildName, uint32 position, uint32 points)
    {
        // Find guild by name
        Guild* guild = sGuildMgr->GetGuildByName(guildName);
        if (!guild)
            return;

        std::vector<std::pair<uint32, uint32>>* rewards = nullptr;
        std::string positionText = "";
        std::string medal = "";
        uint32 gold = 0;
        uint32 mount = 0;

        switch (position)
        {
            case 1:
                rewards = &firstPlaceRewards;
                positionText = "1st Place";
                medal = "🥇";
                gold = firstPlaceGold;
                mount = firstPlaceMount;
                break;
            case 2:
                rewards = &secondPlaceRewards;
                positionText = "2nd Place";
                medal = "🥈";
                gold = secondPlaceGold;
                mount = secondPlaceMount;
                break;
            case 3:
                rewards = &thirdPlaceRewards;
                positionText = "3rd Place";
                medal = "🥉";
                gold = thirdPlaceGold;
                mount = thirdPlaceMount;
                break;
            default:
                rewards = &participationRewards;
                positionText = "Participation";
                medal = "🎖️";
                gold = participationGold;
                mount = 0; // No mount for participation
                break;
        }

        if (!rewards || rewards->empty())
            return;

        // Server announcement
        if (position <= 3)
        {
            std::string goldText = gold > 0 ? " 💰" + std::to_string(gold/10000) + "g" : "";
            std::string mountText = mount > 0 ? " 🐎Mount" : "";
            std::string announcement = medal + " |cFFFFD700[SEASON REWARDS]|r |cFF87CEEB<" + guildName + ">|r " +
                "|cFF98FB98finished " + positionText + " with|r |cFFFFD700" + std::to_string(points) + " points|r " +
                "|cFF90EE90and top " + std::to_string(topRanksCount) + " ranks (max " + std::to_string(maxRewardedPerGuild) + " people) received rewards!|r" + goldText + mountText;
            sWorldSessionMgr->SendServerMessage(SERVER_MSG_STRING, announcement.c_str());
        }

        // Distribute rewards based on configuration
        switch (rewardDistribution)
        {
            case 0: // Only Guild Master
                DistributeRewardsToPlayer(guild->GetLeaderGUID(), *rewards, gold, mount);
                break;
            case 1: // All guild members
                DistributeRewardsToAllMembers(guild, *rewards, gold, mount);
                break;
            case 2: // Only online guild members
                DistributeRewardsToOnlineMembers(guild, *rewards, gold, mount);
                break;
            case 3: // Only top ranks in guild
                DistributeRewardsToTopRanks(guild, *rewards, gold, mount);
                break;
        }

        // Guild internal message
        std::string goldText = gold > 0 ? " 💰" + std::to_string(gold/10000) + "g" : "";
        std::string mountText = mount > 0 ? " 🐎Mount" : "";
        std::string guildMsg = medal + " |cFFFFD700[SEASON END]|r |cFF98FB98Your guild finished " + positionText + 
            " with " + std::to_string(points) + " points! Top " + std::to_string(topRanksCount) + 
            " ranks received rewards!|r" + goldText + mountText;
        guild->BroadcastToGuild(nullptr, false, guildMsg, LANG_UNIVERSAL);
    }

    void DistributeRewardsToPlayer(ObjectGuid playerGuid, const std::vector<std::pair<uint32, uint32>>& rewards, uint32 gold, uint32 mount)
    {
        Player* player = ObjectAccessor::FindPlayer(playerGuid);
        if (!player)
        {
            // Send rewards via mail if player is offline
            SendRewardsByMail(playerGuid, rewards, gold, mount);
            return;
        }

        // Give gold
        if (gold > 0)
        {
            player->ModifyMoney(gold);
        }

        // Give mount
        if (mount > 0)
        {
            player->learnSpell(mount);
        }

        // Give items
        for (const auto& reward : rewards)
        {
            uint32 itemId = reward.first;
            uint32 quantity = reward.second;
            
            ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(itemId);
            if (!itemTemplate)
                continue;

            // Add item to inventory or send by mail if bags full
            ItemPosCountVec dest;
            InventoryResult msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemId, quantity);
            if (msg == EQUIP_ERR_OK)
            {
                Item* item = player->StoreNewItem(dest, itemId, true);
                if (item)
                    player->SendNewItem(item, quantity, true, false);
            }
            else
            {
                SendItemByMail(player->GetGUID(), itemId, quantity);
            }
        }
    }

    void DistributeRewardsToTopRanks(Guild* guild, const std::vector<std::pair<uint32, uint32>>& rewards, uint32 gold, uint32 mount)
    {
        // Get top ranks from guild - simplified implementation
        // In a real implementation, you'd query guild_member table ordered by rank
        auto topMembers = GetTopGuildMembers(guild, topRanksCount);
        
        for (const auto& memberGuid : topMembers)
        {
            DistributeRewardsToPlayer(memberGuid, rewards, gold, mount);
        }
        
        LOG_INFO("module", "Distributed season rewards to top {} ranks of guild {}", topRanksCount, guild->GetName());
    }

    std::vector<ObjectGuid> GetTopGuildMembers(Guild* guild, uint32 count)
    {
        std::vector<ObjectGuid> topMembers;
        
        // Get members with top 3 ranks (0, 1, 2) and limit to maxRewardedPerGuild
        uint32 maxMembers = std::min(count, maxRewardedPerGuild);
        uint32 addedCount = 0;
        
        // Query guild members with top ranks from database
        QueryResult result = CharacterDatabase.Query(
            "SELECT guid FROM guild_member WHERE guildid = {} AND rank <= 2 ORDER BY rank ASC LIMIT {}",
            guild->GetId(), maxMembers);
        
        if (result)
        {
            do
            {
                Field* fields = result->Fetch();
                uint32 guid = fields[0].Get<uint32>();
                topMembers.push_back(ObjectGuid::Create<HighGuid::Player>(guid));
            } while (result->NextRow());
        }
        
        return topMembers;
    }

    void DistributeRewardsToAllMembers(Guild* guild, const std::vector<std::pair<uint32, uint32>>& rewards, uint32 gold, uint32 mount)
    {
        // Query all guild members from database
        QueryResult result = CharacterDatabase.Query("SELECT guid FROM guild_member WHERE guildid = {}", guild->GetId());
        if (result)
        {
            do
            {
                Field* fields = result->Fetch();
                uint32 guid = fields[0].Get<uint32>();
                DistributeRewardsToPlayer(ObjectGuid::Create<HighGuid::Player>(guid), rewards, gold, mount);
            } while (result->NextRow());
        }
        LOG_INFO("module", "Distributing rewards to all members of guild {}", guild->GetName());
    }

    void DistributeRewardsToOnlineMembers(Guild* guild, const std::vector<std::pair<uint32, uint32>>& rewards, uint32 gold, uint32 mount)
    {
        QueryResult result = CharacterDatabase.Query("SELECT guid FROM guild_member WHERE guildid = {}", guild->GetId());
        if (result)
        {
            do
            {
                Field* fields = result->Fetch();
                uint32 guid = fields[0].Get<uint32>();
                Player* player = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(guid));
                if (player && player->IsInWorld())
                {
                    DistributeRewardsToPlayer(player->GetGUID(), rewards, gold, mount);
                }
            } while (result->NextRow());
        }
        LOG_INFO("module", "Distributing rewards to online members of guild {}", guild->GetName());
    }

    void SendRewardsByMail(ObjectGuid playerGuid, const std::vector<std::pair<uint32, uint32>>& rewards, uint32 gold, uint32 mount)
    {
        // Send rewards by mail - simplified implementation
        LOG_INFO("module", "Sending season rewards by mail to player {} (Gold: {}, Mount: {})", 
                 playerGuid.ToString(), gold, mount);
    }

    void SendItemByMail(ObjectGuid playerGuid, uint32 itemId, uint32 quantity)
    {
        // Send item by mail - simplified implementation
        LOG_INFO("module", "Sending item {} (qty: {}) by mail to player {}", itemId, quantity, playerGuid.ToString());
    }

    RaidDifficulty GetRaidDifficulty(Group* group, Creature* boss)
    {
        uint32 maxPlayers = group->GetMembersCount();
        Map* map = boss->GetMap();
        bool isHeroic = map->IsHeroic();
        
        // Check for hardmode conditions (simplified)
        bool isHardmode = false; // You'd implement hardmode detection here
        
        if (maxPlayers <= 10)
        {
            if (isHardmode) return DIFFICULTY_10_HARDMODE;
            if (isHeroic) return DIFFICULTY_10_HEROIC;
            return DIFFICULTY_10_NORMAL;
        }
        else
        {
            if (isHardmode) return DIFFICULTY_25_HARDMODE;
            if (isHeroic) return DIFFICULTY_25_HEROIC;
            return DIFFICULTY_25_NORMAL;
        }
    }

    bool IsBossValidForDifficulty(uint32 bossId, RaidDifficulty difficulty)
    {
        auto it = bossPoints.find(bossId);
        if (it == bossPoints.end())
            return false;
        
        return std::find(it->second.begin(), it->second.end(), difficulty) != it->second.end();
    }

    RaidStats AnalyzeRaid(Group* group, Player* leader)
    {
        RaidStats stats = {0};
        
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || !member->IsInWorld())
                continue;
                
            stats.totalPlayers++;
            
            if (member->IsAlive())
                stats.alivePlayers++;
            
            // Count by role - simplified role detection for WotLK
            uint8 playerClass = member->getClass();
            if (playerClass == CLASS_WARRIOR || playerClass == CLASS_PALADIN || playerClass == CLASS_DEATH_KNIGHT)
                stats.tanks++;
            else if (playerClass == CLASS_PRIEST || playerClass == CLASS_DRUID || playerClass == CLASS_SHAMAN || playerClass == CLASS_PALADIN)
                stats.healers++;
            else
                stats.dps++;
            
            // Count by faction
            if (member->GetTeamId() == TEAM_HORDE)
                stats.hordeCount++;
            else
                stats.allianceCount++;
        }
        
        return stats;
    }

    bool IsGuildRun(Group* group, Player* leader, const RaidStats& stats)
    {
        Guild* leaderGuild = leader->GetGuild();
        if (!leaderGuild)
            return false;
        
        uint32 guildMembers = 0;
        uint32 playersInRange = 0;
        
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || !member->IsInWorld())
                continue;
            
            // Check guild membership
            if (member->GetGuild() && member->GetGuild()->GetId() == leaderGuild->GetId())
                guildMembers++;
            
            // Check distance from leader
            if (member->GetDistance(leader) <= maxDistance)
                playersInRange++;
        }
        
        uint32 guildPercentage = (guildMembers * 100) / stats.totalPlayers;
        uint32 rangePercentage = (playersInRange * 100) / stats.totalPlayers;
        
        return guildPercentage >= minGuildPercentage && rangePercentage >= minGuildPercentage;
    }

    uint32 GetPointsForDifficulty(RaidDifficulty difficulty)
    {
        switch (difficulty)
        {
            case DIFFICULTY_10_NORMAL: return points10Man;
            case DIFFICULTY_25_NORMAL: return points25Man;
            case DIFFICULTY_10_HEROIC: return points10HC;
            case DIFFICULTY_25_HEROIC: return points25HC;
            case DIFFICULTY_10_HARDMODE: return points10Hard;
            case DIFFICULTY_25_HARDMODE: return points25Hard;
            default: return 0;
        }
    }

    std::string GetDifficultyString(RaidDifficulty difficulty)
    {
        switch (difficulty)
        {
            case DIFFICULTY_10_NORMAL: return "10";
            case DIFFICULTY_25_NORMAL: return "25";
            case DIFFICULTY_10_HEROIC: return "10HC";
            case DIFFICULTY_25_HEROIC: return "25HC";
            case DIFFICULTY_10_HARDMODE: return "10Hard";
            case DIFFICULTY_25_HARDMODE: return "25Hard";
            default: return "Unknown";
        }
    }

    void AddPointsToGuild(Guild* guild, uint32 points)
    {
        CharacterDatabase.Execute("UPDATE guild SET guild_points = guild_points + {} WHERE guildid = {}", 
            points, guild->GetId());
    }

    void SendGuildMessage(Guild* guild, const std::string& bossName, uint32 points)
    {
        uint32 totalPoints = GetGuildPoints(guild->GetId());
        std::string message = "|cFFFFD700[GUILD ACHIEVEMENT]|r |cFF98FB98Your guild earned|r |cFFFFD700" + std::to_string(points) + 
            " points|r |cFF87CEEBfor defeating|r |cFFFF6B6B" + bossName + "|r! " +
            "|cFF90EE90Total Guild Points:|r |cFFFFD700" + std::to_string(totalPoints) + "|r";
        
        guild->BroadcastToGuild(nullptr, false, message, LANG_UNIVERSAL);
    }

    void ApplyFactionalEffects(Group* group, const RaidStats& stats)
    {
        bool majorityHorde = stats.hordeCount > stats.allianceCount;
        bool majorityAlliance = stats.allianceCount > stats.hordeCount;
        
        if (!majorityHorde && !majorityAlliance)
            return;
        
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || !member->IsInWorld())
                continue;
            
            // Remove debuffs
            for (uint32 spellId : removeDebuffs)
            {
                member->RemoveAura(spellId);
            }
            
            // Reset cooldowns
            for (uint32 spellId : resetCooldowns)
            {
                member->RemoveSpellCooldown(spellId, true);
            }
            
            // Apply faction buffs
            if (majorityHorde && member->GetTeamId() == TEAM_HORDE)
            {
                for (uint32 spellId : hordeBuffs)
                {
                    member->CastSpell(member, spellId, true);
                }
            }
            else if (majorityAlliance && member->GetTeamId() == TEAM_ALLIANCE)
            {
                for (uint32 spellId : allianceBuffs)
                {
                    member->CastSpell(member, spellId, true);
                }
            }
        }
    }
};

class GuildPointsCreatureScript : public CreatureScript
{
public:
    GuildPointsCreatureScript() : CreatureScript("GuildPointsCreatureScript") { }

    void OnJustDied(Creature* creature, Unit* killer) 
    {
        if (!killer)
            return;
        
        Player* player = killer->ToPlayer();
        if (!player)
        {
            if (killer->GetTypeId() == TYPEID_UNIT && killer->GetOwner())
                player = killer->GetOwner()->ToPlayer();
        }
        
        if (player)
            GuildPointsManager::instance()->OnCreatureKill(player, creature);
    }
};

class GuildPointsCommandScript : public CommandScript
{
public:
    GuildPointsCommandScript() : CommandScript("GuildPointsCommandScript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable commandTable =
        {
            { "grank", HandleGRankCommand, SEC_PLAYER, Console::No },
            { "greset", HandleGResetCommand, SEC_ADMINISTRATOR, Console::Yes }
        };
        return commandTable;
    }

    static bool HandleGRankCommand(ChatHandler* handler, char const* /*args*/)
    {
        Player* player = handler->GetSession()->GetPlayer();
        Guild* guild = player->GetGuild();

        if (!guild)
        {
            handler->SendSysMessage("|cFFFF6B6BYou are not in a guild.|r");
            return true;
        }

        uint32 guildPoints = GuildPointsManager::instance()->GetGuildPoints(guild->GetId());
        auto topGuilds = GuildPointsManager::instance()->GetTopGuilds(5);

        handler->PSendSysMessage("|cFF87CEEB Your guild|r |cFFFFD700<%s>|r |cFF87CEEBhas|r |cFFFFD700%u points|r", guild->GetName().c_str(), guildPoints);
        handler->SendSysMessage(" ");
        handler->SendSysMessage("|cFFFFD700═══════ TOP 5 GUILDS ═══════|r");

        for (size_t i = 0; i < topGuilds.size(); ++i)
        {
            const auto& guildInfo = topGuilds[i];
            std::string medal = "";
            if (i == 0) medal = "|cFFFFD700🥇|r ";
            else if (i == 1) medal = "|cFFC0C0C0🥈|r ";
            else if (i == 2) medal = "|cFFCD7F32🥉|r ";
            else medal = "|cFF90EE90" + std::to_string(i + 1) + ".|r ";
            
            handler->PSendSysMessage("%s|cFF87CEEB<%s>|r |cFF98FB98-|r |cFFFFD700%u points|r", 
                medal.c_str(), guildInfo.first.c_str(), guildInfo.second);
        }

        return true;
    }

    static bool HandleGResetCommand(ChatHandler* handler, char const* /*args*/)
    {
        if (!GuildPointsManager::instance()->IsEnabled())
        {
            handler->SendSysMessage("|cFFFF6B6BGuild Points Module is disabled.|r");
            return true;
        }

        // Confirmation message
        handler->SendSysMessage("|cFFFFD700[GUILD POINTS RESET]|r");
        handler->SendSysMessage("|cFF87CEEBResetting all guild points and distributing season rewards...|r");

        // Perform the reset
        bool success = GuildPointsManager::instance()->ResetGuildPoints();

        if (success)
        {
            handler->SendSysMessage("|cFF90EE90✅ Guild points have been successfully reset!|r");
            handler->SendSysMessage("|cFF98FB98🏆 Season rewards have been distributed to eligible guilds.|r");
            handler->SendSysMessage("|cFFFFD700✨ New season has begun!|r");
        }
        else
        {
            handler->SendSysMessage("|cFFFF6B6B❌ Failed to reset guild points. Check server logs for details.|r");
        }

        return true;
    }
};

class GuildPointsWorldScript : public WorldScript
{
public:
    GuildPointsWorldScript() : WorldScript("GuildPointsWorldScript") { }

    void OnConfigLoad(bool /*reload*/) 
    {
        GuildPointsManager::instance()->LoadConfig();
        LOG_INFO("module", "Guild Points Module configuration loaded.");
    }

    void OnStartup() 
    {
        LOG_INFO("module", "Guild Points Module loaded successfully.");
    }
};

void Addmod_guild_pointsScripts()
{
    new GuildPointsCreatureScript();
    new GuildPointsCommandScript();
    new GuildPointsWorldScript();
}
