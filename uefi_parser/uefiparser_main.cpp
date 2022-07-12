#include <iostream>
#include "common/guiddatabase.h"
#include "common/filesystem.h"
#include "imageinfo.h"
#include <boost/program_options.hpp>
namespace po = boost::program_options;


int main(int argc, char* argv[])
{
	std::cout << "UEFI Image Parser" << std::endl;
	if (argc > 1)
	{
		std::string inputFilePath, anotherInputFilePath, streamModeStr, outputModeStr;
		po::options_description desc("General options");
		desc.add_options()
			("help,h", "Show help message")
			("file,f", po::value<std::string>(&inputFilePath), "Path to image file")
			("stream,s", po::value<std::string>(&streamModeStr)->default_value("cout"),
				"Output stream: \n"
				"\'cout\' - write to cmd (default)\n"
				"\'name.txt\' - name of file to write")
			("outputmode,o", po::value<std::string>(&outputModeStr)->default_value("desc"),
				"Output mode (format \"x1,x2,x3,...\"): \n"
				"\'desc\' - short description with main info (default)\n"
				"\'capsule\' - info about image capsule\n"
				"\'image\' - info about image section\n"
				"\'bg\' - info about Boot Guard\n"
				"\'peicore\' - info about Pei Core\n"
				"\'peimodules\' - info about Pei Modules\n"
				"\'dxecore\' - info about DXE Core\n"
				"\'dxedrivers\' - info about DXE Drivers\n"
				"\'all\' - all information about image")
			("compare,c", po::value<std::string>(&anotherInputFilePath), "Enable compare mode. Path to another image file for comparing.")
			;
		//("process-jpeg,e", po::value<string>()->default_value("")->implicit_value("./"), "Processes a JPEG.");
		namespace po = boost::program_options;
		po::variables_map vm;
		po::parsed_options parsed = po::command_line_parser(argc, argv).options(desc).allow_unregistered().run();
		//po::parsed_options parsed = po::command_line_parser(argc, argv).options(desc).positional(p).run()
		po::store(parsed, vm);
		po::notify(vm);

		if (vm.count("help")) {
			std::cout << desc << std::endl;
			return 0;
		};
		if (!vm.count("file"))
		{
			std::cout << "Invalid arguments! Path to image file is required. Use --help for more info." << std::endl;
			std::cout << desc << std::endl;
			return 0;
		};

		initGuidDatabase("guids.csv");
		USTATUS result;
		UByteArray buffer;
		UString path;
		path = getAbsPath(inputFilePath.c_str());
		std::cout << "Input image file: " << path << std::endl;
		std::cout << "Reading file..." << std::endl;
		result = readFileIntoBuffer(path, buffer);
		if (result)
		{
			std::cout << "Error of reading file." << std::endl;
			return result;
		};

		ImageInfo imageInfo(buffer);

		//Compare mode
		if (vm.count("compare"))
		{
			UByteArray anotherBuffer;
			UString anotherPath = getAbsPath(anotherInputFilePath.c_str());
			std::cout << "Second image file: " << anotherPath << std::endl;
			std::cout << "Reading second file..." << std::endl;
			result = readFileIntoBuffer(anotherPath, anotherBuffer);
			if (result)
			{
				std::cout << "Error of reading second file." << std::endl;
				return result;
			}
			ImageInfo anotherImageInfo(anotherBuffer);
			imageInfo.compareWithAnother(anotherImageInfo);
			return 0;
		};

		//Main mode, try to reading existing report or explore file and write report
		if (!imageInfo.readFromFile())
		{
		    imageInfo.explore();
		    if (!imageInfo.writeToFile())
		    {
		        std::cout << "Error of writing information to report file." << std::endl;
		    }
		}; 

		//parse outputmode arguments
		UINT16 mode = 0;
		if (outputModeStr.find("desc") != std::string::npos)
			mode |= OUTPUT_MODE_DESCRIPTION;
		if (outputModeStr.find("capsule") != std::string::npos)
			mode |= OUTPUT_MODE_CAPSULE;
		if (outputModeStr.find("image") != std::string::npos)
			mode |= OUTPUT_MODE_IMAGE;
		if (outputModeStr.find("peicore") != std::string::npos)
			mode |= OUTPUT_MODE_FILE_PEI_CORE;
		if (outputModeStr.find("peimodules") != std::string::npos)
			mode |= OUTPUT_MODE_FILE_PEI;
		if (outputModeStr.find("dxecore") != std::string::npos)
			mode |= OUTPUT_MODE_FILE_DXE_CORE;
		if (outputModeStr.find("dxedrivers") != std::string::npos)
			mode |= OUTPUT_MODE_FILE_DXE;
		if (outputModeStr.find("bg") != std::string::npos)
			mode |= OUTPUT_MODE_BG;
		if (outputModeStr.find("all") != std::string::npos)
			mode |= OUTPUT_MODE_FULL;

		if (streamModeStr == "cout")
			imageInfo.infoOutput(std::cout, mode);
		else
		{
			std::cout << "Writing output information to \"" << streamModeStr << "\" file." << std::endl;
			std::ofstream outputFile(streamModeStr, std::ios::out);
			imageInfo.infoOutput(outputFile, mode);
		};

	}
	else
	{
		initGuidDatabase("guids.csv");
		std::string inputFilePath, anotherInputFilePath;
		USTATUS result;
		UString path;
		UByteArray buffer;

		int menuSection = 3, option;
		while (true)
		{
			if (menuSection == 0)
			{
				std::cout
					<< "--------------------------------\n"
					<< "Select option:\n"
					<< "    1) output info about image\n"
					<< "    2) compare with another\n"
					<< "    3) enter new file path\n"
					<< "    4) Exit\n"
					<< "--------------------------------\n"
					<< ">";
				std::cin >> menuSection;
				std::cout << "--------------------------------\n";
			}
			else if (menuSection == 1)
			{
				ImageInfo imageInfo(buffer);
				//Main mode, try to reading existing report or explore file and write report
				if (!imageInfo.readFromFile())
				{
					imageInfo.explore();
					if (!imageInfo.writeToFile())
					{
						std::cout << "Error of writing information to report file." << std::endl;
					}
				};
				while (true)
				{
					std::cout
						<< "--------------------------------------\n"
						<< "Select information for output:\n"
						<< "    1) General info about image file\n"
						<< "    2) Capsule\n"
						<< "    3) Image section\n"
						<< "    4) Pei Core\n"
						<< "    5) Pei Modules\n"
						<< "    6) DXE Core\n"
						<< "    7) DXE Drivers\n"
						<< "    8) Boot Guard\n"
						<< "    9) All information\n"
						<< "    10) Back to previous menu\n"
						<< "--------------------------------------\n"
						<< ">";
					std::cin >> option;
					std::cout << "--------------------------------------\n";
					UINT16 mode = 0;
					if (option == 1)
						mode = OUTPUT_MODE_DESCRIPTION;
					else if (option == 2)
						mode = OUTPUT_MODE_CAPSULE;
					else if (option == 3)
						mode = OUTPUT_MODE_IMAGE;
					else if (option == 4)
						mode = OUTPUT_MODE_FILE_PEI_CORE;
					else if (option == 5)
						mode = OUTPUT_MODE_FILE_PEI;
					else if (option == 6)
						mode = OUTPUT_MODE_FILE_DXE_CORE;
					else if (option == 7)
						mode = OUTPUT_MODE_FILE_DXE;
					else if (option == 8)
						mode = OUTPUT_MODE_BG;
					else if (option == 9)
						mode = OUTPUT_MODE_FULL;
					else if (option == 10)
						break;
					else
					{
						std::cout << "Wrong option number." << std::endl;
						continue;
					};
					imageInfo.infoOutput(std::cout, mode);
				}
				menuSection = 0;
			}
			else if (menuSection == 2)
			{
				ImageInfo imageInfo(buffer);

				std::cout << "Enter path to another image file for comparing: ";
				std::cin >> anotherInputFilePath;
				UByteArray anotherBuffer;
				UString anotherPath = getAbsPath(anotherInputFilePath.c_str());
				std::cout << "Input image file for comparing: " << path << std::endl;
				std::cout << "Reading second file..." << std::endl;
				result = readFileIntoBuffer(anotherPath, anotherBuffer);
				if (result)
				{
					std::cout << "Error of reading second file." << std::endl;
					return result;
				};
				ImageInfo anotherImageInfo(anotherBuffer);
				imageInfo.compareWithAnother(anotherImageInfo);
				menuSection = 0;
			}
			else if (menuSection == 3)
			{
				std::cout << "Enter path to file for analyze: ";
				std::cin >> inputFilePath;
				path = getAbsPath(inputFilePath.c_str());
				std::cout << "Input image file: " << path << std::endl;
				std::cout << "Reading file..." << std::endl;
				result = readFileIntoBuffer(path, buffer);
				if (result)
				{
					std::cout << "Error of reading file." << std::endl;
				}
				else
					menuSection = 0;
			}
			else if (menuSection == 4)
				return 0;
			else
			{
				std::cout << "Wrong option number." << std::endl;
				continue;
			}
		}
	}
	return 0;
}