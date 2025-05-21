# iOS Battery Health Analyzer

[[License: GPL-3.0](https://img.shields.io/badge/License-GPL%203.0-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)

This utility analyzes iOS sysdiagnose archives to extract and display battery health information, including cycle count and last charging date, from the latest BatteryBDC logs. While iPhone 15 and newer models display battery cycle count directly in the Battery Health settings, older iPhone models also record this data but don't expose it in the UI. This tool allows users of older devices to access this important battery health metric that would otherwise remain hidden.

## Features

- Automatically locates and extracts the most recent BatteryBDC log file from iOS sysdiagnose reports
- Parses CSV data to retrieve battery health metrics
- Displays key information including:
  - Battery cycle count
  - Last charging timestamp
- Handles compressed tar.gz archives efficiently

## Usage

```
BatteryHealthAnalyzer <Sysdiagnose Report tar.gz File>
```

### Example

```
BatteryHealthAnalyzer Sysdiagnose_2025-05-15_13-45-32.tar.gz
```

### Sample Output

```
Parsing Sysdiagnose Report: Sysdiagnose_2025-05-15_13-45-32.tar.gz

Latest BatteryBDC daily Log found BDC_Daily_version_2025-05-14_20:30:45.csv

Checking Charging Cycle...
Battery Cycle Count: 253
Last Charging Date: 2025-05-14 20:15:23
```

## Background

iOS devices regularly log battery diagnostic information in sysdiagnose reports. This utility helps users and technicians access this information without having to manually extract and parse these logs. This can be particularly useful for:

- Checking battery health without using third-party apps
- Diagnosing power-related issues
- Verifying battery cycle count for device assessment
- Technical support and repair verification

## How It Works

1. The utility opens the specified sysdiagnose tar.gz archive
2. It searches for BatteryBDC log files within the `logs/BatteryBDC/` directory
3. It identifies the most recent log file based on the embedded timestamp
4. It extracts and parses the CSV data to retrieve battery information
5. It displays the extracted information in a human-readable format

## Generating Sysdiagnose Reports

To use this tool, you first need to generate a sysdiagnose report from your iOS device. For instructions on how to create, locate, and share sysdiagnose files, please refer to Apple's official documentation:

[Apple Support - Capturing sysdiagnose logs](https://it-training.apple.com/tutorials/support/sup075/)

Once you have the sysdiagnose file, you can analyze it with this tool to extract the battery health information.

## Building from Source

### Prerequisites

- C compiler (GCC, Clang, or MSVC)
- zlib development libraries
- Windows.h (if building on Windows)

### Compilation

```bash
# On Linux/macOS
gcc -o BatteryHealthAnalyzer main.c -lz

# On Windows with MSVC
cl main.c /link zlib.lib
```

## License

This project is licensed under the GNU General Public License v3.0.

## Contributors

- Initial implementation by MJ0011 of Kunlun Lab

## Support

For bugs, feature requests, or general questions:
- Open an issue on GitHub

**Note**: This tool is not affiliated with or endorsed by Apple Inc. All trademarks are the property of their respective owners.
