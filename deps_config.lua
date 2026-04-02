-- =============================================================================
-- ThetaGP Dependencies Configuration
--
-- This file defines all external dependencies required by the ThetaGP project.
-- Each dependency entry contains:
--   - name: Display name of the dependency
--   - type: Dependency type (currently "git" or "git_sparse")
--   - url: Git repository URL
--   - branch: Branch to checkout (used if tag is not specified)
--   - tag: Specific tag to checkout (takes precedence over branch)
--   - dest: Destination path relative to project root
--   - optional: If true, failure to fetch will not stop the build
--   - sparse_paths: List of paths for sparse checkout (git_sparse type only)
-- =============================================================================

DEPENDENCIES = {
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
        tag = "0.15.0",
        dest = "lib/tinyusb",
        optional = false,
    },
    {
        name = "mbedtls",
        type = "git",
        url = "https://github.com/Mbed-TLS/mbedtls.git",
        tag = "v3.6.2",
        dest = "lib/mbedtls",
        optional = false,
    }
}
