# Guild Points System

## Description

This module implements a guild points system for WoTLK (3.3.5a). Guild points are awarded to guilds when they defeat raid bosses, with different point values based on raid size (10/25) and difficulty (Normal/Heroic).

## Features

- Automatic point awarding for raid boss kills
- Different point values for different raid sizes and difficulties
- Configurable minimum guild member percentage for guild runs
- Support for all WoTLK raids (Naxxramas, Ulduar, Trial of the Crusader, Icecrown Citadel, etc.)
- Spell and debuff system after boss kills
- Distance checking to prevent exploits
- Chat commands to check guild points

## Installation

1. Clone this module into the `modules` directory of your AzerothCore source
2. Re-run cmake and rebuild AzerothCore
3. Execute the SQL file in `sql/characters/base/guild_points.sql` to create required tables

## Configuration

Copy `conf/GuildPoints.conf.dist` to your config folder (`etc/modules`) and rename it to `GuildPoints.conf`, then edit the settings as needed:

- Enable/disable the module
- Set minimum guild member percentage for guild runs
- Configure points for each boss
- Set up spell and debuff systems
- Adjust maximum allowed distance between players

## Commands

### `.grank`
Displays the guild points ranking.

- If the player is in a guild, it will show their guild's current rank and points.
- It will then display a leaderboard of the top 5 guilds on the server.
- Players not in a guild can also use this command to see the top 5 leaderboard.

**Example output for a guild member:**
```
--- Guild Points Ranking ---
Your Guild: <Your Guild Name> | Rank: #3 | Points: 450
--------------------------
Top 5 Guilds:
#1: <Guild One> - 780 points
#2: <Guild Two> - 620 points
#3: <Your Guild Name> - 450 points
#4: <Guild Four> - 310 points
#5: <Guild Five> - 200 points
```

## Credits

- Original author: Mojispectre(imagionationeditor)
- AzerothCore: [https://github.com/azerothcore/azerothcore-wotlk](https://github.com/azerothcore/azerothcore-wotlk)
- License: AGPL 3.0 