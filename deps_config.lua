-- =============================================================================
-- ThetaGP Dependencies Configuration
--
-- This file defines all external dependencies required by the ThetaGP project.
-- Each dependency entry contains:
--   - name: Display name of the dependency
--   - type: Dependency type (currently only "git" is supported)
--   - url: Git repository URL
--   - branch: Branch or tag to checkout
--   - dest: Destination path relative to project root
--   - optional: If true, failure to fetch will not stop the build
-- =============================================================================

DEPENDENCIES = {
    {
        name = "STM32H7xx_HAL_Driver",
        type = "git",
        url = "https://github.com/STMicroelectronics/stm32h7xx_hal_driver.git",
        branch = "v1.10.2",
        dest = "lib/Drivers/STM32H7xx_HAL_Driver",
        optional = false,
    },
    {
        name = "CMSIS",
        type = "git",
        url = "https://github.com/STMicroelectronics/cmsis_core.git",
        branch = "master",
        dest = "lib/Drivers/CMSIS",
        optional = false,
    },
    {
        name = "GP2040-CE",
        type = "git_sparse",
        url = "https://github.com/OpenStickCommunity/GP2040-CE.git",
        branch = "main",
        dest = "GP2040-CE",
        sparse_paths = { "lib/nanopb", "proto" },
        optional = false,
    },
    {
        name = "tinyusb",
        type = "git",
        url = "https://github.com/hathach/tinyusb.git",
        branch = "master",
        dest = "lib/tinyusb",
        optional = false,
    }
}
