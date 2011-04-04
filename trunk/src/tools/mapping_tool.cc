// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public
// License along with main.c; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor Boston, MA 02110-1301,  USA

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <map>

#include "getopt.h"
#include "mappingfile.h"
#include "dplcfile.h"

static void usage()
{
	std::cerr << "Usage: mapping_tool {-o|--optimize} [--sonic2] {input_mappings} {input_dplc} {output_mappings} {output_dplc}" << std::endl;
	std::cerr << "\tOptimizes the input mappings and DPLC files to use the least possible amount of DMA transfers." << std::endl << std::endl;
	std::cerr << "Usage: mapping_tool {-s|--split} [--sonic2] {input_mappings} {output_mappings} {output_dplc}" << std::endl;
	std::cerr << "\tConverts a mappings file into a mappings + DPLC pair, optimized to use the least possible amount of DMA transfers." << std::endl << std::endl;
	std::cerr << "Usage: mapping_tool {-m|--merge} [--sonic2] {input_mappings} {input_dplc} {output_mappings}" << std::endl;
	std::cerr << "\tConverts a mappings + DPLC pair into a non-DPLC mappings file. Warning: this will not work correctly in-game if there are more then 0x7FF (2047) tiles in the art file." << std::endl << std::endl;
	std::cerr << "Usage: mapping_tool {-f|--fix} {input_mappings} {output_mappings}" << std::endl;
	std::cerr << "\tSonic 2 only. Fixes the SonMapED mappings file to the correct tile value for double resolution 2-player mode." << std::endl << std::endl;
	std::cerr << "Usage: mapping_tool {-c|--convert} [--from-sonic2|--to-sonic2] {input_mappings} {output_mappings}" << std::endl;
	std::cerr << "\tConverts the source mappings file to/from Sonic 2 format." << std::endl << std::endl;
	
	std::cerr << "\t--sonic2\t{output_filename} is in Sonic 2 mappings format. Default to non-Sonic 2 format." << std::endl << std::endl;
	std::cerr << "Usage: mapping_tool {-i|--info} [--sonic2] {input_mappings}" << std::endl;
	std::cerr << "\tGives information about the mappings file." << std::endl << std::endl;
	std::cerr << "Usage: mapping_tool {-d|--dplc} {input_dplc}" << std::endl;
	std::cerr << "\tGives information about the DPLC file." << std::endl << std::endl;
	std::cerr << "\tWarning: the optimized mappings + DPLC files generated by this utility work perfectly in-game, but do not work in SonMapED. Complains about this should go to Xenowhirl." << std::endl << std::endl;
}

enum Actions
{
	eNone = 0,
	eOptimize,
	eSplit,
	eMerge,
	eFix,
	eConvert,
	eInfo,
	eDplc,
	ePalChange
};

enum FileErrors
{
	eInvalidArgs = 1,
	eInputMapsMissing,
	eInputDplcMissing,
	eOutputMapsMissing,
	eOutputDplcMissing
};

#define ARG_CASE(x,y,z) 	case (x):	\
				if (act != eNone)	\
				{	\
					usage();	\
					return eInvalidArgs;	\
				}	\
				act = (y);	\
				nargs = (z);	\
				break;

#define TEST_FILE(x,y,z)	do {	\
				if (!(x).good())	\
				{	\
					std::cerr << "File '" << argv[(y)] << "' could not be opened." << std::endl << std::endl;	\
					return (z);	\
				}	\
			} while (0)


int main(int argc, char *argv[])
{
	int sonic2 = 0, tosonic2 = 0, fromsonic2 = 0;
	static struct option long_options[] = {
		{"optimize"      , no_argument      , 0, 'o'},
		{"split"         , no_argument      , 0, 's'},
		{"merge"         , no_argument      , 0, 'm'},
		{"fix"           , no_argument      , 0, 'f'},
		{"convert"       , no_argument      , 0, 'c'},
		{"info"          , no_argument      , 0, 'i'},
		{"dplc"          , no_argument      , 0, 'd'},
		{"pal-change"    , required_argument, 0, 'p'},
		{"pal-dest"      , required_argument, 0, 'a'},
		{"from-sonic2", no_argument, &fromsonic2, 1},
		{"to-sonic2"  , no_argument, &tosonic2  , 1},
		{"sonic2"     , no_argument, &sonic2    , 1},
		{0, 0, 0, 0}
	};

	Actions act = eNone;
	int nargs = 0, srcpal = -1, dstpal = -1;

	while (true)
	{
		int option_index = 0;
		int c = getopt_long(argc, argv, "osmfcidp:a:",
                            long_options, &option_index);
		if (c == -1)
			break;
		
		switch (c)
		{
			ARG_CASE('o',eOptimize,4)
			ARG_CASE('s',eSplit,3)
			ARG_CASE('m',eMerge,3)
			ARG_CASE('f',eFix,2)
			ARG_CASE('c',eConvert,2)
			ARG_CASE('i',eInfo,1)
			ARG_CASE('d',eDplc,1)
			case 'p':
				if (act != eNone)
				{
					usage();
					return eInvalidArgs;
				}
				act = ePalChange;
				nargs = 2;
				srcpal = (strtoul(optarg, 0, 0) & 3) << 5;
				break;
			case 'a':
				if (act != ePalChange)
				{
					usage();
					return eInvalidArgs;
				}
				nargs = 2;
				dstpal = (strtoul(optarg, 0, 0) & 3) << 5;
				break;
		}
	}

	if (argc - optind < nargs || act == eNone)
	{
		usage();
		return eInvalidArgs;
	}

	switch (act)
	{
		case eOptimize:
		{
			std::ifstream inmaps(argv[optind+0], std::ios::in|std::ios::binary),
			              indplc(argv[optind+1], std::ios::in|std::ios::binary);
			TEST_FILE(inmaps, optind+0, eInputMapsMissing);
			TEST_FILE(indplc, optind+1, eInputDplcMissing);

			mapping_file srcmaps;
			srcmaps.read(inmaps, sonic2);
			inmaps.close();

			dplc_file    srcdplc;
			srcdplc.read(indplc);
			indplc.close();

			mapping_file intmaps;
			intmaps.merge(srcmaps, srcdplc);

			mapping_file dstmaps;
			dplc_file    dstdplc;
			dstmaps.split(intmaps, dstdplc);

			std::ofstream outmaps(argv[optind+2], std::ios::out|std::ios::binary),
			              outdplc(argv[optind+3], std::ios::out|std::ios::binary);
			TEST_FILE(outmaps, optind+2, eOutputMapsMissing);
			TEST_FILE(outdplc, optind+3, eOutputDplcMissing);

			dstmaps.write(outmaps, sonic2);
			outmaps.close();

			dstdplc.write(outdplc);
			outdplc.close();
		}
		case eSplit:
		{
			std::ifstream inmaps(argv[optind+0], std::ios::in|std::ios::binary);
			TEST_FILE(inmaps, optind+0, eInputMapsMissing);

			mapping_file srcmaps;
			srcmaps.read(inmaps, sonic2);
			inmaps.close();

			mapping_file dstmaps;
			dplc_file    dstdplc;
			dstmaps.split(srcmaps, dstdplc);

			std::ofstream outmaps(argv[optind+1], std::ios::out|std::ios::binary),
			              outdplc(argv[optind+2], std::ios::out|std::ios::binary);
			TEST_FILE(outmaps, optind+1, eOutputMapsMissing);
			TEST_FILE(outdplc, optind+2, eOutputDplcMissing);

			dstmaps.write(outmaps, sonic2);
			outmaps.close();

			dstdplc.write(outdplc);
			outdplc.close();
		}
		case eMerge:
		{
			std::ifstream inmaps(argv[optind+0], std::ios::in|std::ios::binary),
			              indplc(argv[optind+1], std::ios::in|std::ios::binary);
			TEST_FILE(inmaps, optind+0, eInputMapsMissing);
			TEST_FILE(indplc, optind+1, eInputDplcMissing);

			mapping_file srcmaps;
			srcmaps.read(inmaps, sonic2);
			inmaps.close();

			dplc_file    srcdplc;
			srcdplc.read(indplc);
			indplc.close();

			mapping_file dstmaps;
			dstmaps.merge(srcmaps, srcdplc);

			std::ofstream outmaps(argv[optind+2], std::ios::out|std::ios::binary);
			TEST_FILE(outmaps, optind+2, eOutputMapsMissing);

			dstmaps.write(outmaps, sonic2);
			outmaps.close();
		}
		case eFix:
		{
			std::ifstream inmaps(argv[optind+0], std::ios::in|std::ios::binary);
			TEST_FILE(inmaps, optind+0, eInputMapsMissing);

			mapping_file srcmaps;
			srcmaps.read(inmaps, sonic2);
			inmaps.close();

			std::ofstream outmaps(argv[optind+1], std::ios::out|std::ios::binary);
			TEST_FILE(outmaps, optind+1, eOutputMapsMissing);

			srcmaps.write(outmaps, sonic2);
			outmaps.close();
		}
		case eConvert:
		{
			std::ifstream inmaps(argv[optind+0], std::ios::in|std::ios::binary);
			TEST_FILE(inmaps, optind+0, eInputMapsMissing);

			mapping_file srcmaps;
			srcmaps.read(inmaps, fromsonic2);
			inmaps.close();

			std::ofstream outmaps(argv[optind+1], std::ios::out|std::ios::binary);
			TEST_FILE(outmaps, optind+1, eOutputMapsMissing);

			srcmaps.write(outmaps, tosonic2);
			outmaps.close();
		}
		case eInfo:
		{
			std::ifstream inmaps(argv[optind+0], std::ios::in|std::ios::binary);
			TEST_FILE(inmaps, optind+0, eInputMapsMissing);

			mapping_file srcmaps;
			srcmaps.read(inmaps, fromsonic2);
			inmaps.close();
			srcmaps.print();
		}
		case eDplc:
		{
			std::ifstream indplc(argv[optind+0], std::ios::in|std::ios::binary);
			TEST_FILE(indplc, optind+0, eInputDplcMissing);

			dplc_file    srcdplc;
			srcdplc.read(indplc);
			indplc.close();
			srcdplc.print();
		}
		case ePalChange:
		{
			if (srcpal < 0 || dstpal < 0)
			{
				usage();
				return eInvalidArgs;
			}
			std::ifstream inmaps(argv[optind+0], std::ios::in|std::ios::binary);
			TEST_FILE(inmaps, optind+0, eInputMapsMissing);

			mapping_file srcmaps;
			srcmaps.read(inmaps, sonic2);
			inmaps.close();
			srcmaps.change_pal(srcpal, dstpal);

			std::ofstream outmaps(argv[optind+1], std::ios::out|std::ios::binary);
			TEST_FILE(outmaps, optind+1, eOutputMapsMissing);

			srcmaps.write(outmaps, sonic2);
			outmaps.close();
		}
	}
	
	return 0;
}
