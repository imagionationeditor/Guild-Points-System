/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

#ifndef MOD_GUILD_POINTS_H
#define MOD_GUILD_POINTS_H

#include "ScriptMgr.h"
#include "Player.h"
#include "Config.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Group.h"
#include "GroupMgr.h"
#include "Map.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "ChatCommand.h"
#include "Creature.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "InstanceScript.h"
#include "WorldSessionMgr.h"

using namespace Acore::ChatCommands;

class GuildPoints : public PlayerScript
{
public:
    GuildPoints() : PlayerScript("GuildPoints") {}

private:
    void OnCreatureKill(Player* player, Creature* killed) override;
};

class GuildPointsCommand : public CommandScript
{
public:
    GuildPointsCommand() : CommandScript("GuildPointsCommand") { }

    ChatCommandTable GetCommands() const override;

    static bool HandleGprankCommand(ChatHandler* handler);
};

bool IsValidRaidMap(uint32 mapId);
void AddGuildPoints(Guild* guild, uint32 points);
void HandlePostKillEffects(Group* group);
bool HasGuildPoints(Player* player);
void CheckGuildRun(Group* group, Guild*& majorityGuild, float& percentage);
uint32 GetPointsForBoss(const std::string& bossName, uint8 difficulty);

void AddGuildPointsScripts();

#endif // MOD_GUILD_POINTS_H 