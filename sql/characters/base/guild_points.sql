-- Create guild points table
CREATE TABLE IF NOT EXISTS `guild_points` (
    `guild_id` INT(10) UNSIGNED NOT NULL,
    `guild_name` VARCHAR(255) NOT NULL,
    `points` INT(10) UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`guild_id`),
    KEY `idx_points` (`points`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Add prepared statement
PREPARE guild_points_check FROM 'SELECT 1 FROM guild_points WHERE guild_id = ? AND points > 0 LIMIT 1'; 