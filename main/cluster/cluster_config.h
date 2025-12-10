/**
 * @file cluster_config.h
 * @brief Clusteraxe Compile-Time Configuration
 *
 * This header defines compile-time options for building Master or Slave
 * firmware variants for the Bitaxe Cluster (Clusteraxe) project.
 *
 * Build Configurations:
 *   - Master: Full functionality with pool connection and slave coordination
 *   - Slave: Receives work from master, no direct pool connection
 *
 * @author Clusteraxe Project
 * @license GPL-3.0
 */

#ifndef CLUSTER_CONFIG_H
#define CLUSTER_CONFIG_H

// ============================================================================
// Build Mode Selection
// ============================================================================

/**
 * Define one of these in your sdkconfig or CMakeLists.txt:
 *   CONFIG_CLUSTER_MODE_MASTER - Build as cluster master
 *   CONFIG_CLUSTER_MODE_SLAVE  - Build as cluster slave
 *
 * If neither is defined, cluster functionality is disabled (standalone mode)
 */

#if defined(CONFIG_CLUSTER_MODE_MASTER)
    #define CLUSTER_ENABLED         1
    #define CLUSTER_IS_MASTER       1
    #define CLUSTER_IS_SLAVE        0
    #define CLUSTER_MODE_DEFAULT    CLUSTER_MODE_MASTER
#elif defined(CONFIG_CLUSTER_MODE_SLAVE)
    #define CLUSTER_ENABLED         1
    #define CLUSTER_IS_MASTER       0
    #define CLUSTER_IS_SLAVE        1
    #define CLUSTER_MODE_DEFAULT    CLUSTER_MODE_SLAVE
#else
    // Standalone mode - cluster disabled at compile time
    #define CLUSTER_ENABLED         0
    #define CLUSTER_IS_MASTER       0
    #define CLUSTER_IS_SLAVE        0
    #define CLUSTER_MODE_DEFAULT    CLUSTER_MODE_DISABLED
#endif

// ============================================================================
// Cluster Configuration Constants
// ============================================================================

// Maximum number of slaves a master can coordinate
#ifndef CONFIG_CLUSTER_MAX_SLAVES
    #define CONFIG_CLUSTER_MAX_SLAVES       8
#endif

// Work queue depth per slave
#ifndef CONFIG_CLUSTER_WORK_QUEUE_SIZE
    #define CONFIG_CLUSTER_WORK_QUEUE_SIZE  4
#endif

// Share queue depth (pending shares to submit)
#ifndef CONFIG_CLUSTER_SHARE_QUEUE_SIZE
    #define CONFIG_CLUSTER_SHARE_QUEUE_SIZE 16
#endif

// Heartbeat interval in milliseconds
#ifndef CONFIG_CLUSTER_HEARTBEAT_MS
    #define CONFIG_CLUSTER_HEARTBEAT_MS     3000
#endif

// Timeout before slave is considered disconnected (ms)
#ifndef CONFIG_CLUSTER_TIMEOUT_MS
    #define CONFIG_CLUSTER_TIMEOUT_MS       10000
#endif

// ============================================================================
// Feature Flags
// ============================================================================

// Enable verbose cluster debug logging
#ifndef CONFIG_CLUSTER_DEBUG_LOGGING
    #define CONFIG_CLUSTER_DEBUG_LOGGING    0
#endif

// Enable cluster statistics tracking
#ifndef CONFIG_CLUSTER_STATS_ENABLED
    #define CONFIG_CLUSTER_STATS_ENABLED    1
#endif

// Enable web API endpoints for cluster status
#ifndef CONFIG_CLUSTER_WEB_API_ENABLED
    #define CONFIG_CLUSTER_WEB_API_ENABLED  1
#endif

// ============================================================================
// Conditional Compilation Helpers
// ============================================================================

/**
 * Use these macros to conditionally include code:
 *
 * #if CLUSTER_ENABLED
 *     // Cluster-specific code
 * #endif
 *
 * #if CLUSTER_IS_MASTER
 *     // Master-only code
 * #endif
 *
 * #if CLUSTER_IS_SLAVE
 *     // Slave-only code
 * #endif
 */

// Helper to check if stratum should be disabled (slave mode)
#define CLUSTER_DISABLE_STRATUM     (CLUSTER_ENABLED && CLUSTER_IS_SLAVE)

// Helper to check if we should run the coordinator task
#define CLUSTER_RUN_COORDINATOR     (CLUSTER_ENABLED && CLUSTER_IS_MASTER)

// ============================================================================
// Version Information
// ============================================================================

#define CLUSTERAXE_VERSION_MAJOR    1
#define CLUSTERAXE_VERSION_MINOR    0
#define CLUSTERAXE_VERSION_PATCH    0

#if CLUSTER_IS_MASTER
    #define CLUSTERAXE_VERSION_STRING   "Clusteraxe-1.0.0-master"
#elif CLUSTER_IS_SLAVE
    #define CLUSTERAXE_VERSION_STRING   "Clusteraxe-1.0.0-slave"
#else
    #define CLUSTERAXE_VERSION_STRING   "Clusteraxe-1.0.0-standalone"
#endif

#endif // CLUSTER_CONFIG_H
