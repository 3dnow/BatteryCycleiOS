#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <windows.h>
#include <stdbool.h>
#include <time.h>

/**
 * Constants
 */
#define CHUNK 16384                // Chunk size for reading
#define TAR_BLOCK_SIZE 512         // TAR file block size
#define MAX_PATH_LENGTH 260        // Maximum path length
#define MAX_COLUMNS 256            // Maximum CSV columns
#define MAX_BUFFER_SIZE 1024       // General buffer size

 /**
  * File matcher callback function type
  * @param szFileName File name to match
  * @param pUserData User data for callback
  * @return 1 to extract the file, 0 to skip it
  */
typedef int (*pfnFileMatcherCallback)(const char* szFileName, void* pUserData);

/**
 * TAR file header structure according to POSIX standard
 */
typedef struct {
    char szName[100];        // File name
    char szMode[8];          // File mode/permissions
    char szUid[8];           // User ID
    char szGid[8];           // Group ID
    char szSize[12];         // File size (octal)
    char szMtime[12];        // Modification time
    char szChksum[8];        // Header checksum
    char cTypeflag;          // Type of file
    char szLinkname[100];    // Name of linked file
    char szMagic[6];         // UStar indicator "ustar"
    char szVersion[2];       // UStar version
    char szUname[32];        // User name
    char szGname[32];        // Group name
    char szDevmajor[8];      // Device major number
    char szDevminor[8];      // Device minor number
    char szPrefix[155];      // Filename prefix
    char szPadding[12];      // Padding to make header 512 bytes
} tarHeaderT;

/**
 * Date structure for tracking file dates
 */
typedef struct {
    int nYear;
    int nMonth;
    int nDay;
    int nHour;
    int nMinute;
    int nSecond;
    char szFilename[MAX_PATH_LENGTH]; // Store the full filename
    time_t tTimestamp;               // Unix timestamp for easy comparison
} fileDateT;

/**
 * Data structure to pass to the callback
 */
typedef struct {
    const char* szPrefix;     // Prefix to match (e.g., "BDC_Daily_")
    fileDateT stLatestFile;   // Stores information about the latest file found
    int bFoundMatch;          // Flag to indicate if a match was found
} matcherDataT;

/**
 * Convert octal string to unsigned long
 * @param szStr Octal string
 * @param nSize Size of the string
 * @return Converted value
 */
unsigned long ulParseOctal(const char* szStr, size_t nSize) {
    unsigned long ulResult = 0;
    size_t i;

    // Skip leading spaces and nulls
    for (i = 0; i < nSize && (szStr[i] == ' ' || szStr[i] == '\0'); i++);

    for (; i < nSize && szStr[i] != ' ' && szStr[i] != '\0'; i++) {
        if (szStr[i] < '0' || szStr[i] > '7')
            break;
        ulResult = ulResult * 8 + (szStr[i] - '0');
    }

    return ulResult;
}

/**
 * Check if file path is in the target directory
 * @param szPath File path to check
 * @param szTargetDir Target directory
 * @return 1 if path is in directory, 0 otherwise
 */
int bIsInDirectory(const char* szPath, const char* szTargetDir) {
    if (!szPath || !szTargetDir) {
        return 0; // Invalid parameters
    }

    size_t nDirLen = strlen(szTargetDir);

    // Handle case where target_dir doesn't end with a slash
    if (nDirLen > 0 && szTargetDir[nDirLen - 1] != '/' && szTargetDir[nDirLen - 1] != '\\') {
        // Create a new string with slash added
        char* szDirWithSlash = (char*)malloc(nDirLen + 2);
        if (!szDirWithSlash) {
            fprintf(stderr, "Error: Memory allocation failed\n");
            return 0;
        }

        // Safe string copy
        if (strncpy_s(szDirWithSlash, nDirLen + 2, szTargetDir, nDirLen) != 0) {
            fprintf(stderr, "Error: String copy failed\n");
            free(szDirWithSlash);
            return 0;
        }

        szDirWithSlash[nDirLen] = '/';
        szDirWithSlash[nDirLen + 1] = '\0';

        int nResult = strncmp(szPath, szDirWithSlash, nDirLen + 1) == 0;
        free(szDirWithSlash);
        return nResult;
    }

    return strncmp(szPath, szTargetDir, nDirLen) == 0;
}

/**
 * Get data from specified row and column name in a CSV buffer
 * @param szCsvBuffer Pointer to CSV data in memory
 * @param nRow Row number (0 for first row after header, -1 for last row)
 * @param szColName Name of the column to retrieve data from
 * @param szResult Buffer to store the result
 * @param nBufSize Size of the result buffer
 * @return 0 for success, negative number for failure
 */
int nGetCSVDataByColName(const char* szCsvBuffer, int nRow, const char* szColName,
    char* szResult, size_t nBufSize) {
    if (szCsvBuffer == NULL || szColName == NULL || szResult == NULL || nBufSize <= 0) {
        return -1; // Invalid parameters
    }

    // Initialize result
    if (strncpy_s(szResult, nBufSize, "", 1) != 0) {
        return -1; // Error in initialization
    }

    // Make a copy of the buffer to avoid modifying the original
    size_t nBufferLen = strlen(szCsvBuffer);
    char* szBufferCopy = (char*)malloc(nBufferLen + 1);
    if (szBufferCopy == NULL) {
        return -2; // Memory allocation failed
    }

    if (memcpy_s(szBufferCopy, nBufferLen + 1, szCsvBuffer, nBufferLen + 1) != 0) {
        free(szBufferCopy);
        return -2; // Memory copy failed
    }

    // Save a pointer to the start for proper memory cleanup
    char* szBufferStart = szBufferCopy;

    // Store column names and their positions
    char* szColumnNames[MAX_COLUMNS] = { 0 };  // Limit maximum columns
    int nColumnCount = 0;

    // Get the header line
    char* pHeaderEnd = strchr(szBufferCopy, '\n');
    if (!pHeaderEnd) {
        // No newline in data, treat entire buffer as header
        pHeaderEnd = szBufferCopy + nBufferLen;
    }

    // Temporarily null-terminate the header
    char cOriginalChar = *pHeaderEnd;
    *pHeaderEnd = '\0';

    // Parse header to get column names
    char* szHeaderToken = szBufferCopy;
    bool bInQuotes = false;
    int nTokenStart = 0;
    int i = 0;

    // More careful CSV parsing that handles quoted fields
    while (szHeaderToken[i] != '\0' && nColumnCount < MAX_COLUMNS) {
        if (szHeaderToken[i] == '"') {
            bInQuotes = !bInQuotes;
        }
        else if (szHeaderToken[i] == ',' && !bInQuotes) {
            // We found a column delimiter
            szHeaderToken[i] = '\0';  // Replace comma with null terminator

            // Trim whitespace and quotes
            char* szTrimmedName = szHeaderToken + nTokenStart;
            while (*szTrimmedName == ' ' || *szTrimmedName == '\t' || *szTrimmedName == '"')
                szTrimmedName++;

            char* pEnd = szTrimmedName + strlen(szTrimmedName) - 1;
            while (pEnd > szTrimmedName &&
                (*pEnd == ' ' || *pEnd == '\t' || *pEnd == '"' || *pEnd == '\r'))
                pEnd--;

            *(pEnd + 1) = '\0';

            // Store the column name
            szColumnNames[nColumnCount] = szTrimmedName;
            nColumnCount++;

            // Move to next token
            nTokenStart = i + 1;
        }
        i++;
    }

    // Handle the last column in header
    if (nColumnCount < MAX_COLUMNS) {
        char* szTrimmedName = szHeaderToken + nTokenStart;
        while (*szTrimmedName == ' ' || *szTrimmedName == '\t' || *szTrimmedName == '"')
            szTrimmedName++;

        char* pEnd = szTrimmedName + strlen(szTrimmedName) - 1;
        while (pEnd > szTrimmedName &&
            (*pEnd == ' ' || *pEnd == '\t' || *pEnd == '"' || *pEnd == '\r'))
            pEnd--;

        *(pEnd + 1) = '\0';

        // Store the last column name
        szColumnNames[nColumnCount] = szTrimmedName;
        nColumnCount++;
    }

    // Find our target column index
    int nTargetCol = -1;
    for (i = 0; i < nColumnCount; i++) {
        if (strcmp(szColumnNames[i], szColName) == 0) {
            nTargetCol = i;
            break;
        }
    }

    // Restore the character we replaced
    *pHeaderEnd = cOriginalChar;

    // If column name not found
    if (nTargetCol == -1) {
        free(szBufferStart);
        return -3; // Column name not found
    }

    // Move to the next line (after the header)
    szBufferCopy = pHeaderEnd + 1;

    // Skip to specified row
    int nCurrentRow = 0;

    if (nRow >= 0) {
        while (nCurrentRow < nRow) {
            char* pNextLine = strchr(szBufferCopy, '\n');
            if (!pNextLine) {
                // No more lines
                free(szBufferStart);
                return -5; // Row not found
            }
            szBufferCopy = pNextLine + 1;
            nCurrentRow++;
        }
    }
    else if (nRow == -1) {
        // Find the last row
        char* pLastRowStart = szBufferCopy;

        while (true) {
            char* pNextLine = strchr(szBufferCopy, '\n');
            if (!pNextLine) {
                break; // No more lines, we're at the last row
            }
            pLastRowStart = szBufferCopy;
            szBufferCopy = pNextLine + 1;
        }

        // Use the last row
        szBufferCopy = pLastRowStart;
    }

    // Now szBufferCopy points to the start of our target row
    // Get end of the row
    char* pRowEnd = strchr(szBufferCopy, '\n');
    if (!pRowEnd) {
        // This is the last row without a newline
        pRowEnd = szBufferCopy + strlen(szBufferCopy);
    }

    // Temporarily terminate the row
    cOriginalChar = *pRowEnd;
    *pRowEnd = '\0';

    // Now parse the row to find the target column
    bInQuotes = false;
    int nCurrentCol = 0;
    i = 0;
    nTokenStart = 0;

    while (szBufferCopy[i] != '\0' && nCurrentCol <= nTargetCol) {
        if (szBufferCopy[i] == '"') {
            bInQuotes = !bInQuotes;
        }
        else if (szBufferCopy[i] == ',' && !bInQuotes) {
            if (nCurrentCol == nTargetCol) {
                // We found our target column
                szBufferCopy[i] = '\0';  // Replace comma with null terminator

                // Extract the value, handling quotes and whitespace
                char* szValue = szBufferCopy + nTokenStart;
                while (*szValue == ' ' || *szValue == '\t' || *szValue == '"')
                    szValue++;

                char* pValueEnd = szValue + strlen(szValue) - 1;
                while (pValueEnd > szValue &&
                    (*pValueEnd == ' ' || *pValueEnd == '\t' ||
                        *pValueEnd == '"' || *pValueEnd == '\r'))
                    pValueEnd--;

                *(pValueEnd + 1) = '\0';

                // Copy to result safely
                if (strncpy_s(szResult, nBufSize, szValue, _TRUNCATE) != 0) {
                    free(szBufferStart);
                    return -6; // String copy failed
                }

                *pRowEnd = cOriginalChar;  // Restore original character
                free(szBufferStart);
                return 0; // Success
            }

            // Move to next column
            nCurrentCol++;
            nTokenStart = i + 1;
        }
        i++;
    }

    // Check if the last column is our target
    if (nCurrentCol == nTargetCol) {
        // Extract the value, handling quotes and whitespace
        char* szValue = szBufferCopy + nTokenStart;
        while (*szValue == ' ' || *szValue == '\t' || *szValue == '"')
            szValue++;

        char* pValueEnd = szValue + strlen(szValue) - 1;
        while (pValueEnd > szValue &&
            (*pValueEnd == ' ' || *pValueEnd == '\t' ||
                *pValueEnd == '"' || *pValueEnd == '\r'))
            pValueEnd--;

        *(pValueEnd + 1) = '\0';

        // Copy to result safely
        if (strncpy_s(szResult, nBufSize, szValue, _TRUNCATE) != 0) {
            free(szBufferStart);
            return -6; // String copy failed
        }

        *pRowEnd = cOriginalChar;  // Restore original character
        free(szBufferStart);
        return 0; // Success
    }

    // Restore the character we replaced
    *pRowEnd = cOriginalChar;

    // Column not found in row
    free(szBufferStart);
    return -4;
}

/**
 * Extract files from a tar.gz file that match the target directory and pass matcher callback
 * @param szTargzPath Path to the tar.gz file
 * @param szTargetDir Target directory to extract from
 * @param pfnMatcher Callback function for file matching
 * @param pUserData User data for callback
 * @return 0 on success, non-zero on error
 */
int nExtractFromTargzWithCallback(
    const char* szTargzPath,
    const char* szTargetDir,
    pfnFileMatcherCallback pfnMatcher,
    void* pUserData
) {
    gzFile gzTarFile;
    tarHeaderT stHeader;
    char szBuffer[CHUNK];
    unsigned long ulFileSize;
    int nRet = 0;

    // Open tar.gz file
    gzTarFile = gzopen(szTargzPath, "rb");
    if (!gzTarFile) {
        fprintf(stderr, "Error: Cannot open %s\n", szTargzPath);
        return -1;
    }

    // Main extraction loop
    while (1) {
        // Read the tar header
        if (gzread(gzTarFile, &stHeader, TAR_BLOCK_SIZE) != TAR_BLOCK_SIZE) {
            if (gzeof(gzTarFile)) {
                // End of archive
                break;
            }
            fprintf(stderr, "Error: Unexpected end of archive\n");
            nRet = -1;
            break;
        }

        // Check for end of archive (empty block)
        if (stHeader.szName[0] == '\0') {
            // Skip one more block to validate end of archive
            gzread(gzTarFile, szBuffer, TAR_BLOCK_SIZE);
            break;
        }

        // Validate file path for safety - prevent path traversal attacks
        if (strstr(stHeader.szName, "../") || strstr(stHeader.szName, "..\\")) {
            fprintf(stderr, "Warning: Skipping potentially unsafe path: %s\n", stHeader.szName);
            // Skip this file's content
            ulFileSize = ulParseOctal(stHeader.szSize, sizeof(stHeader.szSize));
            size_t nBlocks = (ulFileSize + TAR_BLOCK_SIZE - 1) / TAR_BLOCK_SIZE;
            gzseek(gzTarFile, nBlocks * TAR_BLOCK_SIZE, SEEK_CUR);
            continue;
        }

        // Get file size
        ulFileSize = ulParseOctal(stHeader.szSize, sizeof(stHeader.szSize));

        // Calculate number of blocks for this file and padding
        size_t nBlocks = (ulFileSize + TAR_BLOCK_SIZE - 1) / TAR_BLOCK_SIZE;

        // Check if it's a file and if it's in our target directory
        if ((stHeader.cTypeflag == '0' || stHeader.cTypeflag == '\0') &&
            bIsInDirectory(stHeader.szName, szTargetDir)) {

            // Extract the filename from the path
            const char* szFilename = stHeader.szName;
            const char* pSlash = strrchr(stHeader.szName, '/');
            if (pSlash) {
                szFilename = pSlash + 1;
            }

            // Skip if filename is empty (directory entry)
            if (*szFilename == '\0') {
                gzseek(gzTarFile, nBlocks * TAR_BLOCK_SIZE, SEEK_CUR);
                continue;
            }

            // Call the matcher callback to see if we should extract this file
            if (pfnMatcher && !pfnMatcher(szFilename, pUserData)) {
                // Matcher says skip this file
                gzseek(gzTarFile, nBlocks * TAR_BLOCK_SIZE, SEEK_CUR);
                continue;
            }

            // Extract file content in chunks
            size_t nRemaining = ulFileSize;

            // Allocate memory for the full file content
            char* szCsvBuf = (char*)malloc(ulFileSize + 1); // +1 for null terminator
            if (!szCsvBuf) {
                fprintf(stderr, "Error: Memory allocation failed for file content\n");
                gzclose(gzTarFile);
                return -2;
            }

            // Ensure null-termination
            szCsvBuf[ulFileSize] = '\0';

            char* szCurrentBuf = szCsvBuf;

            while (nRemaining > 0) {
                size_t nToRead = (nRemaining > CHUNK) ? CHUNK : nRemaining;
                size_t nBytesRead = gzread(gzTarFile, szBuffer, nToRead);

                if (nBytesRead != nToRead) {
                    fprintf(stderr, "Error: Failed to read data (expected %lu, got %lu)\n",
                        (unsigned long)nToRead, (unsigned long)nBytesRead);
                    free(szCsvBuf);
                    gzclose(gzTarFile);
                    return -1;
                }

                if (memcpy_s(szCurrentBuf, nRemaining, szBuffer, nBytesRead) != 0) {
                    fprintf(stderr, "Error: Memory copy failed\n");
                    free(szCsvBuf);
                    gzclose(gzTarFile);
                    return -3;
                }

                szCurrentBuf += nBytesRead;
                nRemaining -= nBytesRead;
            }

            // Process the CSV data
            char szBufferTimestamp[MAX_BUFFER_SIZE] = { 0 };
            char szBufferCycleCount[MAX_BUFFER_SIZE] = { 0 };

            if (nGetCSVDataByColName(szCsvBuf, -1, "TimeStamp", szBufferTimestamp, sizeof(szBufferTimestamp)) != 0) {
                fprintf(stderr, "Error: Failed to parse timestamp\n");
                free(szCsvBuf);
                break;
            }

            if (nGetCSVDataByColName(szCsvBuf, -1, "CycleCount", szBufferCycleCount, sizeof(szBufferCycleCount)) != 0) {
                fprintf(stderr, "Error: Failed to parse CycleCount\n");
                free(szCsvBuf);
                break;
            }

            printf("Battery Cycle Count: %s\nLast Charging Date: %s\n",
                szBufferCycleCount, szBufferTimestamp);

            // Free the memory we allocated
            free(szCsvBuf);
            break;

            // Skip padding if necessary - Note: This code is unreachable after break
            // size_t nPadding = nBlocks * TAR_BLOCK_SIZE - ulFileSize;
            // if (nPadding > 0) {
            //     gzseek(gzTarFile, nPadding, SEEK_CUR);
            // }
        }
        else {
            // Skip this file's data blocks
            gzseek(gzTarFile, nBlocks * TAR_BLOCK_SIZE, SEEK_CUR);
        }
    }

    gzclose(gzTarFile);
    return nRet;
}

/**
 * File matcher that accepts files with specific extension
 * @param szFilename Filename to check
 * @param pUserData Pointer to extension string
 * @return 1 if matches, 0 otherwise
 */
int bFileExtensionMatcher(const char* szFilename, void* pUserData) {
    const char* szExtension = (const char*)pUserData;
    const char* szFileExt = strrchr(szFilename, '.');

    if (!szFileExt) return 0; // No extension

    return _stricmp(szFileExt, szExtension) == 0;
}

/**
 * File matcher that uses a wildcard pattern (basic implementation)
 * @param szFilename Filename to check
 * @param pUserData Pointer to pattern string
 * @return 1 if matches, 0 otherwise
 */
int bWildcardMatcher(const char* szFilename, void* pUserData) {
    const char* szPattern = (const char*)pUserData;

    // Simple wildcard matching for "*.ext" patterns
    if (szPattern[0] == '*' && szPattern[1] == '.') {
        const char* szFileExt = strrchr(szFilename, '.');
        if (!szFileExt) return 0;
        return _stricmp(szFileExt, szPattern + 1) == 0;
    }

    // Exact match
    return strcmp(szFilename, szPattern) == 0;
}/**
 * Parse date in format YYYY-MM-DD_HH:MM:SS
 * @param szDateStr Date string to parse
 * @param pDate Pointer to date structure to fill
 * @return 1 if successful, 0 if failed
 */
int bParseDate(const char* szDateStr, fileDateT* pDate) {
    if (!szDateStr || !pDate) {
        return 0; // Invalid parameters
    }

    // Using sscanf_s which is the secure version of sscanf
    int nResult = sscanf_s(szDateStr, "%d-%d-%d_%d:%d:%d",
        &pDate->nYear, &pDate->nMonth, &pDate->nDay,
        &pDate->nHour, &pDate->nMinute, &pDate->nSecond);

    if (nResult != 6) {
        fprintf(stderr, "Error: Date parsing failed for %s\n", szDateStr);
        return 0; // Failed to parse all date components
    }

    // Validate date components
    if (pDate->nYear < 1970 || pDate->nYear > 2100 ||
        pDate->nMonth < 1 || pDate->nMonth > 12 ||
        pDate->nDay < 1 || pDate->nDay > 31 ||
        pDate->nHour < 0 || pDate->nHour > 23 ||
        pDate->nMinute < 0 || pDate->nMinute > 59 ||
        pDate->nSecond < 0 || pDate->nSecond > 59) {
        fprintf(stderr, "Error: Invalid date components in %s\n", szDateStr);
        return 0; // Invalid date components
    }

    // Convert to timestamp for easy comparison
    struct tm stTimeinfo;
    memset(&stTimeinfo, 0, sizeof(struct tm));
    stTimeinfo.tm_year = pDate->nYear - 1900;
    stTimeinfo.tm_mon = pDate->nMonth - 1;
    stTimeinfo.tm_mday = pDate->nDay;
    stTimeinfo.tm_hour = pDate->nHour;
    stTimeinfo.tm_min = pDate->nMinute;
    stTimeinfo.tm_sec = pDate->nSecond;

    pDate->tTimestamp = mktime(&stTimeinfo);
    if (pDate->tTimestamp == -1) {
        fprintf(stderr, "Error: Failed to convert date to timestamp\n");
        return 0;
    }

    return 1; // Successfully parsed
}

/**
 * Callback function to find the latest BDC_Daily_ file
 * @param szFilename Filename to check
 * @param pUserData User data (matcherDataT)
 * @return 0 to continue searching without extracting
 */
int bLatestBdcDailyMatcher(const char* szFilename, void* pUserData) {
    matcherDataT* pData = (matcherDataT*)pUserData;
    if (!pData || !szFilename) {
        return 0;
    }

    // Check if file starts with the prefix
    if (strncmp(szFilename, pData->szPrefix, strlen(pData->szPrefix)) != 0) {
        return 0; // Not a BDC_Daily_ file
    }

    // Get the date part (after the prefix)
    const char* szDatePart = szFilename + strlen(pData->szPrefix);

    szDatePart = strchr(szDatePart, '_');

    if (szDatePart == 0) {
        return 0;
    }

    szDatePart++;

    // Parse the date
    fileDateT stCurrentFile;
    memset(&stCurrentFile, 0, sizeof(fileDateT));

    if (strncpy_s(stCurrentFile.szFilename, sizeof(stCurrentFile.szFilename),
        szFilename, _TRUNCATE) != 0) {
        fprintf(stderr, "Error: Failed to copy filename\n");
        return 0;
    }

    if (!bParseDate(szDatePart, &stCurrentFile)) {
        fprintf(stderr, "Error: Invalid date format\n");
        return 0; // Invalid date format
    }

    // First match or newer than previous match
    if (!pData->bFoundMatch || stCurrentFile.tTimestamp > pData->stLatestFile.tTimestamp) {
        pData->stLatestFile = stCurrentFile;
        pData->bFoundMatch = 1;
    }

    // We're only collecting information in this pass, not extracting yet
    return 0;
}

/**
 * Callback function to extract only the latest file (second pass)
 * @param szFilename Filename to check
 * @param pUserData User data (matcherDataT)
 * @return 1 if file should be extracted, 0 otherwise
 */
int bExtractLatestFileMatcher(const char* szFilename, void* pUserData) {
    matcherDataT* pData = (matcherDataT*)pUserData;
    if (!pData || !szFilename) {
        return 0;
    }

    // Only extract if this is the latest file we found
    return strcmp(szFilename, pData->stLatestFile.szFilename) == 0;
}

/**
 * Function to find and extract the latest BDC_Daily_ file
 * @param szTargzPath Path to tar.gz file
 * @param szTargetDir Target directory in archive
 * @return 0 on success, non-zero on error
 */
int nExtractLatestBdcDailyFile(
    const char* szTargzPath,
    const char* szTargetDir
) {
    if (!szTargzPath || !szTargetDir) {
        fprintf(stderr, "Error: Invalid parameters\n");
        return -1;
    }

    // Setup matcher data
    matcherDataT stData;
    memset(&stData, 0, sizeof(matcherDataT));
    stData.szPrefix = "BDC_Daily_version";
    stData.bFoundMatch = 0;

    // First pass: Find the latest file
    printf("Parsing Sysdiagnose Report: %s\n", szTargzPath);

    int nResult = nExtractFromTargzWithCallback(szTargzPath, szTargetDir,
        bLatestBdcDailyMatcher, &stData);
    if (nResult != 0) {
        fprintf(stderr, "Error: Failed to analyze archive\n");
        return nResult;
    }

    if (!stData.bFoundMatch) {
        fprintf(stderr, "Error: No matching BDC_Daily_ files found\n");
        return -1;
    }

    // Print info about the latest file
    printf("\nLatest BatteryBDC daily Log found %s\n", stData.stLatestFile.szFilename);

    // Second pass: Extract only the latest file
    printf("\nChecking Charging Cycle...\n");
    return nExtractFromTargzWithCallback(szTargzPath, szTargetDir,
        bExtractLatestFileMatcher, &stData);
}

/**
 * Main function
 * @param argc Argument count
 * @param argv Argument vector
 * @return Exit status
 */
int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Usage: %s <Sysdiagnose Report tar.gz File>\n", argv[0]);
        printf("Example: %s Sysdiagnose_.tar.gz\n", argv[0]);
        return 1;
    }

    // Find and extract the latest BDC_Daily_ file
    return nExtractLatestBdcDailyFile(argv[1], "logs/BatteryBDC/");
}