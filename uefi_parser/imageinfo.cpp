#include "imageinfo.h"
#include "utilities.h"
#include "common/types.h"
#include "common/filesystem.h"
#include "common/ffs.h"
#include "common/utility.h"
#include "common/descriptor.h"

#include "nlohmann/json.hpp"
using json = nlohmann::json;
using ordered_json = nlohmann::ordered_json;

#include <iostream>
#include <string>
#include <iomanip>
#include <algorithm>
#include <sstream>


ImageInfo::ImageInfo(UByteArray& inputBuffer) : openedImage(inputBuffer), model(), ffsParser(&model)
{
    crc = (UINT32)crc32(0, (const UINT8*)openedImage.constData(), (uInt)openedImage.size());
    sizeFullFile = openedImage.size();
    isCapsule = false;
    isIntelImage = false;
    isBootGuard = false;
};

void ImageInfo::calculateBufferCRC()
{
    crc = (UINT32)crc32(0, (const UINT8*)openedImage.constData(), (uInt)openedImage.size());
};

USTATUS ImageInfo::parseCapsule(const UModelIndex& index)
{
    UByteArray capsule = model.header(index);
    UINT32 capsuleHeaderSize = 0;
    // Check buffer for being normal EFI capsule header
    if (capsule.startsWith(EFI_CAPSULE_GUID)
        || capsule.startsWith(EFI_FMP_CAPSULE_GUID)
        || capsule.startsWith(INTEL_CAPSULE_GUID)
        || capsule.startsWith(LENOVO_CAPSULE_GUID)
        || capsule.startsWith(LENOVO2_CAPSULE_GUID)) {

        const EFI_CAPSULE_HEADER* capsuleHeader = (const EFI_CAPSULE_HEADER*)capsule.constData();
        infoCapsule.name = std::string("UEFI capsule");
        infoCapsule.guid = std::string(guidToUString(capsuleHeader->CapsuleGuid, false).toLocal8Bit());
        infoCapsule.base = model.base(index);
        infoCapsule.size = capsule.size();
        sizeFullImage = capsuleHeader->CapsuleImageSize - capsuleHeaderSize;
    }
    // Check buffer for being Toshiba capsule header
    else if (capsule.startsWith(TOSHIBA_CAPSULE_GUID)) {
        const TOSHIBA_CAPSULE_HEADER* capsuleHeader = (const TOSHIBA_CAPSULE_HEADER*)capsule.constData();
        infoCapsule.name = std::string("Toshiba capsule");
        infoCapsule.guid = std::string(guidToUString(capsuleHeader->CapsuleGuid, false).toLocal8Bit());
        infoCapsule.base = model.base(index);
        infoCapsule.size = capsule.size();
        sizeFullImage = capsuleHeader->FullSize - capsuleHeaderSize;
    }
    // Check buffer for being extended Aptio capsule header
    else if (capsule.startsWith(APTIO_SIGNED_CAPSULE_GUID)
        || capsule.startsWith(APTIO_UNSIGNED_CAPSULE_GUID)) {
        bool signedCapsule = capsule.startsWith(APTIO_SIGNED_CAPSULE_GUID);
        const APTIO_CAPSULE_HEADER* capsuleHeader = (const APTIO_CAPSULE_HEADER*)capsule.constData();
        infoCapsule.name = std::string("AMI Aptio capsule (");
        if (signedCapsule)
            infoCapsule.name += "signed)";
        else
            infoCapsule.name += "unsigned)";

        infoCapsule.guid = std::string(guidToUString(capsuleHeader->CapsuleHeader.CapsuleGuid, false).toLocal8Bit());
        infoCapsule.base = model.base(index);
        infoCapsule.size = capsule.size();
        
    }
    return U_SUCCESS;
}

USTATUS ImageInfo::parseIntelImage(const UModelIndex& index)
{
    UByteArray intelImage = model.header(index) + model.body(index) + model.tail(index);
    infoIntelImage.base = model.base(index);
    infoIntelImage.size = model.header(index).size() + model.body(index).size() + model.tail(index).size();

    const FLASH_DESCRIPTOR_HEADER* descriptor = (const FLASH_DESCRIPTOR_HEADER*)intelImage.constData();
    const FLASH_DESCRIPTOR_MAP* descriptorMap = (const FLASH_DESCRIPTOR_MAP*)((UINT8*)descriptor + sizeof(FLASH_DESCRIPTOR_HEADER));
    const FLASH_DESCRIPTOR_UPPER_MAP* upperMap = (const FLASH_DESCRIPTOR_UPPER_MAP*)((UINT8*)descriptor + FLASH_DESCRIPTOR_UPPER_MAP_BASE);
    const FLASH_DESCRIPTOR_REGION_SECTION* regionSection = (const FLASH_DESCRIPTOR_REGION_SECTION*)calculateAddress8((UINT8*)descriptor, descriptorMap->RegionBase);
    //const FLASH_DESCRIPTOR_REGION_SECTION regionSection = *ptrRegionSection;

    const FLASH_DESCRIPTOR_COMPONENT_SECTION* componentSection = (const FLASH_DESCRIPTOR_COMPONENT_SECTION*)calculateAddress8((UINT8*)descriptor, descriptorMap->ComponentBase);

    UINT8 descriptorVersion = 2;
    // Check descriptor version by getting hardcoded value of FlashParameters.ReadClockFrequency
    if (componentSection->FlashParameters.ReadClockFrequency == FLASH_FREQUENCY_20MHZ)
        descriptorVersion = 1;

    infoIntelImage.descriptor.version = descriptorVersion;

    // Region access settings
    if (descriptorVersion == 1) {
        const FLASH_DESCRIPTOR_MASTER_SECTION* masterSection = (const FLASH_DESCRIPTOR_MASTER_SECTION*)calculateAddress8((UINT8*)descriptor, descriptorMap->MasterBase);
        infoIntelImage.descriptor.masterSection.BiosRead = masterSection->BiosRead;
        infoIntelImage.descriptor.masterSection.BiosWrite = masterSection->BiosWrite;
        infoIntelImage.descriptor.masterSection.MeRead = masterSection->MeRead;
        infoIntelImage.descriptor.masterSection.MeWrite = masterSection->MeWrite;
        infoIntelImage.descriptor.masterSection.GbeRead = masterSection->GbeRead;
        infoIntelImage.descriptor.masterSection.GbeWrite = masterSection->GbeWrite;
    }
    else if (descriptorVersion == 2) {
        const FLASH_DESCRIPTOR_MASTER_SECTION_V2* masterSection = (const FLASH_DESCRIPTOR_MASTER_SECTION_V2*)calculateAddress8((UINT8*)descriptor, descriptorMap->MasterBase);
        infoIntelImage.descriptor.masterSectionV2.BiosRead = masterSection->BiosRead;
        infoIntelImage.descriptor.masterSectionV2.BiosWrite = masterSection->BiosWrite;
        infoIntelImage.descriptor.masterSectionV2.MeRead = masterSection->MeRead;
        infoIntelImage.descriptor.masterSectionV2.MeWrite = masterSection->MeWrite;
        infoIntelImage.descriptor.masterSectionV2.GbeRead = masterSection->GbeRead;
        infoIntelImage.descriptor.masterSectionV2.GbeWrite = masterSection->GbeWrite;
        infoIntelImage.descriptor.masterSectionV2.EcRead = masterSection->EcRead;
        infoIntelImage.descriptor.masterSectionV2.EcWrite = masterSection->EcWrite;
    }

    infoIntelImage.descriptor.base = model.base(index.child(0, 0));
    infoIntelImage.descriptor.size = FLASH_DESCRIPTOR_SIZE;
    
    // Regions
    // ME region
    REGION_INTEL_IMAGE me;
    me.type = Subtypes::MeRegion;
    me.offset = 0;
    me.size = 0;
    me.base = 0;
    if (regionSection->MeLimit) {
        me.offset = calculateRegionOffset(regionSection->MeBase);
        me.size = calculateRegionSize(regionSection->MeBase, regionSection->MeLimit);
        me.base = model.base(index) + me.offset;
        infoIntelImage.vRegions.push_back(me);
    }

    // BIOS region
    if (regionSection->BiosLimit) {
        REGION_INTEL_IMAGE bios;
        bios.type = Subtypes::BiosRegion;
        bios.offset = calculateRegionOffset(regionSection->BiosBase);
        bios.size = calculateRegionSize(regionSection->BiosBase, regionSection->BiosLimit);
        bios.base = model.base(index) + bios.offset;
        // Check for Gigabyte specific descriptor map
        if (bios.size == (UINT32)intelImage.size()) {
            // Use ME region end as BIOS region offset
            bios.offset = me.offset + me.size;
            bios.size = (UINT32)intelImage.size() - bios.offset;
            bios.base = model.base(index) + bios.offset;
        }
        infoIntelImage.vRegions.push_back(bios);
    }


    // Add all other regions
    for (UINT8 i = Subtypes::GbeRegion; i <= Subtypes::PttRegion; i++) {
        if (descriptorVersion == 1 && i == Subtypes::MicrocodeRegion)
            break; // Do not parse Microcode and other following regions for legacy descriptors

        const UINT16* RegionBase = ((const UINT16*)regionSection) + 2 * i;
        const UINT16* RegionLimit = ((const UINT16*)regionSection) + 2 * i + 1;
        if (*RegionLimit && !(*RegionBase == 0xFFFF && *RegionLimit == 0xFFFF)) {
            REGION_INTEL_IMAGE region;
            region.type = i;
            region.offset = calculateRegionOffset(*RegionBase);
            region.size = calculateRegionSize(*RegionBase, *RegionLimit);
            region.base = model.base(index) + region.offset;
            if (region.size != 0) {
                region.base = model.base(index) + region.offset;
                infoIntelImage.vRegions.push_back(region);
            }
        }
    }

    std::sort(infoIntelImage.vRegions.begin(), infoIntelImage.vRegions.end());

    return U_SUCCESS;
}

USTATUS ImageInfo::parseUefiImage(const UModelIndex& index) {
    UByteArray uefiImage = model.header(index);
    infoUefiImage.base = model.base(index);
    infoUefiImage.size = model.header(index).size() + model.body(index).size() + model.tail(index).size();
    sizeFullImage = infoUefiImage.size;
    return U_SUCCESS;
}

INFO_FILE ImageInfo::parseFileType(const UModelIndex& index)
{
    INFO_FILE infoFile;
    UByteArray file = model.header(index) + model.body(index) + model.tail(index);
    UByteArray header = file.left(sizeof(EFI_FFS_FILE_HEADER));
    const EFI_FFS_FILE_HEADER* fileHeader = (const EFI_FFS_FILE_HEADER*)header.constData();

    infoFile.type = model.subtype(index);
    infoFile.name = std::string(guidToUString(fileHeader->Name).toLocal8Bit());
    infoFile.guid = std::string(guidToUString(fileHeader->Name, false).toLocal8Bit());
    infoFile.base = model.base(index);

    UINT64 address = ffsParser.addressDiff + model.base(index);
    UINT32 headerSize = (UINT32)model.header(index).size();
    infoFile.dataAddress = (unsigned long long)address + headerSize;
    
    infoFile.attributes = fileHeader->Attributes;
    infoFile.size = file.size();

    std::stringstream headerChecksum, dataChecksum;
    headerChecksum << usprintf("%02Xh", fileHeader->IntegrityCheck.Checksum.Header).toLocal8Bit();
    infoFile.headerChecksum = std::string(headerChecksum.str());
    dataChecksum << usprintf("%02Xh", fileHeader->IntegrityCheck.Checksum.File).toLocal8Bit();
    infoFile.dataChecksum = std::string(dataChecksum.str());

    return infoFile;
}

std::string ReplaceAll(std::string str, const std::string& from, const std::string& to) {
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length(); // Handles case where 'to' is a substring of 'from'
    }
    return str;
}

USTATUS ImageInfo::explore()
{
    // Parse input buffer
    std::cout << "Start explore file." << std::endl;
    USTATUS result = ffsParser.parse(openedImage);
    if (result)
        return result;
    UModelIndex root = model.index(0, 0);
    if (ffsParser.bgBootPolicyFound)
    {
        isBootGuard = true;
        //infoBootGuard = ffsParser.getSecurityInfo().toLocal8Bit();
        infoBootGuard = "  " + ReplaceAll(ffsParser.getSecurityInfo().toLocal8Bit(), "\n", "\n  ");
    }
    result = exploreTopSections(root);
    if (result)
        return result;
    result = exploreFileTypeRecursive(root);
    return result;
}

USTATUS ImageInfo::exploreTopSections(const UModelIndex& index)
{
    if (model.type(index) == Types::Capsule)
    {
        isCapsule = true;
        parseCapsule(index);
        exploreTopSections(index.child(0, 0));
    }
    else if (model.type(index) == Types::Image)
    {
        if (model.subtype(index) == Subtypes::IntelImage)
        {
            isIntelImage = true;
            parseIntelImage(index);
        }
        else if (model.subtype(index) == Subtypes::UefiImage)
        {
            parseUefiImage(index);
        };
    };
    return U_SUCCESS;
}

USTATUS ImageInfo::exploreFileTypeRecursive(const UModelIndex& index)
{
    if (!index.isValid())
        return U_INVALID_PARAMETER;
    if (model.type(index) == Types::File)
    {
        int type = model.subtype(index);
        if (type == EFI_FV_FILETYPE_PEI_CORE
            || type == EFI_FV_FILETYPE_DXE_CORE
            || type == EFI_FV_FILETYPE_PEIM
            || type == EFI_FV_FILETYPE_DRIVER)
        {
            vInfoFile.push_back(parseFileType(index));
            return U_SUCCESS;
        }
    }
    // Process child items
    USTATUS result;
    for (int i = 0; i < model.rowCount(index); i++) {
        result = exploreFileTypeRecursive(index.child(i, 0));
        if (result)
            return result;
    }
    return U_SUCCESS;
}

USTATUS ImageInfo::compareWithAnother(ImageInfo& anotherImage)
{
    std::cout << std::endl << "Comparing:" << std::endl;
    bool isDifferentCrc = false, isDifferentSize = false;

	if (crc != anotherImage.crc)
        isDifferentCrc = true;
	if (openedImage.size() != anotherImage.openedImage.size())
        isDifferentSize = true;
    
    std::cout << " -Images crc: " << ((isDifferentCrc) ? "different." : "equal.") << std::endl;
    std::cout << " -Images size: " << ((isDifferentSize) ? "different." : "equal.") << std::endl;

    if (isDifferentCrc || isDifferentSize)
    {
        UINT32 i;
        UINT32 max_size = std::min(openedImage.size(), anotherImage.openedImage.size());
        for (i = 0; i < max_size; i++)
        {
            if (openedImage[i] != anotherImage.openedImage[i])
                break;
        };
        i++;
        std::cout << std::uppercase;
        std::cout << " -Differences in images start with " << HexAndDecView(i) << std::endl;
    };
    
    return U_SUCCESS;
}

void ImageInfo::infoOutput(std::ostream& outputStream, UINT16 mode)
{
    outputStream << std::uppercase;
    outputStream << std::endl;
    if (mode & OUTPUT_MODE_DESCRIPTION)
    {
        //short description about image file
        outputStream << "General information about image file:" << std::endl;
        outputStream << "   -File size: " << HexAndDecView(sizeFullFile) << std::endl;
        outputStream << "   -CRC32: " << crc << std::endl;
        outputStream << "   -Image size: " << HexAndDecView(sizeFullImage) << std::endl;
        outputStream << "   -Capsule: " << ((isCapsule) ? "Yes" : "No") << std::endl;
        outputStream << "   -Image Type: " << ((isIntelImage) ? "Intel Image" : "UEFI image") << std::endl;
        outputStream << "   -Boot Guard: " << ((isBootGuard) ? "Yes" : "No") << std::endl << std::endl;
    };

    if (mode & OUTPUT_MODE_CAPSULE)
    {
        if (isCapsule)
        {
            outputStream << "Capsule:" << std::endl;
            outputStream << "   -Name: " << infoCapsule.name << std::endl;
            outputStream << "   -Guid: " << infoCapsule.guid << std::endl;
            outputStream << "   -Base: " << HexView(infoCapsule.base) << std::endl;
            outputStream << "   -Size: " << HexAndDecView(infoCapsule.size) << std::endl << std::endl;
        }
        else
            outputStream << "Image doesn't contain capsule." << std::endl << std::endl;
    };

    if (mode & OUTPUT_MODE_IMAGE)
    {
        if (isIntelImage)
        {
            outputStream << "Intel Image" << std::endl;
            outputStream << "   -Base: " << HexView(infoIntelImage.base) << std::endl;
            outputStream << "   -Size: " << HexAndDecView(infoIntelImage.size) << std::endl << std::endl;

            outputStream << " -Descriptor region" << std::endl;
            outputStream << "   -Base: " << HexView(infoIntelImage.descriptor.base) << std::endl;
            outputStream << "   -Size: " << HexAndDecView(infoIntelImage.descriptor.size) << std::endl << std::endl;
            std::ios_base::fmtflags basic_flags(outputStream.flags());
            if (infoIntelImage.descriptor.version == 1)
            {
                outputStream << "   Region access settings:" << std::endl;
                
                outputStream
                    << "   "
                    << std::setw(6)
                    << std::left
                    << "BIOS: "
                    << HexView(static_cast<int>(infoIntelImage.descriptor.masterSection.BiosRead))
                    //<< HexView(+infoIntelImage.descriptor.masterSection.BiosRead)
                    << " "
                    << HexView(static_cast<int>(infoIntelImage.descriptor.masterSection.BiosWrite)) << std::endl;

                outputStream
                    << "   "
                    << std::setw(6)
                    << std::left
                    << "ME: "
                    << HexView(static_cast<int>(infoIntelImage.descriptor.masterSection.MeRead))
                    << " "
                    << HexView(static_cast<int>(infoIntelImage.descriptor.masterSection.MeWrite)) << std::endl;

                outputStream
                    << "   "
                    << std::setw(6)
                    << std::left
                    << "GbE: "
                    << HexView(static_cast<int>(infoIntelImage.descriptor.masterSection.GbeRead))
                    << " "
                    << HexView(static_cast<int>(infoIntelImage.descriptor.masterSection.GbeWrite)) << std::endl << std::endl;

                outputStream.flags(basic_flags);

                outputStream << "   " << "BIOS access table:" << std::endl;

                outputStream << "   " << std::setw(6) << std::setfill(' ') << " ";
                outputStream << "Read  " << "Write" << std::endl;

                outputStream << "   " << std::setw(6) << std::left << "Desc";
                outputStream
                    << std::setw(6)
                    << (infoIntelImage.descriptor.masterSection.BiosRead & FLASH_DESCRIPTOR_REGION_ACCESS_DESC ? "Yes" : "No")
                    << (infoIntelImage.descriptor.masterSection.BiosWrite & FLASH_DESCRIPTOR_REGION_ACCESS_DESC ? "Yes" : "No") << std::endl;

                outputStream << "   " << std::setw(6) << std::left << "BIOS";
                outputStream << std::setw(6) << "Yes" << "Yes" << std::endl;

                outputStream << "   " << std::setw(6) << std::left << "ME";
                outputStream
                    << std::setw(6)
                    << (infoIntelImage.descriptor.masterSection.BiosRead & FLASH_DESCRIPTOR_REGION_ACCESS_ME ? "Yes" : "No")
                    << (infoIntelImage.descriptor.masterSection.BiosWrite & FLASH_DESCRIPTOR_REGION_ACCESS_ME ? "Yes" : "No") << std::endl;

                outputStream << "   " << std::setw(6) << std::left << "Gbe";
                outputStream
                    << std::setw(6)
                    << (infoIntelImage.descriptor.masterSection.BiosRead & FLASH_DESCRIPTOR_REGION_ACCESS_GBE ? "Yes" : "No")
                    << (infoIntelImage.descriptor.masterSection.BiosWrite & FLASH_DESCRIPTOR_REGION_ACCESS_GBE ? "Yes" : "No") << std::endl;

                outputStream << "   " << std::setw(6) << std::left << "PDR";
                outputStream
                    << std::setw(6)
                    << (infoIntelImage.descriptor.masterSection.BiosRead & FLASH_DESCRIPTOR_REGION_ACCESS_PDR ? "Yes" : "No")
                    << (infoIntelImage.descriptor.masterSection.BiosWrite & FLASH_DESCRIPTOR_REGION_ACCESS_PDR ? "Yes" : "No") << std::endl << std::endl;

            }
            else
            {

                outputStream << "   Region access settings:" << std::endl;
                outputStream << "   " << std::setw(6) << std::setfill(' ') << std::left << "BIOS: ";
                outputStream
                    << "   "
                    << std::setw(3) << std::setfill('0') << std::right
                    << HexView(static_cast<int>(infoIntelImage.descriptor.masterSectionV2.BiosRead))//<< HexView(+infoIntelImage.descriptor.masterSection.BiosRead)
                    << " "
                    << std::setw(3) << std::setfill('0') << std::right
                    << HexView(static_cast<int>(infoIntelImage.descriptor.masterSectionV2.BiosWrite)) << std::endl;

                outputStream << "   " << std::setw(6) << std::setfill(' ') << std::left << "ME: ";
                outputStream
                    << "   "
                    << std::setw(3) << std::setfill('0') << std::right
                    << HexView(static_cast<int>(infoIntelImage.descriptor.masterSectionV2.MeRead))
                    << " "
                    << std::setw(3) << std::setfill('0') << std::right
                    << HexView(static_cast<int>(infoIntelImage.descriptor.masterSectionV2.MeWrite)) << std::endl;

                outputStream << "   " << std::setw(6) << std::setfill(' ') << std::left << "GbE: ";
                outputStream
                    << "   "
                    << std::setw(3) << std::setfill('0') << std::right
                    << HexView(static_cast<int>(infoIntelImage.descriptor.masterSectionV2.GbeRead))
                    << " "
                    << std::setw(3) << std::setfill('0') << std::right
                    << HexView(static_cast<int>(infoIntelImage.descriptor.masterSectionV2.GbeWrite)) << std::endl;

                outputStream << "   " << std::setw(6) << std::setfill(' ') << std::left << "EC: ";
                outputStream
                    << "   "
                    << std::setw(3) << std::setfill('0') << std::right
                    << HexView(static_cast<int>(infoIntelImage.descriptor.masterSectionV2.EcRead))
                    << " "
                    << std::setw(3) << std::setfill('0') << std::right
                    << HexView(static_cast<int>(infoIntelImage.descriptor.masterSectionV2.EcWrite)) << std::endl << std::endl;

                outputStream.flags(basic_flags);

                outputStream << "   BIOS access table:" << std::endl;
                outputStream << "   " << std::setw(6) << std::setfill(' ') << " ";
                outputStream << "Read  " << "Write" << std::endl;

                outputStream << "   " << std::setw(6) << std::left << "Desc";
                outputStream
                    << std::setw(6)
                    << (infoIntelImage.descriptor.masterSectionV2.BiosRead & FLASH_DESCRIPTOR_REGION_ACCESS_DESC ? "Yes" : "No")
                    << (infoIntelImage.descriptor.masterSectionV2.BiosWrite & FLASH_DESCRIPTOR_REGION_ACCESS_DESC ? "Yes" : "No") << std::endl;

                outputStream << "   " << std::setw(6) << std::left << "BIOS";
                outputStream << std::setw(6) << "Yes" << "Yes" << std::endl;

                outputStream << "   " << std::setw(6) << std::left << "ME";
                outputStream
                    << std::setw(6)
                    << (infoIntelImage.descriptor.masterSectionV2.BiosRead & FLASH_DESCRIPTOR_REGION_ACCESS_ME ? "Yes" : "No")
                    << (infoIntelImage.descriptor.masterSectionV2.BiosWrite & FLASH_DESCRIPTOR_REGION_ACCESS_ME ? "Yes" : "No") << std::endl;

                outputStream << "   " << std::setw(6) << std::left << "GbE";
                outputStream
                    << std::setw(6)
                    << (infoIntelImage.descriptor.masterSectionV2.BiosRead & FLASH_DESCRIPTOR_REGION_ACCESS_GBE ? "Yes" : "No")
                    << (infoIntelImage.descriptor.masterSectionV2.BiosWrite & FLASH_DESCRIPTOR_REGION_ACCESS_GBE ? "Yes" : "No") << std::endl;

                outputStream << "   " << std::setw(6) << std::left << "PDR";
                outputStream
                    << std::setw(6)
                    << (infoIntelImage.descriptor.masterSectionV2.BiosRead & FLASH_DESCRIPTOR_REGION_ACCESS_PDR ? "Yes" : "No")
                    << (infoIntelImage.descriptor.masterSectionV2.BiosWrite & FLASH_DESCRIPTOR_REGION_ACCESS_PDR ? "Yes" : "No") << std::endl;

                outputStream << "   " << std::setw(6) << std::left << "EC";
                outputStream
                    << std::setw(6)
                    << (infoIntelImage.descriptor.masterSectionV2.BiosRead & FLASH_DESCRIPTOR_REGION_ACCESS_EC ? "Yes" : "No")
                    << (infoIntelImage.descriptor.masterSectionV2.BiosWrite & FLASH_DESCRIPTOR_REGION_ACCESS_EC ? "Yes" : "No") << std::endl << std::endl;
            }
            for (REGION_INTEL_IMAGE iRegionInfo : infoIntelImage.vRegions)
            {
                //VariadicTable<std::string, std::string, std::string, std::string> tableCapsule({ "Name", "Guid", "Base", "Size" });
                outputStream << " -" << regionTypeToUString(iRegionInfo.type).toLocal8Bit() << " region" << std::endl;
                outputStream << "   -Base: " << HexView(iRegionInfo.base) << std::endl;
                outputStream << "   -Offset: " << HexView(iRegionInfo.offset) << std::endl;
                outputStream << "   -Size: " << HexAndDecView(iRegionInfo.size) << std::endl << std::endl;
            }
            outputStream << std::endl;
            outputStream.flags(basic_flags);
        }
        else
        {
            outputStream << "UEFI Image" << std::endl;
            outputStream << "   -Base: " << HexView(infoUefiImage.base) << std::endl;
            outputStream << "   -Size: " << HexAndDecView(infoUefiImage.size) << std::endl << std::endl;
        }
    }
    
    if (mode & OUTPUT_MODE_BG)
    {
        if (isBootGuard)
        {
            outputStream << "Image security info:" << std::endl;
            outputStream << infoBootGuard << std::endl;
        }
        else
            outputStream << "Image doesn't contain Boot Guard." << std::endl << std::endl;
    }

    if (mode & OUTPUT_MODE_FILE_ALL)
    {
        VariadicTable<std::string, std::string, std::string, std::string, std::string, std::string, std::string>
            tablePeiM({ "Name", "Guid", "Base", "Data address", "Size", "Header checksum", "Data checksum" });
        VariadicTable<std::string, std::string, std::string, std::string, std::string, std::string, std::string>
            tablePeiC({ "Name", "Guid", "Base", "Data address", "Size", "Header checksum", "Data checksum" });
        VariadicTable<std::string, std::string, std::string, std::string, std::string, std::string, std::string>
            tableDxeD({ "Name", "Guid", "Base", "Data address", "Size", "Header checksum", "Data checksum" });
        VariadicTable<std::string, std::string, std::string, std::string, std::string, std::string, std::string>
            tableDxeC({ "Name", "Guid", "Base", "Data address", "Size", "Header checksum", "Data checksum" });

        for (INFO_FILE iInfoFile : vInfoFile)
        {
            std::stringstream base, dataAddress, size;
            base << std::uppercase;
            dataAddress << std::uppercase;
            size << std::uppercase;
            base << HexView(iInfoFile.base);
            dataAddress << HexView(iInfoFile.dataAddress);
            size << HexView(iInfoFile.size);
            switch (iInfoFile.type)
            {
            case EFI_FV_FILETYPE_PEIM:
                tablePeiM.addRow(
                    iInfoFile.name,
                    iInfoFile.guid,
                    base.str(),
                    dataAddress.str(),
                    size.str(),
                    iInfoFile.headerChecksum,
                    iInfoFile.dataChecksum
                );
                break;
            case EFI_FV_FILETYPE_PEI_CORE:
                tablePeiC.addRow(
                    iInfoFile.name,
                    iInfoFile.guid,
                    base.str(),
                    dataAddress.str(),
                    size.str(),
                    iInfoFile.headerChecksum,
                    iInfoFile.dataChecksum
                );
                break;
            case EFI_FV_FILETYPE_DRIVER:

                tableDxeD.addRow(
                    iInfoFile.name,
                    iInfoFile.guid,
                    base.str(),
                    dataAddress.str(),
                    size.str(),
                    iInfoFile.headerChecksum,
                    iInfoFile.dataChecksum
                );
                break;
            case EFI_FV_FILETYPE_DXE_CORE:
                tableDxeC.addRow(
                    iInfoFile.name,
                    iInfoFile.guid,
                    base.str(),
                    dataAddress.str(),
                    size.str(),
                    iInfoFile.headerChecksum,
                    iInfoFile.dataChecksum
                );
                break;
            };
        };

        if (mode & OUTPUT_MODE_FILE_PEI_CORE)
            tablePeiC.print(outputStream, "PEI Core");
        if (mode & OUTPUT_MODE_FILE_PEI)
            tablePeiM.print(outputStream, "PEI Modules");
        if (mode & OUTPUT_MODE_FILE_DXE_CORE)
            tableDxeC.print(outputStream, "DXE Core");
        if (mode & OUTPUT_MODE_FILE_DXE)
            tableDxeD.print(outputStream, "DXE Drivers");
    };
};

INFO_FILE parseFileTypeStructureFromJSON(ordered_json& fileInfoObj, int type)
{
    INFO_FILE tempInfoFile;
    tempInfoFile.type = type;
    tempInfoFile.name = fileInfoObj["name"].get<std::string>();
    tempInfoFile.guid = fileInfoObj["guid"].get<std::string>();
    tempInfoFile.base = fileInfoObj["base"].get<UINT32>();
    tempInfoFile.dataAddress = fileInfoObj["dataAddress"].get<UINT64>();
    tempInfoFile.attributes = fileInfoObj["attributes"].get<UINT32>();
    tempInfoFile.size = fileInfoObj["size"].get<UINT32>();
    tempInfoFile.headerChecksum = fileInfoObj["headerChecksum"].get<std::string>();
    tempInfoFile.dataChecksum = fileInfoObj["dataChecksum"].get<std::string>();
    return tempInfoFile;
}

bool ImageInfo::readFromFile()
{
    UString reportPath = usprintf("reports/report_%u.json", crc);
    if (!isExistOnFs(reportPath))
        return false;

    std::ifstream inputFile(reportPath.toLocal8Bit(), std::ios::in);
    if (!inputFile)
        return false;

    std::cout << "Report is already exist. Reading..." << std::endl;

    ordered_json imageMainJsonObj;
    inputFile >> imageMainJsonObj;

    //crc and full file size already in class
    sizeFullImage = imageMainJsonObj["sizeFullImage"].get<UINT32>();

    if (imageMainJsonObj.contains("capsule"))
    {
        isCapsule = true;
        ordered_json capsuleObj = imageMainJsonObj["capsule"];
        infoCapsule.name = capsuleObj["name"].get<std::string>();;
        infoCapsule.guid = capsuleObj["guid"].get<std::string>();;
        infoCapsule.base = capsuleObj["base"].get<UINT32>();
        infoCapsule.size = capsuleObj["size"].get<UINT32>();
    };

    if (imageMainJsonObj.contains("intel_image"))
    {
        isIntelImage = true;
        ordered_json intelImageObj = imageMainJsonObj["intel_image"];
        infoIntelImage.base = intelImageObj["base"].get<UINT32>();
        infoIntelImage.size = intelImageObj["size"].get<UINT32>();

        ordered_json descriptorObj = intelImageObj["descriptor"];
        infoIntelImage.descriptor.version = descriptorObj["version"].get<UINT8>();
        infoIntelImage.descriptor.base = descriptorObj["base"].get<UINT32>();
        infoIntelImage.descriptor.size = descriptorObj["size"].get<UINT32>();

        if (infoIntelImage.descriptor.version == 1)
        {
            ordered_json masterSectionObj = descriptorObj["masterSection"];
            infoIntelImage.descriptor.masterSection.BiosRead = masterSectionObj["BiosRead"].get<UINT8>();
            infoIntelImage.descriptor.masterSection.BiosWrite = masterSectionObj["BiosWrite"].get<UINT8>();
            infoIntelImage.descriptor.masterSection.MeRead = masterSectionObj["MeRead"].get<UINT8>();
            infoIntelImage.descriptor.masterSection.MeWrite = masterSectionObj["MeWrite"].get<UINT8>();
            infoIntelImage.descriptor.masterSection.GbeRead = masterSectionObj["GbeRead"].get<UINT8>();
            infoIntelImage.descriptor.masterSection.GbeWrite = masterSectionObj["GbeWrite"].get<UINT8>();
        }
        else
        {
            ordered_json masterSectionV2Obj = descriptorObj["masterSectionV2"];
            infoIntelImage.descriptor.masterSectionV2.BiosRead = masterSectionV2Obj["BiosRead"].get<UINT32>();
            infoIntelImage.descriptor.masterSectionV2.BiosWrite = masterSectionV2Obj["BiosWrite"].get<UINT32>();
            infoIntelImage.descriptor.masterSectionV2.MeRead = masterSectionV2Obj["MeRead"].get<UINT32>();
            infoIntelImage.descriptor.masterSectionV2.MeWrite = masterSectionV2Obj["MeWrite"].get<UINT32>();
            infoIntelImage.descriptor.masterSectionV2.GbeRead = masterSectionV2Obj["GbeRead"].get<UINT32>();
            infoIntelImage.descriptor.masterSectionV2.GbeWrite = masterSectionV2Obj["GbeWrite"].get<UINT32>();
            infoIntelImage.descriptor.masterSectionV2.EcRead = masterSectionV2Obj["EcRead"].get<UINT32>();
            infoIntelImage.descriptor.masterSectionV2.EcWrite = masterSectionV2Obj["EcWrite"].get<UINT32>();
        };

        ordered_json regionsArr = ordered_json::array();
        regionsArr = intelImageObj["regions"];
        for (ordered_json iRegionInfoObj : regionsArr)
        {
            REGION_INTEL_IMAGE tempRegion;
            tempRegion.type = iRegionInfoObj["type"].get<UINT8>();
            tempRegion.base = iRegionInfoObj["base"].get<UINT32>();
            tempRegion.size = iRegionInfoObj["size"].get<UINT32>();
            infoIntelImage.vRegions.push_back(tempRegion);
        };

    }
    else
    {
        ordered_json uefiImageObj = imageMainJsonObj["uefi_image"];
        infoUefiImage.base = uefiImageObj["base"].get<UINT32>();
        infoUefiImage.size = uefiImageObj["size"].get<UINT32>();
    }

    if (imageMainJsonObj.contains("boot_guard"))
    {
        isBootGuard = true;
        infoBootGuard = imageMainJsonObj["boot_guard"].get<std::string>();
    };

    ordered_json peimArr = ordered_json::array();
    ordered_json dxedArr = ordered_json::array();
    ordered_json peiCoreArr = ordered_json::array();
    ordered_json dxeCoreArr = ordered_json::array();

    peimArr = imageMainJsonObj["pei_modules"];
    dxedArr = imageMainJsonObj["dxe_drivers"];
    peiCoreArr = imageMainJsonObj["pei_core"];
    dxeCoreArr = imageMainJsonObj["dxe_core"];

    for (ordered_json iInfoFileJson : peimArr)
    {
        vInfoFile.push_back(parseFileTypeStructureFromJSON(iInfoFileJson, EFI_FV_FILETYPE_PEIM));
    };
    for (ordered_json iInfoFileJson : dxedArr)
    {
        vInfoFile.push_back(parseFileTypeStructureFromJSON(iInfoFileJson, EFI_FV_FILETYPE_DRIVER));
    };
    for (ordered_json iInfoFileJson : peiCoreArr)
    {
        vInfoFile.push_back(parseFileTypeStructureFromJSON(iInfoFileJson, EFI_FV_FILETYPE_PEI_CORE));
    };
    for (ordered_json iInfoFileJson : dxeCoreArr)
    {
        vInfoFile.push_back(parseFileTypeStructureFromJSON(iInfoFileJson, EFI_FV_FILETYPE_DXE_CORE));
    };

    return true;
}

bool ImageInfo::writeToFile()
{
    UString reportPath = usprintf("reports/report_%u.json", crc);
    if (isExistOnFs(reportPath))
        return false;
    UString dirPath("reports");
    if (!isExistOnFs(dirPath))
    {
        if (!makeDirectory(dirPath))
        {
            std::cout << "Error of creating directory for reports." << std::endl;
            return false;
        }
    };

    std::cout << "Writing image information to file: " << reportPath.toLocal8Bit() <<std::endl;
    std::ofstream outputFile(reportPath.toLocal8Bit(), std::ios::out);

    ordered_json imageMainJsonObj;

    imageMainJsonObj["crc"] = crc;
    imageMainJsonObj["sizeFullFile"] = sizeFullFile;
    imageMainJsonObj["sizeFullImage"] = sizeFullImage;

    if (isCapsule)
    {
        ordered_json capsuleObj;
        capsuleObj["name"] = infoCapsule.name;
        capsuleObj["guid"] = infoCapsule.guid;
        capsuleObj["base"] = infoCapsule.base;
        capsuleObj["size"] = infoCapsule.size;
        imageMainJsonObj["capsule"] = capsuleObj;
    };

    if (isIntelImage)
    {
        ordered_json intelImageObj;
        intelImageObj["base"] = infoIntelImage.base;
        intelImageObj["size"] = infoIntelImage.size;

        ordered_json descriptorObj;
        descriptorObj["version"] = infoIntelImage.descriptor.version;
        descriptorObj["base"] = infoIntelImage.descriptor.base;
        descriptorObj["size"] = infoIntelImage.descriptor.size;
        if (infoIntelImage.descriptor.version == 1)
        {
            ordered_json masterSectionObj;
            masterSectionObj["BiosRead"] = infoIntelImage.descriptor.masterSection.BiosRead;
            masterSectionObj["BiosWrite"] = infoIntelImage.descriptor.masterSection.BiosWrite;
            masterSectionObj["MeRead"] = infoIntelImage.descriptor.masterSection.MeRead;
            masterSectionObj["MeWrite"] = infoIntelImage.descriptor.masterSection.MeWrite;
            masterSectionObj["GbeRead"] = infoIntelImage.descriptor.masterSection.GbeRead;
            masterSectionObj["GbeWrite"] = infoIntelImage.descriptor.masterSection.GbeWrite;
            descriptorObj["masterSection"] = masterSectionObj;
        }
        else
        {
            ordered_json masterSectionV2Obj;
            masterSectionV2Obj["BiosRead"] = (UINT32)infoIntelImage.descriptor.masterSectionV2.BiosRead;
            masterSectionV2Obj["BiosWrite"] = (UINT32)infoIntelImage.descriptor.masterSectionV2.BiosWrite;
            masterSectionV2Obj["MeRead"] = (UINT32)infoIntelImage.descriptor.masterSectionV2.MeRead;
            masterSectionV2Obj["MeWrite"] = (UINT32)infoIntelImage.descriptor.masterSectionV2.MeWrite;
            masterSectionV2Obj["GbeRead"] = (UINT32)infoIntelImage.descriptor.masterSectionV2.GbeRead;
            masterSectionV2Obj["GbeWrite"] = (UINT32)infoIntelImage.descriptor.masterSectionV2.GbeWrite;
            masterSectionV2Obj["EcRead"] = (UINT32)infoIntelImage.descriptor.masterSectionV2.GbeRead;
            masterSectionV2Obj["EcWrite"] = (UINT32)infoIntelImage.descriptor.masterSectionV2.GbeWrite;
            descriptorObj["masterSectionV2"] = masterSectionV2Obj;
        }
        intelImageObj["descriptor"] = descriptorObj;

        ordered_json regionsArr = ordered_json::array();
        for (REGION_INTEL_IMAGE iRegionInfo : infoIntelImage.vRegions)
        {
            ordered_json tempRegionObj;
            tempRegionObj["type"] = iRegionInfo.type;
            tempRegionObj["base"] = iRegionInfo.base;
            tempRegionObj["size"] = iRegionInfo.size;
            regionsArr.push_back(tempRegionObj);
        }
        intelImageObj["regions"] = regionsArr;

        imageMainJsonObj["intel_image"] = intelImageObj;
    }
    else
    {
        ordered_json uefiImageObj;
        uefiImageObj["base"] = infoUefiImage.base;
        uefiImageObj["sizeFullImage"] = infoUefiImage.size;
        imageMainJsonObj["uefi_image"] = uefiImageObj;
    }

    if (isBootGuard)
    {
        imageMainJsonObj["boot_guard"] = infoBootGuard;
    }

    ordered_json peimArr = ordered_json::array();
    ordered_json dxedArr = ordered_json::array();
    ordered_json peiCoreArr = ordered_json::array();
    ordered_json dxeCoreArr = ordered_json::array();

    for (INFO_FILE iInfoFile : vInfoFile)
    {
        ordered_json tempObj;
        tempObj["name"] = iInfoFile.name;
        tempObj["guid"] = iInfoFile.guid;
        tempObj["base"] = iInfoFile.base;
        tempObj["dataAddress"] = iInfoFile.dataAddress;
        tempObj["attributes"] = iInfoFile.attributes;
        tempObj["size"] = iInfoFile.size;
        tempObj["headerChecksum"] = iInfoFile.headerChecksum;
        tempObj["dataChecksum"] = iInfoFile.dataChecksum;
        switch (iInfoFile.type)
        {
        case EFI_FV_FILETYPE_PEIM:
            peimArr.push_back(tempObj);
            break;
        case EFI_FV_FILETYPE_DRIVER:
            dxedArr.push_back(tempObj);
            break;
        case EFI_FV_FILETYPE_PEI_CORE:
            peiCoreArr.push_back(tempObj);
            break;
        case EFI_FV_FILETYPE_DXE_CORE:
            dxeCoreArr.push_back(tempObj);
            break;
        };
    };

    if (!peiCoreArr.empty())
        imageMainJsonObj["pei_core"] = peiCoreArr;
    if (!peimArr.empty())
        imageMainJsonObj["pei_modules"] = peimArr;
    if (!dxeCoreArr.empty())
        imageMainJsonObj["dxe_core"] = dxeCoreArr;
    if (!dxedArr.empty())
        imageMainJsonObj["dxe_drivers"] = dxedArr;

    outputFile << std::setw(4) << imageMainJsonObj;
    return true;
}



/******************************************UNKNOWN******************************************/
void ImageInfo::printSecurityInfo()
{
    std::cout << "----------------------------------------------------------------------------------" << std::endl;
    std::cout << "----------------------------------------------------------------------------------" << std::endl;
    std::cout << "Security info = " << ffsParser.getSecurityInfo() << std::endl;

};

USTATUS ImageInfo::checkProtectedRegions()
{
//    #define BG_PROTECTED_RANGE_INTEL_BOOT_GUARD_IBB      0x01
//#define BG_PROTECTED_RANGE_INTEL_BOOT_GUARD_POST_IBB 0x02
//#define BG_PROTECTED_RANGE_VENDOR_HASH_PHOENIX       0x03
//#define BG_PROTECTED_RANGE_VENDOR_HASH_AMI_OLD       0x04
//#define BG_PROTECTED_RANGE_VENDOR_HASH_AMI_NEW       0x05
//#define BG_PROTECTED_RANGE_VENDOR_HASH_MICROSOFT     0x06
    //outputStream << "bgProtectedRanges size: " << ffsParser.bgProtectedRanges.size() << std::endl;
    //VariadicTable<std::string, std::string, std::string> tableBG({ "Type", "Offset", "Size" });
    //for (auto iBG_PROTECTED_RANGE : ffsParser.bgProtectedRanges)
    //{
    //    std::stringstream offset, size;
    //    offset << HexView(iBG_PROTECTED_RANGE.Offset);
    //    size << HexView(iBG_PROTECTED_RANGE.Size);
    //    tableBG.addRow(std::to_string(iBG_PROTECTED_RANGE.Type), offset.str(), size.str());
    //}
    //tableBG.print(outputStream, "BG Ranges");
    //return;
    std::cout << "check protected ranges" << std::endl;
    std::cout << "size = " << ffsParser.bgProtectedRanges.size() << std::endl;

    VariadicTable<std::string, std::string, std::string> tableBG({ "Type", "Offset", "Size" });
    for (auto iBG_PROTECTED_RANGE : ffsParser.bgProtectedRanges)
    {
            std::stringstream offset, size;
            offset << HexView(iBG_PROTECTED_RANGE.Offset);
            size << HexView(iBG_PROTECTED_RANGE.Size);
            tableBG.addRow(std::to_string(iBG_PROTECTED_RANGE.Type), offset.str(), size.str());
    }
    tableBG.print(std::cout);
    return U_SUCCESS;
}