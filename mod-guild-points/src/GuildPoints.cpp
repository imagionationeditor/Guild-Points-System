/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

#include "ScriptMgr.h"
#include "Player.h"
#include "Config.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Group.h"
#include "Map.h"
#include "Chat.h"
#include "World.h"
#include "InstanceScript.h"
#include "DatabaseEnv.h"
#include "ChatCommand.h"
#include "GameTime.h"
#include "Creature.h"
#include "fmt/format.h"
#include "GuildPoints.h"

using namespace Acore::ChatCommands;

bool GuildPoints::IsValidRaidMap(uint32 mapId)
{
    switch (mapId)
    {
        case 533:  // Naxxramas
        case 615:  // Obsidian Sanctum
        case 616:  // Eye of Eternity
        case 603:  // Ulduar
        case 649:  // Trial of the Crusader
        case 249:  // Onyxia's Lair
        case 631:  // Icecrown Citadel
        case 724:  // Ruby Sanctum
            return true;
        default:
            return false;
    }
}

void GuildPoints::OnCreatureKill(Player* player, Creature* killed)
{
    if (!player || !killed)
        return;

    // Check if the killed creature is a boss
    CreatureTemplate const* creatureTemplate = killed->GetCreatureTemplate();
    if (!creatureTemplate || creatureTemplate->rank != CREATURE_ELITE_WORLDBOSS)
        return;

    // Get the raid group
    Group* group = player->GetGroup();
    if (!group || !group->isRaidGroup())
        return;

    // Check if it's a valid raid map
    Map* map = killed->GetMap();
    if (!map || !IsValidRaidMap(map->GetId()))
        return;

    // Get difficulty
    uint8 difficulty = map->GetDifficulty();
    std::string difficultyStr;
    switch(difficulty)
    {
        case RAID_DIFFICULTY_10MAN_NORMAL:
            difficultyStr = "10 Normal";
            break;
        case RAID_DIFFICULTY_25MAN_NORMAL:
            difficultyStr = "25 Normal";
            break;
        case RAID_DIFFICULTY_10MAN_HEROIC:
            difficultyStr = "10 Heroic";
            break;
        case RAID_DIFFICULTY_25MAN_HEROIC:
            difficultyStr = "25 Heroic";
            break;
        default:
            return;
    }

    // Check if it's a guild run
    Guild* majorityGuild = nullptr;
    float guildMemberPercentage = 0.0f;
    CheckGuildRun(group, majorityGuild, guildMemberPercentage);

    Player* leader = ObjectAccessor::FindPlayer(group->GetLeaderGUID());
    std::string leaderName = leader ? leader->GetName().c_str() : "Unknown";
    std::string bossName = killed->GetName();

    if (majorityGuild && guildMemberPercentage >= ConfigMgr::instance()->GetOption<float>("GuildPoints.MinGuildMemberPercentage", 80.0f))
    {
        // It's a guild run
        uint32 points = GetPointsForBoss(bossName, difficulty);
        if (points > 0)
        {
            // Add points to guild
            AddGuildPoints(majorityGuild, points);

            // Announce to world
            std::string announce = "|TInterface\\Icons\\Achievement_Boss_" + std::string(bossName) + ":25:25|t ";
            announce += "|cFF00FF00[Guild Run]|r |cFFFFFF00" + std::string(bossName) + "|r ";
            announce += "(" + difficultyStr + ") |cFF00CCFFdefeated|r. ";
            announce += "|cFFFF9900Raid Leader:|r " + leaderName + ", ";
            announce += "|cFF00FF00Guild:|r " + majorityGuild->GetName();
            announce += " |cFFFF0000granted " + std::to_string(points) + " points!|r";
            sWorld->SendServerMessage(SERVER_MSG_STRING, announce.c_str());

            // Announce to guild
            PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_GUILD_POINTS);
            stmt->SetData(0, MYSQL_TYPE_LONG, majorityGuild->GetId());

            if (PreparedQueryResult result = CharacterDatabase.Query(stmt))
            {
                uint32 totalPoints = (*result)[0].Get<uint32>();

                std::string guildAnnounce = "|TInterface\\Icons\\INV_Misc_Coin_01:25:25|t ";
                guildAnnounce += "|cFF00FF00Guild has earned|r |cFFFFFF00" + std::to_string(points) + " points|r ";
                guildAnnounce += "|cFF00FF00from|r |cFFFFFF00" + bossName + "|r. ";
                guildAnnounce += "|cFFFF9900Total guild points:|r |cFFFF0000" + std::to_string(totalPoints) + "|r";
                majorityGuild->BroadcastToGuild(0, false, guildAnnounce, LANG_UNIVERSAL);
            }

            // Handle spells and debuffs
            HandlePostKillEffects(group);
        }
    }
    else
    {
        // It's a PUG run
        std::string announce = "|TInterface\\Icons\\Achievement_Boss_" + std::string(bossName) + ":25:25|t ";
        announce += "|cFFFF0000[PUG Run]|r |cFFFFFF00" + std::string(bossName) + "|r ";
        announce += "(" + difficultyStr + ") |cFF00CCFFdefeated|r. ";
        announce += "|cFFFF9900Raid Leader:|r " + leaderName + ". ";
        announce += "|cFFFF0000This raid is PUG!|r";
        sWorld->SendServerMessage(SERVER_MSG_STRING, announce.c_str());
    }
}

void GuildPoints::AddGuildPoints(Guild* guild, uint32 points)
{
    if (!guild)
        return;

    
    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_GUILD_POINTS);
    stmt->SetData(0, MYSQL_TYPE_LONG, guild->GetId());
    stmt->SetData(1, MYSQL_TYPE_STRING, guild->GetName().c_str());
    stmt->SetData(2, MYSQL_TYPE_LONG, points);
    stmt->SetData(3, MYSQL_TYPE_LONG, points);
    CharacterDatabase.Execute(stmt);
}

void GuildPoints::HandlePostKillEffects(Group* group)
{
    if (!group)
        return;

    // Check if spell system is enabled
    if (!ConfigMgr::instance()->GetOption<bool>("GuildPoints.SpellSystem.Enabled", true))
        return;

    std::string debuffIds = ConfigMgr::instance()->GetOption<std::string>("GuildPoints.Debuffs", "");
    std::string spellIds = ConfigMgr::instance()->GetOption<std::string>("GuildPoints.Spells", "");

    if (debuffIds.empty() && spellIds.empty())
        return;

    Player* leader = ObjectAccessor::FindPlayer(group->GetLeaderGUID());
    if (!leader)
        return;

    Map* map = leader->GetMap();
    if (!map || !map->IsDungeon())
        return;

    float maxAllowedDistance = ConfigMgr::instance()->GetOption<float>("GuildPoints.MaxDistance", 500.0f);

    // Handle debuffs
    if (!debuffIds.empty())
    {
        std::istringstream debuffStream(debuffIds);
        uint32 debuffId;
        while (debuffStream >> debuffId)
        {
            for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
            {
                Player* member = itr->GetSource();
                if (!member || !member->IsInWorld() || member->GetMap() != map)
                    continue;

                if (member != leader && !member->IsWithinDist3d(leader, maxAllowedDistance))
                    continue;

                member->RemoveAura(debuffId);
            }
        }
    }

    // Handle spells
    if (!spellIds.empty())
    {
        std::istringstream spellStream(spellIds);
        uint32 spellId;
        while (spellStream >> spellId)
        {
            for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
            {
                Player* member = itr->GetSource();
                if (!member || !member->IsInWorld() || member->GetMap() != map)
                    continue;

                if (member != leader && !member->IsWithinDist3d(leader, maxAllowedDistance))
                    continue;

                member->CastSpell(member, spellId, true);
            }
        }
    }
}

ChatCommandTable GuildPointsCommand::GetCommands() const
{
    static ChatCommandTable guildPointsCommandTable =
    {
        { "rank", HandleGuildRankCommand, SEC_PLAYER, Console::No }
    };

    return guildPointsCommandTable;
}

bool GuildPointsCommand::HandleGuildRankCommand(ChatHandler* handler, const char* args)
{
    if (!args)
        return false;

    Player* target;
    std::string targetName;
    if (!handler->extractPlayerTarget((char*)args, &target, nullptr, &targetName))
        return false;

    if (!target)
    {
        handler->PSendSysMessage("Player %s not found", targetName.c_str());
        handler->SetSentErrorMessage(true);
        return false;
    }

    Guild* guild = target->GetGuild();
    if (!guild)
    {
        handler->PSendSysMessage("%s is not in a guild", target->GetName().c_str());
        handler->SetSentErrorMessage(true);
        return false;
    }

    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_GUILD_POINTS);
    stmt->SetData(0, guild->GetId());

    if (PreparedQueryResult result = CharacterDatabase.Query(stmt))
    {
        uint32 points = (*result)[0].Get<uint32>();
        handler->PSendSysMessage("Guild %s has %u points", guild->GetName().c_str(), points);
        return true;
    }

    handler->PSendSysMessage("Guild %s has no points", guild->GetName().c_str());
    handler->SetSentErrorMessage(true);
    return false;
}

void AddGuildPointsScripts()
{
    DefineGuildPointsStatements();
    new GuildPoints();
    new GuildPointsCommand();
}
