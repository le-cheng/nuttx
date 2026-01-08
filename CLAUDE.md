# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

Apache NuttX is a POSIX-compliant real-time operating system (RTOS) scalable from 8-bit to 64-bit microcontrollers. The project consists of two main repositories:

- **nuttx/** - This repository: RTOS kernel, drivers, filesystems, networking, scheduler, memory management
- **nuttx-apps/** - Separate repository: Applications, examples, tests, libraries, NSH (NuttX Shell)

Both repositories should be side-by-side in the parent directory for proper builds.

## Build System

### Configuration

```bash
# Configure for a specific board:config
./tools/configure.sh <board>:<config>

# List available configurations
./tools/configure.sh -L

# List configurations for a specific board
./tools/configure.sh -L <board>

# Specify apps directory location if not side-by-side
./tools/configure.sh -a ../nuttx-apps <board>:<config>
```

### Building

```bash
# Build (from nuttx directory)
make

# Clean build artifacts
make clean

# Full clean including configuration
make distclean

# CMake build (out-of-tree)
cmake -S nuttx -B build -DBOARD_CONFIG=<board>:<config>
cmake --build build
```

### Code Style Checking

```bash
# Check style for recent commits
./tools/checkpatch.sh -g HEAD~...HEAD

# Check style for specific file
./tools/checkpatch.sh -f path/to/file.c
```

## Architecture

### Directory Structure

- **arch/** - Architecture-specific code (arm, arm64, risc-v, mips, xtensa, etc.)
  - Each architecture has: src/, include/, chip/ family/ subdirectories
- **boards/** - Board Support Packages (BSPs) organized by architecture
  - `boards/<arch>/<vendor>/<board>/src/` - Board-specific initialization
  - `boards/<arch>/<vendor>/<board>/include/` - Board-specific headers
  - Configurations in `boards/<arch>/<vendor>/<board>/configs/<config>/`
- **drivers/** - Device drivers (audio, wireless, graphics, sensors, etc.)
- **net/** - Networking stack (TCP/IP, CAN, Bluetooth, IEEE 802.15.4, etc.)
- **fs/** - Filesystems (NuttX, LittleFS, FatFS, ROMFS, etc.)
- **sched/** - Scheduler (task/thread management, signals, POSIX APIs)
- **mm/** - Memory management
- **libs/** - Libraries (math, cxx, graphics, etc.)
- **syscall/** - System call interfaces
- **binfmt/** - Binary format loaders (ELF, script interpreters)
- **crypto/** - Cryptography framework
- **include/** - Public headers
- **tools/** - Build tools, utilities, CI scripts

### Configuration System

NuttX uses Kconfig for configuration. Key files:
- `.config` - Generated configuration file
- `Kconfig` - Top-level configuration
- `boards/*/Kconfig` - Board-specific options
- `defconfig` files in board configuration directories

Configuration drives feature selection - components are only built if enabled.

### Board Support Pattern

Each board has:
- Startup code in `src/` (boot logic, board initialization)
- Pin multiplexing and clock configuration
- Device-specific drivers
- Configuration defconfig files for different use cases

## Simulator

The sim target provides a native host simulator:
```bash
./tools/configure.sh sim:nsh
make
./nuttx
```

## Testing

- **ostest** - Core OS functionality tests
- **posix_test** - POSIX compliance tests
- **cxxtest** - C++ library tests
- **tools/ci/** - CI infrastructure
- **tools/testbuild.sh** - Build testing script

For code changes, real hardware testing with build and runtime logs is expected (see CONTRIBUTING.md).

## Commit Message Format

NuttX requires a specific commit message format (see CONTRIBUTING.md):

```
<area>: <topic>. (note the period)

<detailed description of what changed, how, and why>
<multiple lines ok>

Signed-off-by: Name <email>
```

Example:
```
net/can: Add g_ prefix to can_dlc_to_len and len_to_can_dlc.

Add g_ prefix to can_dlc_to_len and len_to_can_dlc to
follow NuttX coding style conventions for global symbols.

Signed-off-by: Your Name <you@example.com>
```

## Key Principles

From INVIOLABLES.md - these are never to be violated:

1. **Strict POSIX compliance** - The portable interface must never be compromised
2. **Modular Architecture** - Formalized internal interfaces, minimal global variables
3. **Coding Style** - Strict conformance to NuttX coding standard
4. **No Breaking Changes** - Self-compatibility and long-term maintenance are paramount
5. **All Users Matter** - Support all platforms (Linux, Windows, macOS, BSD), all architectures, all toolchains

## Common Patterns

- Global functions use `g_` prefix
- Architecture-specific code isolated in `arch/<arch>/`
- Drivers follow character device or block device interfaces
- Board code bridges architecture and drivers
- Kconfig options control feature inclusion at compile time

## Resources

- Documentation: https://nuttx.apache.org/docs/latest/
- Contributing: https://nuttx.apache.org/docs/latest/contributing/index.html
- Coding Style: https://nuttx.apache.org/docs/latest/contributing/coding_style.html
