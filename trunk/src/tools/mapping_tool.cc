/* -*- Mode: C++; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * Copyright (C) Flamewing 2011-2013 <flamewing.sonic@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <map>

#include "getopt.h"
#include "mappingfile.h"
#include "dplcfile.h"

static void usage() {
	std::cerr << "Usage: mapping_tool {-c|--crush-mappings} [OPTIONS] INPUT_MAPS OUTPUT_MAPS" << std::endl;
	std::cerr << "\tPerforms several size optimizations on the mappings file. All other options that write mappings also perform these" << std::endl
	          << "\toptimizations, so this option should be used only if you don't want those side-effects." << std::endl << std::endl;
	std::cerr << "Usage: mapping_tool {-k|--crush-dplc} [OPTIONS] INPUT_DPLC OUTPUT_DPLC" << std::endl;
	std::cerr << "\tPerforms several size optimizations on the DPLC file. All other options that write DPLC files also perform these" << std::endl
	          << "\toptimizations, so this option should be used only if you don't want those side-effects." << std::endl << std::endl;
	std::cerr << "Usage: mapping_tool {-o|--optimize} [OPTIONS] INPUT_MAPS INPUT_DPLC OUTPUT_MAPS OUTPUT_DPLC" << std::endl;
	std::cerr << "\tPerforms joint optimization on the input mappings and DPLC files to use as few DMA transfers as possible." << std::endl << std::endl;
	std::cerr << "Usage: mapping_tool {-s|--split} [OPTIONS] INPUT_MAPS OUTPUT_MAPS OUTPUT_DPLC" << std::endl;
	std::cerr << "\tConverts a mappings file into a mappings + DPLC pair, optimized to use as few DMA transfers as possible." << std::endl << std::endl;
	std::cerr << "Usage: mapping_tool {-m|--merge} [OPTIONS] INPUT_MAPS INPUT_DPLC OUTPUT_MAPS" << std::endl;
	std::cerr << "\tConverts a mappings + DPLC pair into a non-DPLC mappings file. Warning: this will not work correctly if there are" << std::endl
	          << "\tmore than 0x1000 (4096) tiles in the art file." << std::endl << std::endl;
	std::cerr << "Usage: mapping_tool {-f|--fix} [OPTIONS] INPUT_MAPS OUTPUT_MAPS" << std::endl;
	std::cerr << "\tSonic 2 only. Fixes the SonMapED mappings file to the correct tile value for double resolution 2-player mode." << std::endl << std::endl;
	std::cerr << "Usage: mapping_tool {-p|--pal-change=SRC} {-a|--pal-dest=DST} [OPTIONS] INPUT_MAPS" << std::endl;
	std::cerr << "\tWrites new mappings with pieces matching the SRC palette bits changed to palette line DST." << std::endl << std::endl;
	std::cerr << "Usage: mapping_tool {-i|--info} [OPTIONS] INPUT_MAPS" << std::endl;
	std::cerr << "\tGives information about the mappings file." << std::endl << std::endl;
	std::cerr << "Usage: mapping_tool {-d|--dplc} [OPTIONS] INPUT_DPLC" << std::endl;
	std::cerr << "\tGives information about the DPLC file." << std::endl << std::endl;
	std::cerr << "\tWarning: the optimized mappings + DPLC files generated by this utility work perfectly in-game, but do not work in" << std::endl
	          << "\tSonMapED. Complains about this should go to Xenowhirl." << std::endl << std::endl;

	std::cerr << "Available options are:" << std::endl;
	std::cerr << "\t-0, --no-null   \tDisables null first frame optimization. This affects both mappings and DPLC. It has no effects" << std::endl
	          << "\t                \ton S3&K non-player DPLCs, which do not support this optimization. Disabling this optimization" << std::endl
	          << "\t                \tincreases the odds of SonMapEd being able to load the mappings or DPLC files generated, but it" << std::endl
	          << "\t                \tis not a guarantee. Complaints about SonMapEd should go to Xenowhirl." << std::endl;
	std::cerr << "\t--sonic=VER     \tShorthand for using both --from=sonic=VER and --to-sonic=VER." << std::endl;
	std::cerr << "\t--from-sonic=VER\tSpecifies the game engine version (format) for input mappings and DPLC." << std::endl;
	std::cerr << "\t--to-sonic=VER  \tSpecifies the game engine version (format) for output mappings and DPLC." << std::endl;
	std::cerr << "\t                \tThe following values are accepted for the game engine version:" << std::endl;
	std::cerr << "\t                \tVER=1\tSonic 1 mappings and DPLC." << std::endl;
	std::cerr << "\t                \tVER=2\tSonic 2 mappings and DPLC." << std::endl;
	std::cerr << "\t                \tVER=3\tSonic 3 mappings and DPLC, as used by player objects." << std::endl;
	std::cerr << "\t                \tVER=4\tSonic 3 mappings and DPLC, as used by non-player objects." << std::endl;
	std::cerr << "\t                \tInvalid values or unspecified options will default to Sonic 2 format all cases." << std::endl << std::endl;
}

enum Actions {
	eNone = 0,
	eOptimize,
	eSplit,
	eMerge,
	eFix,
	eConvert,
	eConvertDPLC,
	eInfo,
	eDplc,
	ePalChange
};

enum FileErrors {
	eInvalidArgs = 1,
	eInputMapsMissing,
	eInputDplcMissing,
	eOutputMapsMissing,
	eOutputDplcMissing
};

#define ARG_CASE(x,y,z,w)   case (x):   \
	if (act != eNone)   \
	{   \
		usage();    \
		return eInvalidArgs;    \
	}   \
	act = (y);  \
	nargs = (z);    \
	w;  \
	break;

#define TEST_FILE(x,y,z)    do {    \
		if (!(x).good())    \
		{   \
			std::cerr << "File '" << argv[(y)] << "' could not be opened." << std::endl << std::endl;   \
			return (z); \
		}   \
	} while (0)


int main(int argc, char *argv[]) {
	static struct option long_options[] = {
		{"optimize"      , no_argument      , 0, 'o'},
		{"split"         , no_argument      , 0, 's'},
		{"merge"         , no_argument      , 0, 'm'},
		{"fix"           , no_argument      , 0, 'f'},
		{"crush-mappings", no_argument      , 0, 'c'},
		{"crush-dplc"    , no_argument      , 0, 'k'},
		{"info"          , no_argument      , 0, 'i'},
		{"dplc"          , no_argument      , 0, 'd'},
		{"no-null"       , no_argument      , 0, '0'},
		{"pal-change"    , required_argument, 0, 'p'},
		{"pal-dest"      , required_argument, 0, 'a'},
		{"from-sonic"    , required_argument, 0, 'x'},
		{"to-sonic"      , required_argument, 0, 'y'},
		{"sonic"         , required_argument, 0, 'z'},
		{0, 0, 0, 0}
	};

	Actions act = eNone;
	bool nullfirst = true;
	int nargs = 0, srcpal = -1, dstpal = -1;
	int tosonicver = 2, fromsonicver = 2;

	while (true) {
		int option_index = 0;
		int c = getopt_long(argc, argv, "osmfckidp:a:0",
		                    long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
			case '0':
				nullfirst = false;
				break;
			case 'x':
				fromsonicver = strtoul(optarg, 0, 0);
				if (fromsonicver < 1 || fromsonicver > 4)
					fromsonicver = 2;
				break;
			case 'y':
				tosonicver = strtoul(optarg, 0, 0);
				if (tosonicver < 1 || tosonicver > 4)
					tosonicver = 2;
				break;
			case 'z':
				fromsonicver = tosonicver = strtoul(optarg, 0, 0);
				if (fromsonicver < 1 || fromsonicver > 4)
					fromsonicver = tosonicver = 2;
				break;
				ARG_CASE('o', eOptimize, 4,)
				ARG_CASE('s', eSplit, 3,)
				ARG_CASE('m', eMerge, 3,)
				ARG_CASE('f', eFix, 2,)
				ARG_CASE('c', eConvert, 2,)
				ARG_CASE('k', eConvertDPLC, 2,)
				ARG_CASE('i', eInfo, 1,)
				ARG_CASE('d', eDplc, 1,)
				ARG_CASE('p', ePalChange, 2, srcpal = (strtoul(optarg, 0, 0) & 3) << 5)
				/*case 'p':
				    if (act != eNone)
				    {
				        usage();
				        return eInvalidArgs;
				    }
				    act = ePalChange;
				    nargs = 2;
				    srcpal = (strtoul(optarg, 0, 0) & 3) << 5;
				    break;*/
			case 'a':
				if (act != ePalChange) {
					usage();
					return eInvalidArgs;
				}
				nargs = 2;
				dstpal = (strtoul(optarg, 0, 0) & 3) << 5;
				break;
		}
	}

	if (argc - optind < nargs || act == eNone) {
		usage();
		return eInvalidArgs;
	}

	switch (act) {
		case eOptimize: {
			std::ifstream inmaps(argv[optind + 0], std::ios::in | std::ios::binary),
			    indplc(argv[optind + 1], std::ios::in | std::ios::binary);
			TEST_FILE(inmaps, optind + 0, eInputMapsMissing);
			TEST_FILE(indplc, optind + 1, eInputDplcMissing);

			mapping_file srcmaps;
			srcmaps.read(inmaps, fromsonicver);
			inmaps.close();

			dplc_file    srcdplc;
			srcdplc.read(indplc, fromsonicver);
			indplc.close();

			//mapping_file intmaps;
			//intmaps.merge(srcmaps, srcdplc);

			mapping_file dstmaps;
			dplc_file    dstdplc;
			//dstmaps.split(intmaps, dstdplc);
			dstmaps.optimize(srcmaps, srcdplc, dstdplc);

			std::ofstream outmaps(argv[optind + 2], std::ios::out | std::ios::binary | std::ios::trunc),
			    outdplc(argv[optind + 3], std::ios::out | std::ios::binary | std::ios::trunc);
			TEST_FILE(outmaps, optind + 2, eOutputMapsMissing);
			TEST_FILE(outdplc, optind + 3, eOutputDplcMissing);

			dstmaps.write(outmaps, tosonicver, nullfirst);
			outmaps.close();

			dstdplc.write(outdplc, tosonicver, nullfirst);
			outdplc.close();
			break;
		}
		case eSplit: {
			std::ifstream inmaps(argv[optind + 0], std::ios::in | std::ios::binary);
			TEST_FILE(inmaps, optind + 0, eInputMapsMissing);

			mapping_file srcmaps;
			srcmaps.read(inmaps, fromsonicver);
			inmaps.close();

			mapping_file dstmaps;
			dplc_file    dstdplc;
			dstmaps.split(srcmaps, dstdplc);

			std::ofstream outmaps(argv[optind + 1], std::ios::out | std::ios::binary | std::ios::trunc),
			    outdplc(argv[optind + 2], std::ios::out | std::ios::binary | std::ios::trunc);
			TEST_FILE(outmaps, optind + 1, eOutputMapsMissing);
			TEST_FILE(outdplc, optind + 2, eOutputDplcMissing);

			dstmaps.write(outmaps, tosonicver, nullfirst);
			outmaps.close();

			dstdplc.write(outdplc, tosonicver, nullfirst);
			outdplc.close();
			break;
		}
		case eMerge: {
			std::ifstream inmaps(argv[optind + 0], std::ios::in | std::ios::binary),
			    indplc(argv[optind + 1], std::ios::in | std::ios::binary);
			TEST_FILE(inmaps, optind + 0, eInputMapsMissing);
			TEST_FILE(indplc, optind + 1, eInputDplcMissing);

			mapping_file srcmaps;
			srcmaps.read(inmaps, fromsonicver);
			inmaps.close();

			dplc_file    srcdplc;
			srcdplc.read(indplc, fromsonicver);
			indplc.close();

			mapping_file dstmaps;
			dstmaps.merge(srcmaps, srcdplc);

			std::ofstream outmaps(argv[optind + 2], std::ios::out | std::ios::binary | std::ios::trunc);
			TEST_FILE(outmaps, optind + 2, eOutputMapsMissing);

			dstmaps.write(outmaps, tosonicver, nullfirst);
			outmaps.close();
			break;
		}
		case eFix: {
			std::ifstream inmaps(argv[optind + 0], std::ios::in | std::ios::binary);
			TEST_FILE(inmaps, optind + 0, eInputMapsMissing);

			mapping_file srcmaps;
			srcmaps.read(inmaps, fromsonicver);
			inmaps.close();

			std::ofstream outmaps(argv[optind + 1], std::ios::out | std::ios::binary | std::ios::trunc);
			TEST_FILE(outmaps, optind + 1, eOutputMapsMissing);

			srcmaps.write(outmaps, tosonicver, nullfirst);
			outmaps.close();
			break;
		}
		case eConvert: {
			std::ifstream inmaps(argv[optind + 0], std::ios::in | std::ios::binary);
			TEST_FILE(inmaps, optind + 0, eInputMapsMissing);

			mapping_file srcmaps;
			srcmaps.read(inmaps, fromsonicver);
			inmaps.close();

			std::ofstream outmaps(argv[optind + 1], std::ios::out | std::ios::binary | std::ios::trunc);
			TEST_FILE(outmaps, optind + 1, eOutputMapsMissing);

			srcmaps.write(outmaps, tosonicver, nullfirst);
			outmaps.close();
			break;
		}
		case eConvertDPLC: {
			std::ifstream indplc(argv[optind + 0], std::ios::in | std::ios::binary);
			TEST_FILE(indplc, optind + 0, eInputDplcMissing);

			dplc_file srcdplc;
			srcdplc.read(indplc, fromsonicver);
			indplc.close();

			std::ofstream outdplc(argv[optind + 1], std::ios::out | std::ios::binary | std::ios::trunc);
			TEST_FILE(outdplc, optind + 1, eOutputDplcMissing);

			srcdplc.write(outdplc, tosonicver, nullfirst);
			outdplc.close();
			break;
		}
		case eInfo: {
			std::ifstream inmaps(argv[optind + 0], std::ios::in | std::ios::binary);
			TEST_FILE(inmaps, optind + 0, eInputMapsMissing);

			mapping_file srcmaps;
			srcmaps.read(inmaps, fromsonicver);
			inmaps.close();
			srcmaps.print();
			break;
		}
		case eDplc: {
			std::ifstream indplc(argv[optind + 0], std::ios::in | std::ios::binary);
			TEST_FILE(indplc, optind + 0, eInputDplcMissing);

			dplc_file    srcdplc;
			srcdplc.read(indplc, fromsonicver);
			indplc.close();
			srcdplc.print();
			break;
		}
		case ePalChange: {
			if (srcpal < 0 || dstpal < 0) {
				usage();
				return eInvalidArgs;
			}
			std::ifstream inmaps(argv[optind + 0], std::ios::in | std::ios::binary);
			TEST_FILE(inmaps, optind + 0, eInputMapsMissing);

			mapping_file srcmaps;
			srcmaps.read(inmaps, fromsonicver);
			inmaps.close();
			srcmaps.change_pal(srcpal, dstpal);

			std::ofstream outmaps(argv[optind + 1], std::ios::out | std::ios::binary | std::ios::trunc);
			TEST_FILE(outmaps, optind + 1, eOutputMapsMissing);

			srcmaps.write(outmaps, tosonicver, nullfirst);
			outmaps.close();
			break;
		}
		default:
			std::cerr << "Divide By Cucumber Error. Please Reinstall Universe And Reboot." << std::endl;
			break;
	}

	return 0;
}
