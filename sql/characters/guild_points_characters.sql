-- Guild Points Module Database Setup
-- Author: mojispectre
-- Date: July 2025
-- Description: Adds guild_points column to the guild table for tracking guild raid achievements

-- Add guild_points column to guild table
ALTER TABLE `guild` 
ADD COLUMN `guild_points` INT(11) UNSIGNED NOT NULL DEFAULT 0 
COMMENT 'Guild points earned from raid boss kills';

-- Create index for performance on guild points queries
CREATE INDEX `idx_guild_points` ON `guild` (`guild_points` DESC);

-- Optional: Create a separate table for detailed guild point history (uncomment if needed)
/*
CREATE TABLE `guild_points_history` (
    `id` INT(11) UNSIGNED NOT NULL AUTO_INCREMENT,
    `guild_id` INT(11) UNSIGNED NOT NULL,
    `boss_id` INT(11) UNSIGNED NOT NULL,
    `boss_name` VARCHAR(100) NOT NULL,
    `difficulty` VARCHAR(20) NOT NULL,
    `points_earned` INT(11) UNSIGNED NOT NULL,
    `raid_leader` VARCHAR(50) NOT NULL,
    `raid_size` TINYINT(3) UNSIGNED NOT NULL,
    `alive_players` TINYINT(3) UNSIGNED NOT NULL,
    `kill_timestamp` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    KEY `idx_guild_id` (`guild_id`),
    KEY `idx_boss_id` (`boss_id`),
    KEY `idx_timestamp` (`kill_timestamp`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='Guild Points History Table';
*/
