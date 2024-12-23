#pragma once

#include <pico/stdlib.h>
#include "ROMImageManager.h"

#ifdef MASS_STORAGE_DEBUG
// More time needed when in debug mode, as it takes time to upload debug information to the console!
#define MASS_STORAGE_REFRESH_EJECTION_DURATION_MS 5000
#else
#define MASS_STORAGE_REFRESH_EJECTION_DURATION_MS 1000
#endif
#define MASS_STORAGE_REFRESH_INJECTION_DURATION_MS 4000
#define MASS_STORAGE_FIRST_INJECTION_DURATION_MS 100

#define DISK_BLOCK_COUNT ((DISK_BLOCK_COUNT_ROM) + (DISK_BLOCK_COUNT_RAM))
#define DISK_BLOCK_COUNT_ROM 256
#define DISK_BLOCK_COUNT_RAM 24 // Change this if more blocks are needed
#define DISK_BLOCK_SIZE 512

#define MASS_STORAGE_VIRTUAL_BLOCK_COUNT (DISK_BLOCK_COUNT_ROM + DISK_BLOCK_COUNT_RAM + 2)
#define FAT_BLOCK_COUNT ((MASS_STORAGE_VIRTUAL_BLOCK_COUNT + MASS_STORAGE_VIRTUAL_BLOCK_COUNT / 2) / DISK_BLOCK_SIZE + 1)
#define FAT_ENTRIES_COUNT_IN_BLOCK ((DISK_BLOCK_SIZE * 2) / 3)
#define FAT_ENTRIES_COUNT_TOTAL (FAT_ENTRIES_COUNT_IN_BLOCK * FAT_BLOCK_COUNT)

#define DIRECTORY_ENTRY_SIZE 32
#define DIRECTORY_ENTRIES_COUNT (DISK_BLOCK_SIZE / DIRECTORY_ENTRY_SIZE)

#if FAT_BLOCK_COUNT != 1
#error "FAT block is not the correct size"
#endif

class MassStorageManager
{
public:
    static void Setup();
    static void Loop();
    static void PrintDirectoryListing();
    static int32_t WriteHook(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize);
    static int32_t ReadHook(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize);
    static uint16_t CreateFile(const char *fileName, const char *fileExtension, const char *fileContent, int fileContentSize, bool hidden = false, bool isFolder = false, uint16_t currentFolder = 1, uint16_t parentFolderCluster = 1);
    static uint16_t GetNextFreeClusterIndex();
    static bool ejected;
private:
    enum class ClusterType
    {
        Unknown,
        System,
        Directory,
        DataSingle,
        DataFirst,
        DataIntermediate,
        DataLast
    };
    static absolute_time_t timeToWaitBeforeInjection;
    static absolute_time_t timeToWaitBeforeEjection;
    static uint8_t BricoROMBankClusterNumber[FLASH_MAX_ROM_BANKS];
    
    static uint8_t MBR[62];
    static uint16_t VFAT[FAT_BLOCK_COUNT * DISK_BLOCK_SIZE + 3];
    static uint8_t ClustersContent[DISK_BLOCK_COUNT_RAM][DISK_BLOCK_SIZE];
    
    static void InitStorageContent();
    static void EjectInject();
    static void ReadMBR(void* buffer);
    static void ReadFAT(void* buffer);
    static void WriteFAT(void* buffer);
    static void WriteCluster(int clusterIndex, void* buffer);
    static uint8_t *GetClusterMemoryLocation(int clusterIndex);
    static int GetFreeRAMClustersAvailable();
    static void FileCopyComplete(int parentDirectoryCluster, int parentDirectoryEntryIndex);
    static ClusterType GetClusterInfo(int clusterIndex, int &parentDirectoryCluster, int &parentDirectoryEntryIndex, int &firstDataCluster);
    static bool FindClusterDirectoryEntry(int clusterIndex, int &parentDirectoryCluster, int &parentDirectoryEntryIndex);

    static void PrintDirectoryListing(int dirCluster, int depth);
    static void FormatFileName(const uint8_t *pDirEntry, uint8_t *pTargetBuffer13Bytes);
    static void FormatDepthIndentation(int depth);
    static void DebugCluster(int clusterNumber);
    static void PrettyPrintDirectoryBuffer(uint8_t *buffer);
    static int CountPreviousClusters(int clusterIndex);
    static bool IsValidFileExtension(const char *extension);
};