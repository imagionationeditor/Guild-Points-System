# Guild Points Module

**Author:** mojispectre | **Version:** 1.0 | **Date:** July 2025

## 📖 Description

Guild points system that rewards guilds for raid boss kills with server announcements and competitive ranking.

## ✨ Features

- **Guild Point Rewards**: Automatic points for boss kills (80% guild members required)
- **Server Announcements**: Beautiful messages with boss icons and role distribution 🛡️💚⚔️
- **Ranking System**: `.grank` command shows guild standings with 🥇🥈🥉 medals
- **Season Reset & Rewards**: GM command `.greset` to reset points and give rewards to top guilds
- **Faction Benefits**: Auto buffs/debuff removal for successful guild runs
- **PUG Detection**: Separate announcements for mixed groups

## 🎯 Supported Content

**All WotLK Raids**: Naxx, EoE, OS, Ulduar, Onyxia, ToC, ICC, RS  
**Difficulties**: 10/25 Normal, Heroic, Hardmode  
**Point Values**: 10-25 points based on difficulty

## 🛠️ Installation

1. **Database**: Execute `sql/characters/guild_points_characters.sql` on your **characters** database
2. **Files**: Copy module to `modules/mod-guild-points/`
3. **Config**: Copy `.conf.dist` to `.conf` and configure
4. **Build**: Recompile AzerothCore

## ⚙️ Configuration

```ini
GuildPoints.Enable = 1                         # Enable module
GuildPoints.MinGuildPercentage = 80            # Guild member % required
GuildPoints.MaxDistance = 300                  # Distance validation (yards)
GuildPoints.Points.25HC = 25                   # Point values per difficulty

# Season Reset & Rewards
GuildPoints.MinPointsForRewards = 100          # Minimum points for rewards
GuildPoints.RewardDistribution = 3             # 3=Top ranks only, 1=All members, 2=Online only
GuildPoints.TopRanksCount = 3                  # Number of top ranks to reward
GuildPoints.Rewards.FirstPlace.Gold = 50000    # Gold reward (in copper)
GuildPoints.Rewards.FirstPlace.Mount = 25953   # Mount spell ID
```

## 🎮 Commands

- **`.grank`** - View guild rankings and standings
- **`.greset`** - (GM only) Reset season and distribute rewards

## 📊 Examples

**Guild Victory:**
```
|TThe Lich King (25HC) defeated by Arthas <Immortal Legion> received 25 points! 💀 [23/25 alive] [🛡️:2 💚:5 ⚔️:18]
```

**Season End:**
```
🏆 [SEASON END] Guild Points Season has ended! Final standings:
🥇 [SEASON REWARDS] <Immortal Legion> finished 1st Place with 1250 points and top 3 ranks received rewards! 💰500g 🐎Mount
🥈 [SEASON REWARDS] <Death Knights> finished 2nd Place with 980 points and top 3 ranks received rewards! 💰300g 🐎Mount
✨ [NEW SEASON] All guild points have been reset! New season begins now!
```

**Guild Ranking:**
```
Your guild <Dragon Slayers> has 450 points

🥇 <Immortal Legion> - 1250 points
🥈 <Death Knights> - 980 points
🥉 <Shadow Legion> - 750 points
```

---

**Credits:** mojispectre | **Framework:** AzerothCore | **License:** GPL v3.0
