#ifndef IMAGEINFO_H
#define IMAGEINFO_H

#include "common/treemodel.h"
#include "common/ffsparser.h"
#include "common/descriptor.h"

#define OUTPUT_MODE_DESCRIPTION 1
#define OUTPUT_MODE_CAPSULE 2
#define OUTPUT_MODE_IMAGE 4
#define OUTPUT_MODE_FILE_PEI_CORE 8
#define OUTPUT_MODE_FILE_PEI 16
#define OUTPUT_MODE_FILE_DXE_CORE 32
#define OUTPUT_MODE_FILE_DXE 64
#define OUTPUT_MODE_FILE_ALL (OUTPUT_MODE_FILE_PEI | OUTPUT_MODE_FILE_PEI_CORE | OUTPUT_MODE_FILE_DXE | OUTPUT_MODE_FILE_DXE_CORE)
#define OUTPUT_MODE_BG 128
#define OUTPUT_MODE_FULL 1023

#define HexAndDecView(value) std::hex << value << "h (" <<std::dec << value << ")"
#define HexView(value) std::hex << value << "h"


struct INFO_CAPSULE
{
    std::string name;
    std::string guid;
    UINT32 base;
    UINT32 size;
};

struct INFO_UEFI_IMAGE
{
    UINT32 base;
    UINT32 size;
};

struct REGION_INTEL_IMAGE {
    UINT8  type;
    UINT32 base;
    UINT32 offset;
    UINT32 size;
    friend bool operator< (const REGION_INTEL_IMAGE& lhs, const REGION_INTEL_IMAGE& rhs) { return lhs.offset < rhs.offset; }
};

struct INFO_INTEL_IMAGE_DESCRIPTOR {
    UINT8 version;
    UINT32 base;
    UINT32 size;
    union
    {
        struct MASTER_SECTION {
            UINT8 BiosRead;
            UINT8 BiosWrite;
            UINT8 MeRead;
            UINT8 MeWrite;
            UINT8 GbeRead;
            UINT8 GbeWrite;
        } masterSection;

        struct MASTER_SECTION_V2 {
            UINT32 BiosRead : 12;
            UINT32 BiosWrite : 12;
            UINT32 MeRead : 12;
            UINT32 MeWrite : 12;
            UINT32 GbeRead : 12;
            UINT32 GbeWrite : 12;
            UINT32 EcRead : 12;
            UINT32 EcWrite : 12;
        } masterSectionV2;

    };
};

struct INFO_INTEL_IMAGE
{
    UINT32 base;
    UINT32 size;
    INFO_INTEL_IMAGE_DESCRIPTOR descriptor;
    std::vector<REGION_INTEL_IMAGE> vRegions;
};

struct INFO_FILE
{
    int type;
    std::string name;
    std::string guid;
    UINT32 base;
    UINT64 dataAddress;
    UINT32 attributes;
    UINT32 size;
    std::string headerChecksum;
    std::string dataChecksum;
};


class ImageInfo
{
public:
    ImageInfo(UByteArray& inputBuffer);
    ~ImageInfo() {};
    void calculateBufferCRC();
    
    USTATUS explore();
    USTATUS exploreTopSections(const UModelIndex& index);
    USTATUS exploreFileTypeRecursive(const UModelIndex& index);

    USTATUS parseCapsule(const UModelIndex& index);
    USTATUS parseIntelImage(const UModelIndex& index);
    USTATUS parseUefiImage(const UModelIndex& index);
    INFO_FILE parseFileType(const UModelIndex& index);

    USTATUS compareWithAnother(ImageInfo& anotherImage);

    USTATUS checkProtectedRegions();

    void printSecurityInfo();

    void infoOutput(std::ostream& outputStream, UINT16 mode);
    bool readFromFile();
    bool writeToFile();

private:
    UByteArray openedImage;
    TreeModel model;
    FfsParser ffsParser;

    UINT32 crc;
    UINT32 sizeFullImage;
    UINT32 sizeFullFile;

    bool isCapsule;
    bool isIntelImage;
    bool isBootGuard;

    INFO_CAPSULE infoCapsule;
    INFO_UEFI_IMAGE infoUefiImage;
    INFO_INTEL_IMAGE infoIntelImage;
    std::vector<INFO_FILE> vInfoFile;
    std::string infoBootGuard;
};

#endif // !IMAGEINFO_H