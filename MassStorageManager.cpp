//http://elm-chan.org/docs/fat_e.html
//https://fr.wikipedia.org/wiki/File_Allocation_Table

#include <bsp/board.h>
#include <tusb.h>
#include <pico/unique_id.h>
#include <bsp/board.h>
#include <ctype.h>

#include "MassStorageManager.h"
#include "MemoryMap.h"
#include "PIOManager.h"
#include "CommandManager.h"

#define FAT_OFFSET_FILE_NAME  0x00
#define FAT_OFFSET_FILE_EXTENSION  0x08
#define FAT_OFFSET_FILE_ATTRIBUTES  0x0b
#define FAT_OFFSET_FIRST_CLUSTER  0x1a
#define FAT_OFFSET_FILE_SIZE  0x1c

#define FAT_CLUSTER_MARKER_FILLED (1<<14)
#define FAT_CLUSTER_MARKER_ROM (1<<15)
#define FAT_CLUSTER_MARKERS (FAT_CLUSTER_MARKER_ROM | FAT_CLUSTER_MARKER_FILLED)
#define FAT_ADD_CLUSTER_MARKER_ROM(val) ((val)|(FAT_CLUSTER_MARKER_ROM))
#define FAT_ADD_CLUSTER_MARKER_FILLED(val) ((val)|(FAT_CLUSTER_MARKER_FILLED))
#define FAT_REMOVE_CLUSTER_MARKERS(val) ((val)&~(FAT_CLUSTER_MARKERS))

#define RAM_ADDRESS_IMAGE_IN_RAM ((uint16_t *)BASE_ADDRESS_IMAGE_IN_RAM)

#define ROM_BANK_FOLDER_NAME_ROOT "BANK"
#define ROM_BANK_FOLDER_NAME ROM_BANK_FOLDER_NAME_ROOT "%d"

static const char *ReadMeData = 
"Build " BUILD_DATE_TIME_FULL " - Bouz/@AurelienBricole\n" \
"    ____       _            _   __\n" \
"   / __ )_____(_)________  / | / /__  ____\n" \
"  / __  / ___/ / ___/ __ \\/  |/ / _ \\/ __ \\\n" \
" / /_/ / /  / / /__/ /_/ / /|  /  __/ /_/ /\n" \
"/_____/_/  /_/\\___/\\____/_/ |_/\\___/\\____/\n" \
"\n" \
"Place SP1 ROMs at drive root.\n" \
        ROM_BANK_FOLDER_NAME_ROOT " folders flash to corresponding banks.\n" \
        "Bank 0 loads at startup.\n"
        "Auto byteswap by detecting reset vector upper word endianness.";

static const char *IndexHtmData = "<html><head><meta http-equiv=\"refresh\" content=\"0;URL='https://www.delta-island.com/briconeo'\"/></head><body>Redirecting to <a href='https://www.delta-island.com/briconeo'>BricoNeo page on Delta Island forum</a></body></html>'";

#define AUTORUN_DATA_ROOT "[autorun]\r\n;Show pretty little NeoGeo icon\r\nicon=ICON.ICO\r\n"
#define AUTORUN_DATA_AUTOPLAY ";Disable automatic annoying explorer opening after each ROM copy\r\nUseAutoPlay=0\r\n"
#ifdef MASS_STORAGE_DISABLE_AUTOPLAY
#define AUTORUN_DATA AUTORUN_DATA_ROOT AUTORUN_DATA_AUTOPLAY
#else
#define AUTORUN_DATA AUTORUN_DATA_ROOT
#endif

static const char *AutoRunData = AUTORUN_DATA;

static const uint8_t DriveIconData[]  = {
  0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x10, 0x10, 0x10, 0x00, 0x00, 0x00, 0x04, 0x00, 0x28, 0x01, 
  0x00, 0x00, 0x16, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x20, 0x00, 
  0x00, 0x00, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x35, 
  0x59, 0x00, 0x10, 0x08, 0x0a, 0x00, 0x05, 0x4d, 0x6d, 0x00, 0x56, 0x2e, 0x04, 0x00, 0x70, 0x4d, 
  0x07, 0x00, 0x6a, 0x68, 0x66, 0x00, 0x06, 0x70, 0x97, 0x00, 0x7f, 0x82, 0x85, 0x00, 0x08, 0x88, 
  0xb6, 0x00, 0x0a, 0xa9, 0xe1, 0x00, 0x11, 0xc3, 0xff, 0x00, 0x9d, 0x6b, 0x06, 0x00, 0xb0, 0x81, 
  0x15, 0x00, 0xd8, 0x97, 0x00, 0x00, 0xac, 0xac, 0xab, 0x00, 0xff, 0xff, 0xff, 0x00, 0x11, 0x11, 
  0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0xbc, 0x13, 0x33, 0xb1, 0x11, 0x11, 0x11, 0x1c, 
  0xdd, 0x1d, 0x4c, 0xdb, 0xb1, 0x11, 0x11, 0xbd, 0xbc, 0xcd, 0x1c, 0xbc, 0xdb, 0x11, 0x13, 0xdb, 
  0xdd, 0xdb, 0x4b, 0xdd, 0xdd, 0x11, 0x13, 0xd1, 0xb4, 0xcd, 0xd4, 0xbd, 0xdd, 0x41, 0x11, 0xd4, 
  0xbb, 0x3d, 0xc4, 0x4c, 0xcd, 0x31, 0x11, 0xbd, 0xd4, 0x24, 0xdd, 0x40, 0x88, 0x11, 0x16, 0x23, 
  0x56, 0x98, 0xc3, 0x19, 0x99, 0x61, 0x16, 0x82, 0xa9, 0x9a, 0x91, 0x8a, 0xa9, 0x81, 0x12, 0xaa, 
  0xa8, 0x98, 0x22, 0x9a, 0xaa, 0x81, 0x16, 0xa8, 0x89, 0x9a, 0x68, 0xaa, 0x99, 0x21, 0x11, 0x86, 
  0x69, 0x99, 0x86, 0x9a, 0x92, 0x11, 0x11, 0x16, 0x26, 0x9a, 0x81, 0x66, 0x11, 0x11, 0x11, 0x11, 
  0x11, 0x06, 0x92, 0x61, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0xf0, 0x1f, 
  0x00, 0x00, 0xe0, 0x07, 0x00, 0x00, 0xc0, 0x03, 0x00, 0x00, 0x80, 0x01, 0x00, 0x00, 0x80, 0x01, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x01, 0x00, 0x00, 0x80, 0x01, 
  0x00, 0x00, 0xc0, 0x03, 0x00, 0x00, 0xe0, 0x07, 0x00, 0x00, 0xf8, 0x1f, 0x00, 0x00
};

#define TO_FAT_DATE(Y,M,D) ((((Y)-1980) << 9) + ((M) << 5) + (D))
#define TO_FAT_TIME(H,M,S) (((H) << 11) + ((M) << 5) + ((S) >> 1))

uint8_t MassStorageManager::BricoROMBankClusterNumber[FLASH_MAX_ROM_BANKS];
uint16_t MassStorageManager::VFAT[FAT_BLOCK_COUNT * DISK_BLOCK_SIZE + 3];
uint8_t MassStorageManager::ClustersContent[DISK_BLOCK_COUNT_RAM][DISK_BLOCK_SIZE];
absolute_time_t MassStorageManager::timeToWaitBeforeInjection;
absolute_time_t MassStorageManager::timeToWaitBeforeEjection;
bool MassStorageManager::ejected = true;
uint32_t MassStorageManager::tud_msc_test_unit_ready_cb_count = 0;

uint8_t MassStorageManager::MBR[62] =
{
    // Jump to startup program
    0xEB, 0x3C, 0x90,
    // "MSDOS5.0"
    0x4D, 0x53, 0x44, 0x4F, 0x53, 0x35, 0x2E, 0x30,
    // Bytes per sector
    DISK_BLOCK_SIZE & 0xff, (DISK_BLOCK_SIZE >> 8) & 0xff,
    // Sectors by cluster
    0x01,
    // Reserved clusters including boot sector
    0x01, 0x00,
    // FAT count on disk
    0x01,
    // Root folder entries count
    DISK_BLOCK_SIZE / 32, 0x00,
    // Total 16bit sectors count
    DISK_BLOCK_COUNT & 0xff, (DISK_BLOCK_COUNT >> 8) & 0xff,
    // Disk type (0xF8 for HDD, 0xF0 for removable disks). HDD might be necessaru to enable support for device ejection on all systems (even MacOS)
    0xF0,
    // Sectors count for FAT
    FAT_BLOCK_COUNT & 0xff, (FAT_BLOCK_COUNT >> 8) & 0xff,
    // Sectors per track
    0x01, 0x00,
    // Head count
    0x01, 0x00,
    // Hidden sectors
    0x00, 0x00, 0x00, 0x00,
    // Total 32bit sectors count
    0x00, 0x00, 0x00, 0x00,
    // Disk identifier (0x80 for fix drive, 0x00 for floppy)
    0x00,
    // Reserved
    0x00,
    // Signature (always 29)
    0x29,
    // Disk serial number
    0x34, 0x12, 0x00, 0x00,
    // Disk name on 11 characters
    'B' , 'r' , 'i' , 'c' , 'o' , 'N' , 'e' , 'o' , ' ' , ' ' , ' ',
    // File system type "FAT12"
    'F', 'A', 'T', '1', '2', ' ', ' ', ' ',
};

void MassStorageManager::Setup()
{
#ifdef USE_MASS_STORAGE
    timeToWaitBeforeInjection = from_us_since_boot(time_us_64() + MASS_STORAGE_FIRST_INJECTION_DURATION_MS * 1000);

    // Init CDC
    board_init();
#endif
    // init device stack on configured roothub port
    tud_init(BOARD_TUD_RHPORT);
}

void MassStorageManager::EjectInject()
{
    timeToWaitBeforeEjection = from_us_since_boot(time_us_64() + MASS_STORAGE_REFRESH_EJECTION_DURATION_MS * 1000); // Microseconds
}

void MassStorageManager::InitStorageContent()
{
#ifdef MASS_STORAGE_DEBUG
    printf("Initializing storage content\n");
#endif
    // Init VFAT
    memset(VFAT, 0, (FAT_BLOCK_COUNT * DISK_BLOCK_SIZE + 3) * sizeof(uint16_t));
    int i = 0;
    VFAT[i++] = 0x0ff8 | FAT_CLUSTER_MARKER_FILLED; // FAT (last cluster marker - for FAT)
    VFAT[i++] = 0x0fff | FAT_CLUSTER_MARKER_FILLED; // Root directory (last cluster marker) Otherwise, next cluster index

    // Clear clusters content
    memset(ClustersContent, 0, DISK_BLOCK_COUNT_RAM * DISK_BLOCK_SIZE);

    // Init Root Directory
    uint8_t * rootDirectory = ClustersContent[0];
    strcpy((char *)rootDirectory, "BricoNeo     "); // Volume name
    rootDirectory[11] = 0x08; // Volume name attribute

    // Patch volume identifier to increase chances the volume info will be updated
//    static uint8_t injectCounter = 0;
//    MBR[62-20] = injectCounter++;

    // Add file
    CreateFile("ICON", "ICO", (const char *)DriveIconData, sizeof(DriveIconData), true);
    CreateFile("AUTORUN", "INF", (const char *)AutoRunData, strlen(AutoRunData), true);
    CreateFile("README", "TXT", ReadMeData, strlen(ReadMeData));
    CreateFile("INDEX", "HTM", IndexHtmData, strlen(IndexHtmData));
    for (int bank = 0; bank < FLASH_MAX_ROM_BANKS; ++bank)
    {
        char bankName[9];
        sprintf(bankName, ROM_BANK_FOLDER_NAME, bank);
        BricoROMBankClusterNumber[bank] = CreateFile(bankName, "", NULL, 0, false, true);
    }

#ifdef MASS_STORAGE_DEBUG
    printf("Storage ready\n");
#endif
}

void MassStorageManager::Loop()
{
#ifdef USE_MASS_STORAGE

    // Time to eject the disk?
    if (!ejected && timeToWaitBeforeEjection != 0L && time_reached(timeToWaitBeforeEjection))
    {
#ifdef MASS_STORAGE_DEBUG
        printf("Ejecting volume\n");
        printf("Content before ejection:\n");
        PrintDirectoryListing();
#endif
        // Eject the disk
        update_us_since_boot(&timeToWaitBeforeEjection, 0L);
        timeToWaitBeforeInjection = from_us_since_boot(time_us_64() + MASS_STORAGE_REFRESH_INJECTION_DURATION_MS * 1000); // Microseconds
        tud_connect();
        // Eject the soft way (Windows)
        ejected = true;
        if (tud_msc_test_unit_ready_cb_count < 5)
        {
            // Eject the hard way (Linux/MacOS)
            tud_disconnect();
        }
    }
    // Time to (re)introduce the disk?
    else if (ejected && timeToWaitBeforeInjection != 0L && time_reached(timeToWaitBeforeInjection))
    {
#ifdef MASS_STORAGE_DEBUG
        printf("Reinjecting volume\n");
#endif
        // Inject the disk
        update_us_since_boot(&timeToWaitBeforeInjection, 0L);
        InitStorageContent();
        ejected = false;
        //if (tud_msc_test_unit_ready_cb_count < 5)
        {
            tud_msc_test_unit_ready_cb_count = 0;
            tud_connect();
        }
    }
#endif
    tud_task();
}

void MassStorageManager::PrintDirectoryListing()
{
    // Show free blocks available
    PrintDirectoryListing(1, 0);
    printf("Free RAM clusters available (%d bytes each): %d/%d\n", DISK_BLOCK_SIZE, GetFreeRAMClustersAvailable(), DISK_BLOCK_COUNT_RAM);
#ifdef MASS_STORAGE_DEBUG
    for (int i = 0; i < 16; ++i) DebugCluster(i);
#endif
}

#ifdef MASS_STORAGE_DEBUG
void MassStorageManager::DebugCluster(int clusterNumber)
{
    if (clusterNumber == 0)
    {
        printf("VFat cluster 0:\n");
        printf("Cluster types:\n");
        printf("\t? = Unknown\n");
        printf("\tS = System\n");
        printf("\tD = Directory\n");
        printf("\t* = Data (single data cluster)\n");
        printf("\t[ = Data (first of a chain of data clusters)\n");
        printf("\t- = Data (intermediate cluster un a chain of data clusters)\n");
        printf("\t] = Data (last of a chain of data clusters)\n");
        printf("=>VFat decoded content:\n");
        uint16_t *pVFat = (uint16_t *)VFAT;
        for (int i = 0; i < (FAT_ENTRIES_COUNT_TOTAL + 15) / 16; ++i)
        {
            for (int j = 0; j < 16; ++j)
            {
                if (i*16 + j < FAT_ENTRIES_COUNT_TOTAL)
                {
                    uint16_t fatEntry = ((uint16_t *)pVFat)[i*16+j];
                    int parentDirectoryCluster = 1;
                    int parentDirectoryEntryIndex, firstDataCluster;
                    ClusterType eClusterType = GetClusterInfo(i*16 + j, parentDirectoryCluster, parentDirectoryEntryIndex, firstDataCluster);
                    char culsterState;
                    switch (eClusterType)
                    {
                    case ClusterType::Unknown:
                        culsterState = '?';
                        break;
                    case ClusterType::System:
                        culsterState = 'S';
                        break;
                    case ClusterType::Directory:
                        culsterState = 'D';
                        break;
                    case ClusterType::DataSingle:
                        culsterState = '*';
                        break;
                    case ClusterType::DataFirst:
                        culsterState = '[';
                        break;
                    case ClusterType::DataIntermediate:
                        culsterState = '-';
                        break;
                    case ClusterType::DataLast:
                        culsterState = ']';
                        break;
                    default:
                        culsterState = ' ';
                        break;
                    }
                    printf("%c %04x  ", culsterState, (int)fatEntry);
                    if (FAT_REMOVE_CLUSTER_MARKERS(fatEntry) == 0x0000)
                    {
                        // Stop display and consider there is no valid cluster after this one (it might not always be true but makes the display much lighter)
                        printf("\n");
                        return;
                    }
                }
            }
            printf("\n");
        }
    }
    else
    {
        uint8_t *pClusterData;
        pClusterData = GetClusterMemoryLocation(clusterNumber);
        if (clusterNumber == 1 || pClusterData[0] == '.')
        {
            printf("Directory cluster #0x%03x:\n", clusterNumber);
            DebugPrettyPrintDirectoryBuffer(pClusterData);
        }
        else
        {
            printf("File cluster #0x%03x:\n", clusterNumber);
        }
        for (int i = 0; i < 16; ++i)
        {
            for (int j = 0; j < 32; ++j)
            {
                printf("%02x ", (int)pClusterData[i*32+j]);
            }
            printf("\n");
        }
    }
    printf("\n");
}
#endif

void MassStorageManager::PrintDirectoryListing(int dirCluster, int depth)
{
    // Local filename buffer
    uint8_t fileNameBuffer[256];

    // Point to the requested cluster
    uint8_t *pClusterData;
    pClusterData = GetClusterMemoryLocation(dirCluster);

    // Iterate through directory entries
    bool bLongFileNameAvalable = false;
    for (int entryNumber = 0; entryNumber < DIRECTORY_ENTRIES_COUNT && pClusterData[entryNumber * DIRECTORY_ENTRY_SIZE] != 0; ++entryNumber)
    {
        // Compute entry base address
        uint8_t *pEntryData = pClusterData + entryNumber * DIRECTORY_ENTRY_SIZE;

        // Read attribute flags
        uint8_t attributes = pEntryData[FAT_OFFSET_FILE_ATTRIBUTES];
        
        // Long filename part?
        if (attributes == 0x0f)
        {
            // Long file name element
            bLongFileNameAvalable = true;
        }
        else
        {
            // We will potentially have a name to display
            // Build file name
            if (bLongFileNameAvalable)
            {
                fileNameBuffer[0] = 0;
                BuildLongFileName(pEntryData, fileNameBuffer);
                bLongFileNameAvalable = false;
            }
            else
            {
                FormatFileName(pEntryData, fileNameBuffer);
            }

            // Volume name ?
            if (attributes & 0x08)
            {
                printf("Volume name: %s\n", fileNameBuffer);
            }
            // Deleted file?
            else if (fileNameBuffer[0] != 0xE5)
            {
                // Sub-directory?
                if (attributes & 0x10)
                {
                    // List directory content if not hidden and if not "." (the current one) or ".." (the parent one)
                    if (strcmp((char *)fileNameBuffer, ".") && strcmp((char *)fileNameBuffer, ".."))
                    {
                        FormatDepthIndentation(depth);
                        printf("%c%c%c%c <DIR>  %s\n",
                            attributes & 0x01 ? 'R' : '-',
                            attributes & 0x02 ? 'H' : '-',
                            attributes & 0x04 ? 'S' : '-',
                            attributes & 0x20 ? 'A' : '-',
                            fileNameBuffer);
                        int nextClusterIndex = pEntryData[FAT_OFFSET_FIRST_CLUSTER] + (pEntryData[FAT_OFFSET_FIRST_CLUSTER + 1] << 8);
                        PrintDirectoryListing(nextClusterIndex, depth + 1);
                    }
                }
                else
                // File
                {
                    FormatDepthIndentation(depth);
                    printf("%c%c%c%c %6d bytes %s\n",
                        attributes & 0x01 ? 'R' : '-',
                        attributes & 0x02 ? 'H' : '-',
                        attributes & 0x04 ? 'S' : '-',
                        attributes & 0x20 ? 'A' : '-',
                        pEntryData[FAT_OFFSET_FILE_SIZE] + (pEntryData[FAT_OFFSET_FILE_SIZE + 1] << 8) + (pEntryData[FAT_OFFSET_FILE_SIZE + 2] << 16) + (pEntryData[FAT_OFFSET_FILE_SIZE + 3] << 24), fileNameBuffer);
                }
            }
        }
    }
}

void MassStorageManager::BuildLongFileName(const uint8_t *pClusterData, uint8_t *szFileNameBuffer)
{
    int fileNameLength = strlen((char *)szFileNameBuffer);

    // Point to the first file name element
    const uint8_t * pClusterPtr = pClusterData;

    // Iterate through previous clusters
    do
    {
        // Move to the "next" cluster (the previous one, as the name if built upside down)
        pClusterPtr -= DIRECTORY_ENTRY_SIZE;

        // First part of name info
        for (int i = 0; i < 5; ++i)
        {
            szFileNameBuffer[fileNameLength++] = pClusterPtr[1 + i * 2];
        }

        // Second part of name info
        for (int i = 0; i < 6; ++i)
        {
            szFileNameBuffer[fileNameLength++] = pClusterPtr[14 + i * 2];
        }

        // Third part of name info
        for (int i = 0; i < 2; ++i)
        {
            szFileNameBuffer[fileNameLength++] = pClusterPtr[28 + i * 2];
        }
    } while ((*pClusterPtr & 0x40) == 0);
    
    // Terminate the string
    szFileNameBuffer[fileNameLength] = 0;
}

void MassStorageManager::FormatDepthIndentation(int depth)
{
    for (int i = 0; i < depth; ++i)
    {
        printf("   >   ");
    }
    printf(" ");
}

void MassStorageManager::FormatFileName(const uint8_t *pDirEntry, uint8_t *pTargetBuffer13Bytes)
{
    // Copy file name part
    int nextCharOffset;
    for (int i = 0; i < 8; ++i)
    {
        pTargetBuffer13Bytes[i] = pDirEntry[i];
    }

    // Remove trailing spaces
    for (nextCharOffset = 8; nextCharOffset > 0; --nextCharOffset)
    {
        if (pTargetBuffer13Bytes[nextCharOffset - 1] != ' ')
            break;
    }

    // Do we have an extension?
    if (pDirEntry[8] != ' ' && pDirEntry[9] != ' ' && pDirEntry[10] != ' ')
    {
        // Add dot if needed
        pTargetBuffer13Bytes[nextCharOffset++] = '.';
        
        // Add the extension
        for (int i = 8; i < 11 && pDirEntry[i] != ' '; ++i)
        {
            pTargetBuffer13Bytes[nextCharOffset++] = pDirEntry[i];
        }
    }

    // Add trailing 0 character
    pTargetBuffer13Bytes[nextCharOffset] = 0;
}

int32_t MassStorageManager::WriteHook(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize)
{
    if (ejected || timeToWaitBeforeEjection != 0L)
    {
        // Accept writes even if media has been ejected to prevent misdetections after forced ejection
        return bufsize;
    }
    if ( lba >= DISK_BLOCK_COUNT )
    {
        printf("Disk write operation out of bounds!");
        ejected = true;
        return -1;
    }

    if ( offset != 0 || bufsize != DISK_BLOCK_SIZE )
    {
        printf("Disk read operation size not supported");
        ejected = true;
        return -1;
    }

    // MBR requested?
    if (lba == 0)
    {
#ifdef MASS_STORAGE_DEBUG
        printf("Writing to MBR => not allowed\n");
#endif
        // Do nothing
        return bufsize;
    }
    else if (lba == 1)
    {
#ifdef MASS_STORAGE_DEBUG
    printf("Writing FAT (lba %d)\n", lba);
#endif
        WriteFAT(buffer);
    }
    else
    {
        // Check if the targetcluster is a ROM directory
        int iClusterIndex = lba - 1;

        WriteCluster(iClusterIndex, buffer);
    }

    return (int32_t) bufsize;
}

void MassStorageManager::FileCopyComplete(int parentDirectoryCluster, int parentDirectoryEntryIndex)
{
#ifdef MASS_STORAGE_DEBUG
    printf("End of ROM transfert\n");
#endif

    // Byteswap if needed and handle NeoDiag image
    FinalizeResult result = ROMImageManager::FinalizeReceivedImage();

    // Determine if we should store the image in a bank
    // Iterate through flash banks
    int storedBankNumber = -1;
    for (int bankNumber = 0; bankNumber < FLASH_MAX_ROM_BANKS; ++bankNumber)
    {
        // Is our parent in the bank clusters list?
        if (parentDirectoryCluster == BricoROMBankClusterNumber[bankNumber])
        {
#ifdef MASS_STORAGE_DEBUG
            printf("Sending image to flash bank %d\n", bankNumber);
#endif
            ROMImageManager::SendToFlash(bankNumber);
            storedBankNumber = bankNumber;
        }

    }

    // Report to the console
    uint8_t *pDirectoryEntryData = GetClusterMemoryLocation(parentDirectoryCluster) + parentDirectoryEntryIndex * DIRECTORY_ENTRY_SIZE;
    uint32_t fileSize = *(uint32_t *)(pDirectoryEntryData + FAT_OFFSET_FILE_SIZE);
    char szBank[24] = "";
    if (storedBankNumber >= 0)
    {
        sprintf(szBank, ", stored in bank %d", storedBankNumber);
    }
    CommandManager::PrintAsyncMessage("ROM received on mass storage: %lu bytes%s%s%s", (unsigned long)fileSize,
        result.byteSwapped ? ", byteswapped" : "", result.neoDiag ? ", NeoDiag" : "", szBank);
    
#ifdef MASS_STORAGE_DEBUG
    printf("=> Program volume ejection\n");
#endif

    EjectInject();

    // Force reset on the remote system
    PIOManager::ForceWatchdogReset();
}

int32_t MassStorageManager::ReadHook(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize)
{
    if (ejected)
    {
#ifdef MASS_STORAGE_DEBUG
        printf("ReadHook => Inform that the disk has been ejected\n");
        return -1;
#endif
    }
    // Out of authorized space!
    if ( lba >= DISK_BLOCK_COUNT )
    {
        printf("Disk read operation out of bounds!");
        ejected = true;
        return -1;
    }

    if ( lba != 0 && (offset != 0 || bufsize != DISK_BLOCK_SIZE) )
    {
        printf("Disk read operation size not supported");
        ejected = true;
        return -1;
    }

    // MBR requested?
    if (lba == 0)
    {
#ifdef MASS_STORAGE_DEBUG
    printf("Reading MBR (lba %d)\n", lba);
#endif
        // Return MBR
        ReadMBR(buffer);
    }
    else if (lba == 1)
    {
#ifdef MASS_STORAGE_DEBUG
    printf("Reading FAT (lba %d)\n", lba);
#endif
        ReadFAT(buffer);
    }
    else
    {
#ifdef MASS_STORAGE_DEBUG
    printf("Reading cluster %d (lba %d)\n", lba - 1, lba);
#endif
        uint8_t const* addr = GetClusterMemoryLocation(lba - 1) + offset;
        memcpy(buffer, addr, bufsize);
    }

    return (int32_t) bufsize;
}

void MassStorageManager::ReadMBR(void* buffer)
{
    // Build MBR
    
    // Copy interesting part
    memcpy(buffer, MBR, 62);
    
    // Clear 512 - 62 - 2 bytes
    memset(((uint8_t *)buffer) + 62, 0, 512 - 62 - 2);
    
    // Add valid boot sector signature
    ((uint8_t *)buffer)[512 - 2] = 0x55;
    ((uint8_t *)buffer)[512 - 1] = 0xAA;
}

void MassStorageManager::ReadFAT(void* buffer)
{
    // Return the VFAT, removing the MSB (ROM cluster marker)
    // Iterate through VFAT records (16 bits    aligned), processing them 2 by 2
    // FAT records are 12 bits aligned!
    uint8_t *pFat = (uint8_t *)buffer;
    uint16_t *pVFat = (uint16_t *)VFAT;
    for (int iFatEntry = 0; iFatEntry < FAT_ENTRIES_COUNT_IN_BLOCK; iFatEntry += 2)
    {
        pFat[0] = (uint8_t)FAT_REMOVE_CLUSTER_MARKERS(pVFat[0]);
        pFat[1] = (uint8_t)((FAT_REMOVE_CLUSTER_MARKERS(pVFat[0]) >> 8) & 0x0f);
        pFat[1] |= (uint8_t)(FAT_REMOVE_CLUSTER_MARKERS(pVFat[1]) << 4);
        pFat[2] = (uint8_t)(FAT_REMOVE_CLUSTER_MARKERS(pVFat[1]) >> 4);
        pFat += 3;
        pVFat += 2;
    }
    // Clear byte to go to 512 (not a multiple of 3)
    ((uint8_t *)buffer)[510] = 0;
    ((uint8_t *)buffer)[511] = 0;
}

void MassStorageManager::WriteFAT(void* buffer)
{
    // Send host FAT to VFAT
    // Iterate through VFAT records (16 bits aligned), processing them 2 by 2
    // FAT records are 12 bits aligned!
    uint8_t *pFat = (uint8_t *)buffer;
    uint16_t *pVFat = (uint16_t *)VFAT;
    for (int iFatEntry = 0; iFatEntry < FAT_ENTRIES_COUNT_IN_BLOCK; iFatEntry += 2)
    {
        uint16_t FAT0, FAT1;
        FAT0 = *pFat;
        FAT0 |= ((*(pFat+1) & 0x0f)) << 8;
        FAT1 = *(pFat+1) >> 4;
        FAT1 |= *(pFat+2) << 4;

        // Reinject the cluster markers
        pVFat[0] = FAT0 | (pVFat[0] & FAT_CLUSTER_MARKERS);
        pVFat[1] = FAT1 | (pVFat[1] & FAT_CLUSTER_MARKERS);

        pFat += 3;
        pVFat += 2;
    }
#ifdef MASS_STORAGE_DEBUG
        DebugCluster(0);
#endif
}

uint16_t MassStorageManager::CreateFile(const char *fileName, const char *fileExtension, const char *fileContent, int fileContentSize, bool hidden, bool isFolder, uint16_t currentFolder, uint16_t parentFolderCluster)
{
    uint16_t iClusterNumber = 0;

    if (fileContentSize > DISK_BLOCK_SIZE)
    {
        printf("Small file size is too large, it will be truncated!");
        fileContentSize = DISK_BLOCK_SIZE;
    }

    int fileIndex = 0;

    // Find an empty space in the root directory page
    uint8_t *directoryCluster = GetClusterMemoryLocation(currentFolder);
    for (; fileIndex < DIRECTORY_ENTRIES_COUNT && directoryCluster[fileIndex * DIRECTORY_ENTRY_SIZE] != 0; ++fileIndex) {}

    // Found an empty space?
    if (fileIndex != DIRECTORY_ENTRIES_COUNT)
    {
        // Start of file descriptor
        uint8_t *pFileDescriptor = directoryCluster + fileIndex * DIRECTORY_ENTRY_SIZE;

        // Clear descriptor
        memset(pFileDescriptor, 0, DIRECTORY_ENTRY_SIZE);

        // File name
        int nameLength = strlen(fileName);
        for (int i = 0; i < 8; i++)
        {
            pFileDescriptor[i] =    i < nameLength ? toupper(fileName[i]) : ' ';
        }

        // Extension
        nameLength = strlen(fileExtension);
        for (int i = 0; i < 3; i++)
        {
            pFileDescriptor[8+i] =    i < nameLength ? toupper(fileExtension[i]) : ' ';
        }

        // Attributes
        pFileDescriptor[11] = isFolder? 0x10 : 0x01;
        if (hidden)
        {
            pFileDescriptor[11] |= 0x02;
        }

        // Date and time (of compilation)
        pFileDescriptor[14] = (uint8_t)TO_FAT_TIME(BUILD_DATE_TIME_HOURS, BUILD_DATE_TIME_MINUTES, BUILD_DATE_TIME_SECONDS);
        pFileDescriptor[15] = (uint8_t)(TO_FAT_TIME(BUILD_DATE_TIME_HOURS, BUILD_DATE_TIME_MINUTES, BUILD_DATE_TIME_SECONDS) >> 8);
        pFileDescriptor[16] = (uint8_t)TO_FAT_DATE(BUILD_DATE_TIME_YEAR, BUILD_DATE_TIME_MONTH, BUILD_DATE_TIME_DAY);
        pFileDescriptor[17] = (uint8_t)(TO_FAT_DATE(BUILD_DATE_TIME_YEAR, BUILD_DATE_TIME_MONTH, BUILD_DATE_TIME_DAY) >> 8);
        pFileDescriptor[22] = pFileDescriptor[14];
        pFileDescriptor[23] = pFileDescriptor[15];
        pFileDescriptor[24] = pFileDescriptor[16];
        pFileDescriptor[25] = pFileDescriptor[17];

        // Cluster number
        if (!isFolder || fileName[0] != '.')
        {
            // File or non special folder
            iClusterNumber = GetNextFreeClusterIndex();

            // Reserve the cluster in VFAT
            VFAT[iClusterNumber] = 0x0fff | FAT_CLUSTER_MARKER_FILLED; // Only one cluster supported for small file
        }
        else
        {
            // Special folder
            if (fileName[1] == '.')
            {
                    // .. folder
                    // Point to parent cluster
                    iClusterNumber = parentFolderCluster;
            }
            else
            {
                // . folder
                // Point to self
                iClusterNumber = currentFolder;
            }
        }
        pFileDescriptor[26] = iClusterNumber & 0xff;
        pFileDescriptor[27] = (iClusterNumber >> 8) & 0xff;

        // File content
        if (!isFolder)
        {
        // File size (4 bytes, last 2 are 0)
            pFileDescriptor[28] = fileContentSize & 0xff;
            pFileDescriptor[29] = (fileContentSize >> 8) & 0xff;

            // Inject file content
            uint8_t * pTargetLocation = GetClusterMemoryLocation(iClusterNumber);
            memcpy(pTargetLocation, fileContent, fileContentSize);
        }

        // Create special folder files in the new folder cluster
        if (isFolder && fileName[0] != '.')
        {
            CreateFile(".", "", NULL, 0, false, true, iClusterNumber, currentFolder);
            CreateFile("..", "", NULL, 0, false, true, iClusterNumber, currentFolder);
        }
    }

    return iClusterNumber;
}

uint16_t MassStorageManager::GetNextFreeClusterIndex()
{
    // Iterate through the VFAT and find the next empty space
    for (int i = 2; i < FAT_ENTRIES_COUNT_TOTAL; ++i)    
    {
        if (VFAT[i] == 0)
        {
            return i;
        }
    }
    printf("No free cluster found!");
    return 2;
}

uint8_t *MassStorageManager::GetClusterMemoryLocation(int clusterIndex)
{
    // Iterate through clusters to find their index either in RAM or in ROM area
    int iClustersInROM = 0;
    int iClustersInRAM = 0;
    // VFAT indexing starts at 1
    for (int i = 1; i < clusterIndex; ++i)
    {
        if (VFAT[i] & FAT_CLUSTER_MARKER_ROM)
        {
            ++iClustersInROM;
        }
        else
        {
            ++iClustersInRAM;
        }
    }
    // Compute the base address based on the cluster marker
    uint8_t *pClusterBase;
    uint8_t *pClusterLocation;
    if ((VFAT[clusterIndex] & FAT_CLUSTER_MARKER_ROM))
    {
        pClusterBase = (uint8_t *)RAM_ADDRESS_IMAGE_IN_RAM;
        pClusterLocation = pClusterBase + DISK_BLOCK_SIZE * iClustersInROM;
    }
    else
    {
        pClusterBase = (uint8_t *)ClustersContent;
        pClusterLocation = pClusterBase + DISK_BLOCK_SIZE * iClustersInRAM;
    }

    // Return requested cluster pointer
    return pClusterLocation;
}

int MassStorageManager::GetFreeRAMClustersAvailable()
{
    // Iterate through VFat and count RAM allocated clusters
    int clustersAvailable = DISK_BLOCK_COUNT_RAM; 
    for (int i = 0; i < DISK_BLOCK_COUNT; ++i)
    {
        if ((VFAT[i] & FAT_CLUSTER_MARKER_ROM) == 0 && FAT_REMOVE_CLUSTER_MARKERS(VFAT[i]) != 0)
            --clustersAvailable;
    }
    return clustersAvailable;
}

#ifdef MASS_STORAGE_DEBUG
void MassStorageManager::DebugPrettyPrintDirectoryBuffer(uint8_t *buffer)
{
    char fileNameBuffer[13];
    // Iterate through entries and show files, attributes, first cluster and size
    for (int i = 0; i < DIRECTORY_ENTRIES_COUNT; ++i)
    {
        uint8_t *row = buffer + i * DIRECTORY_ENTRY_SIZE;
        if (*row == 0)
        {
            break;
        }
        FormatFileName(row, (uint8_t *)fileNameBuffer);
        uint8_t clusterAttrib = row[0x0b];
        if (clusterAttrib == 0x0f)
        {
            printf("%02x:           #%d attr 0x%02x long file name part\n", i, row[0] & ~0x40, (int)clusterAttrib);
        }
        else if (clusterAttrib & 0x10)
        {
            printf("%02x: %12s attr 0x%02x cluster 0x%02x sub-directory\n", i, fileNameBuffer, (int)clusterAttrib, (int)((uint16_t *)row)[13]);
        }
        else
        {
            printf("%02x: %12s attr 0x%02x cluster 0x%02x size %d bytes\n", i, fileNameBuffer, (int)clusterAttrib, (int)((uint16_t *)row)[13], ((uint32_t *)row)[7]);
        }

    }    
    
}
#endif

MassStorageManager::ClusterType MassStorageManager::GetClusterInfo(int clusterIndex, int &parentDirectoryCluster, int &parentDirectoryEntryIndex, int &firstDataCluster)
{
    // Everything to 0 by default
    parentDirectoryEntryIndex = 0;
    firstDataCluster = 0;

    // FAT cluster?
    if (clusterIndex == 0)
    {
        return ClusterType::System;
    }
    else if (clusterIndex == 1)
    {
        return ClusterType::Directory;
    }
    else if (FAT_REMOVE_CLUSTER_MARKERS(VFAT[clusterIndex]) == 0x0000)
    {
        return ClusterType::Unknown;
    }
    else
    {
        // Find my potential big brother in the VFAT
        firstDataCluster = clusterIndex;
        while (true)
        {
            int iNewBrother = firstDataCluster;
            for (int iPotentialBrother = firstDataCluster - 1; iPotentialBrother > 0; --iPotentialBrother)
            {
                // Does the cluster point to my brother?
                if (FAT_REMOVE_CLUSTER_MARKERS(VFAT[iPotentialBrother]) == firstDataCluster)
                {
                    // Yes, I have an elder brother!
                    iNewBrother = iPotentialBrother;
                    break;
                }
            }
            if (firstDataCluster == iNewBrother)
            {
                break;
            }
            firstDataCluster = iNewBrother;
        }

        // Now find my first cluster recursively in the directory tree (consider 1 block directory records)
        if (FindClusterDirectoryEntry(firstDataCluster, parentDirectoryCluster, parentDirectoryEntryIndex))
        {
            // Found the record in a directory, somewhere
            uint8_t *directoryCluster = GetClusterMemoryLocation(parentDirectoryCluster);
            // Start of file descriptor
            uint8_t *pFileDescriptor = directoryCluster + parentDirectoryEntryIndex * DIRECTORY_ENTRY_SIZE;
            // Check if this is a directory cluster
            if (pFileDescriptor[0x0b] & 0x10)
            {
                // We have a directory
                return ClusterType::Directory;
            }
            else
            {
                // We have a file
                if (firstDataCluster == clusterIndex)
                {
                    if ((VFAT[clusterIndex] & 0xff0) == 0xff0)
                    {
                        return ClusterType::DataSingle;
                    }
                    else
                    {
                        return ClusterType::DataFirst;
                    }
                }
                else if ((VFAT[clusterIndex] & 0xff0) == 0xff0)
                {
                    return ClusterType::DataLast;
                }
                else
                {
                    return ClusterType::DataIntermediate;
                }
            }
        }
        else
        {
            // Did not find the record anywhere in the directory entries
            parentDirectoryCluster = 0;
            return ClusterType::Unknown;
        }
    }
}

bool MassStorageManager::FindClusterDirectoryEntry(int clusterIndex, int &parentDirectoryCluster, int &parentDirectoryEntryIndex)
{
    // Iterate through the directory content
    uint8_t * directoryCluster = GetClusterMemoryLocation(parentDirectoryCluster);
    ;
    for (int entryIndex = 0; entryIndex < DIRECTORY_ENTRIES_COUNT; ++entryIndex)
    {
        // Compute entry base address
        uint8_t * entryData = directoryCluster + entryIndex * DIRECTORY_ENTRY_SIZE;

        // Check if the entry is filled
        switch (entryData[0x00])
        {
            case 0x00:
                // The entry is empty, nothing to do with it
                break;
            case '.':
                // The entry is either "." or "..", skip it
                break;
            default:
                // The entry is either a file or a directory
                // Compare its first cluster with ours
                int entryFirstCluster = *((uint16_t *)(entryData+ 0x1a));
                if (entryFirstCluster == clusterIndex)
                {
                    // We are on the correct entry
                    parentDirectoryEntryIndex = entryIndex;
                    return true;
                }
                else if (entryData[0x0b] & 0x10) // Check if the entry is a directory
                {
                    // The entry is a directory, we can perform a recursive search
                    bool found = FindClusterDirectoryEntry(clusterIndex, entryFirstCluster, entryIndex);
                    if (found)
                    {
                        parentDirectoryCluster = entryFirstCluster;
                        parentDirectoryEntryIndex = entryIndex;
                        return true;
                    }
                }
        }
    }

    // Entry not found
    return false;
}

void MassStorageManager::WriteCluster(int clusterIndex, void* buffer)
{
#ifdef MASS_STORAGE_DEBUG
    printf("Writing cluster 0x%03x (lba %d, cluster state before write: 0x%04x)\n", clusterIndex, clusterIndex + 1, VFAT[clusterIndex]);
    DebugCluster(0);
#endif

    // Determine if the file transfer is over and file info
    bool bTransferComplete = false;
    int transferedDirectoryCluster = 0;
    int transferedDirectoryEntryIndex = 0;

    // Try to determine the entry type associated to the given cluster
    int myParentDirectoryCluster = 1;
    int myParentDirectoryEntryIndex, myFirstDataCluster;
    ClusterType myClusterType = GetClusterInfo(clusterIndex, myParentDirectoryCluster, myParentDirectoryEntryIndex, myFirstDataCluster);
    #ifdef MASS_STORAGE_DEBUG
        printf("Writing cluster with type %d refering to entry index %d in directory %d\n", (int)myClusterType, myParentDirectoryEntryIndex, myParentDirectoryCluster);
    #endif

    // Is current cluster already marked as a ROM cluster?
    if ((VFAT[clusterIndex] & FAT_CLUSTER_MARKER_ROM) == 0)
    {
        if (myClusterType != ClusterType::Directory && myClusterType != ClusterType::System)
        {
            // We are not creating a directory or system file (or at least we don't know, yet)
            // Let's try to get information about the attached file entry in the directory
            bool isFileIdentified = false;
            if (myParentDirectoryCluster > 0)
            {
                // Found a directory entry
                // Determine if the file is a regular file (not a system or hidden one)
                uint8_t *pDirectoryEntryData = GetClusterMemoryLocation(myParentDirectoryCluster) + myParentDirectoryEntryIndex * DIRECTORY_ENTRY_SIZE;
                uint8_t fileAttributes = pDirectoryEntryData[0x0b]; // 0x0b is the file attributes offset in directory structure

                // Regular file?
                uint8_t *pDirectoryEntry;
                
                if (((fileAttributes & ~0x20) == 0)                 // 0x20 is the Archive attribute. Other values point to special file / directory / hidden file / read-only.
                    &&
                    (
                        (myParentDirectoryCluster == 1)
                        ||
                        ((pDirectoryEntry = FindDirectoryClusterData(myParentDirectoryCluster)) != NULL)
                        && ((pDirectoryEntry[0x0b] & 0x07) == 0)
                    )
                )
                {
                    // Now we know the file the cluster belongs to!
                    isFileIdentified = true;
#ifdef MASS_STORAGE_DEBUG
                    char fileNameBuffer[13];
                    FormatFileName(pDirectoryEntryData, (uint8_t *)fileNameBuffer);
                    printf("The cluster belongs to file %s with attributes 0x%02x and size %d bytes\n", fileNameBuffer, fileAttributes, *(uint32_t *)(pDirectoryEntryData + 0x1c));
#endif

                    // This is a regular file
                    // Mark the cluster as a ROM emulation cluster
                    VFAT[clusterIndex] |= FAT_CLUSTER_MARKER_ROM;

    #ifdef MASS_STORAGE_DEBUG
                    printf("Marking the current cluster as a ROM emulation cluster\n");
    #endif
                    // See if we have a cluster count for this file
                    // We can read a DWORD here, as the buffer is aligned with directive CFG_TUSB_MEM_ALIGN    __attribute__ ((aligned(4)))
                    uint16_t clusterCountDirectory = (uint16_t)(((*(uint32_t *)(pDirectoryEntryData + 0x1c)) + 511) / DISK_BLOCK_SIZE);
                    // Count how many clusters we already have starting with the current one
                    uint16_t clusterCountUntilNow = 1 + CountPreviousClusters(clusterIndex);  // +1 = current cluster
    #ifdef MASS_STORAGE_DEBUG
                    printf("This is cluster %d/%d for this file.\n", clusterCountUntilNow, clusterCountDirectory);
    #endif
                    // Now determine if we have finished the file transfer after this cluster
                    // We can call the copy complete only if the file size had been given
                    if (clusterCountDirectory != 0 && (clusterCountDirectory == clusterCountUntilNow))
                        bTransferComplete = true;
                    transferedDirectoryCluster = myParentDirectoryCluster;
                    transferedDirectoryEntryIndex = myParentDirectoryEntryIndex;
                }
                else
                {
                    // We have a system or read-only file => we consider they should stay in RAM and we don't change anything
#ifdef MASS_STORAGE_DEBUG
                    printf("Potential system or read-only file. File attributes: 0x%02x\n", fileAttributes);
#endif
                }
            }
            
            // Have we been able to recognize the cluster's file?
            if (!isFileIdentified)
            {
                // We don't konw this cluster's nature, yet
                // Determine if the previous cluster's origin was also unknown
                // If it is, then we will conclude that we have the first 2 parts of a ROM cluster
                // We proceed only if the ROM marker is not already present on this file
                int prevClusterIndex = clusterIndex - 1;
                if ((VFAT[prevClusterIndex] & FAT_CLUSTER_MARKER_ROM) != 0)
                {
                    // Previous cluster is marked as a ROM cluster
                    // Consider we are also a ROM cluster
                    VFAT[clusterIndex] |= FAT_CLUSTER_MARKER_ROM;
#ifdef MASS_STORAGE_DEBUG
                    printf("Previous cluster is marked as a ROM cluster. Consider we are also a ROM cluster\n");
#endif
                }
                else
                {
                    // Previous cluster is not marked as a ROM cluster (yet?)
                    /*int prevClusterDirectoryCluster = 1;    
                    int prevClusterDirectoryEntryIndex, prevClusterFirstDataCluster;
                    ClusterType prevClusterType = GetClusterInfo(prevClusterIndex, prevClusterDirectoryCluster, prevClusterDirectoryEntryIndex, prevClusterFirstDataCluster);
                    if (prevClusterType != ClusterType::Directory && prevClusterType != ClusterType::System && prevClusterDirectoryCluster == 0)
                    {
                        // The previous cluster was also in an unknown state
                        // We move both of them to their new location
#ifdef MASS_STORAGE_DEBUG
                        printf("More that 2 consecutive unknown state clusters found. Switching them to ROM emulation mode\n");
                        printf("=> Copy cluster %d to the ROM area\n", prevClusterIndex);
#endif
                        // Start with the previous cluster
                        uint8_t * prevLocation = GetClusterMemoryLocation(prevClusterIndex);
                        VFAT[prevClusterIndex] |= FAT_CLUSTER_MARKER_ROM;
                        uint8_t * newLocation = GetClusterMemoryLocation(prevClusterIndex);
                        memcpy(newLocation, prevLocation, DISK_BLOCK_SIZE);

                        // The current one's data will be moved by the standard write code once we have set the correct ROM flag
                        VFAT[clusterIndex] |= FAT_CLUSTER_MARKER_ROM;
                    }
                    else*/
                    {
#ifdef MASS_STORAGE_DEBUG
                        printf("Checking bytes @0x0004-0x0007: %02x %02x %02x %02x\n", ((uint8_t *)buffer)[4], ((uint8_t *)buffer)[5], ((uint8_t *)buffer)[6], ((uint8_t *)buffer)[7]);
#endif
                        uint16_t resetVector = ((uint16_t *)buffer)[2];
                        if (resetVector == 0xc000 || resetVector == 0x00c0)
                        {
#ifdef MASS_STORAGE_DEBUG
                            printf("Detected start of ROM file!!!\n");
#endif
                            VFAT[clusterIndex] |= FAT_CLUSTER_MARKER_ROM;
                        }
                    }
                }
            }
        }
        else if (myClusterType == ClusterType::Directory)
        {
#ifdef MASS_STORAGE_DEBUG
            DebugPrettyPrintDirectoryBuffer((uint8_t *)buffer);
#endif
            // A directory cluster is being updated
            // We need to compare it with its previous version
            uint8_t * pPreviousDirectoryEntry = GetClusterMemoryLocation(clusterIndex);
            uint8_t * pNewDirectoryEntry = (uint8_t *)buffer;
            for (int iEntryIndex = 0; iEntryIndex < DIRECTORY_ENTRIES_COUNT; ++iEntryIndex)
            {
                uint32_t fileSizePrevious = *(uint32_t *)(pPreviousDirectoryEntry + FAT_OFFSET_FILE_SIZE);
                uint32_t fileSizeNew = *(uint32_t *)(pNewDirectoryEntry + FAT_OFFSET_FILE_SIZE);
                uint8_t fileAttributes = pNewDirectoryEntry[FAT_OFFSET_FILE_ATTRIBUTES];
                // Check if the file is... a regular file and if its size has changed
                if ((fileAttributes & ~0x20) == 0 && fileSizeNew != fileSizePrevious)
                {
                    // The file size changed
                    // See of we have any clusters with data for this file
                    uint16_t firstClusterIndex = *(uint16_t *)(pNewDirectoryEntry + 0x1a);
                    if (firstClusterIndex != 0 && (VFAT[firstClusterIndex] & FAT_CLUSTER_MARKER_FILLED) && IsValidFileExtension((const char *)pNewDirectoryEntry + 8))
                    {
                        // We already have some data in the cluster
                        // Consider this is a final update of the directory after all clusters have been transfered
                        bTransferComplete = true;
                        transferedDirectoryCluster = clusterIndex;
                        transferedDirectoryEntryIndex = iEntryIndex;
    #ifdef MASS_STORAGE_DEBUG
                        char fileNameBuffer[13];
                        FormatFileName(pNewDirectoryEntry, (uint8_t *)fileNameBuffer);
                        printf("Directory update detection with data clusters already updated. Consider the file transfer is complete for file %s (%d bytes => %d bytes)\n", fileNameBuffer, fileSizePrevious, fileSizeNew);
    #endif
                    }

                    break;
                }

                pPreviousDirectoryEntry += DIRECTORY_ENTRY_SIZE;
                pNewDirectoryEntry += DIRECTORY_ENTRY_SIZE;
            }
        }
    }

    // Now finally copy the cluster data to memory
    uint8_t* addr = GetClusterMemoryLocation(clusterIndex);
    memcpy(addr, buffer, DISK_BLOCK_SIZE);

    // And mark the cluster as filled
    VFAT[clusterIndex]|= FAT_CLUSTER_MARKER_FILLED;

    // Can we end the transfer?
    if (bTransferComplete && transferedDirectoryCluster != 0)
    {
        FileCopyComplete(transferedDirectoryCluster, transferedDirectoryEntryIndex);
    }
}

bool MassStorageManager::IsValidFileExtension(const char *extension)
{
    return
        (strncmp(extension, "BIN", 3) == 0)
        || (strncmp(extension, "BIO", 3) == 0)
        || (strncmp(extension, "ROM", 3) == 0);
}

int MassStorageManager::CountPreviousClusters(int clusterIndex)
{
    int totalCount = 0;

    for (;;)
    {
        if (FAT_REMOVE_CLUSTER_MARKERS(VFAT[clusterIndex-1]) == clusterIndex)
        {
            ++totalCount;
            clusterIndex--;
        }
        else
        {
            break;
        }
    }

    return totalCount;
}

uint8_t * MassStorageManager::FindDirectoryClusterData(int directoryClusterIndex)
{
    if (directoryClusterIndex > 1)
    {
        uint8_t * pMyClusterData = GetClusterMemoryLocation(directoryClusterIndex);
        if (pMyClusterData != NULL)
        {
            uint16_t parentClusterNumber = (uint16_t)((uint16_t *)(pMyClusterData + DIRECTORY_ENTRY_SIZE + 26))[0];
            if (parentClusterNumber > 0)
            {
                uint8_t * pMyParentClusterData = GetClusterMemoryLocation(parentClusterNumber);
                for (int i = 0; i < DIRECTORY_ENTRIES_COUNT; ++i)
                {
                    uint8_t * myClusterEntry = pMyClusterData + DIRECTORY_ENTRY_SIZE * i;
                    if (((uint16_t *)(myClusterEntry+ 26))[0] == directoryClusterIndex)
                        return myClusterEntry;
                }
            }
        }
    }
    return NULL;
}