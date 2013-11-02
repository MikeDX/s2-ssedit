/* -*- Mode: C++; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * Copyright (C) Flamewing 2013 <flamewing.sonic@gmail.com>
 * Very loosely based on code by the KENS Project Development Team
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

#include <istream>
#include <ostream>
#include <sstream>
#include <cstdio>

#include "saxman.h"
#include "bigendian_io.h"
#include "bitstream.h"
#include "lzss.h"

void saxman::decode_internal(std::istream &in, std::iostream &Dst,
                             std::streamsize const BSize) {
	ibitstream<unsigned char, littleendian<unsigned char> > bits(in);

	// Loop while the file is good and we haven't gone over the declared length.
	while (in.good() && in.tellg() < BSize) {
		if (bits.popd()) {
			// Literal.
			if (in.peek() == EOF)
				break;
			Write1(Dst, Read1(in));
		} else {
			if (in.peek() == EOF)
				break;
			// Offset and length of match.
			size_t offset = Read1(in);
			size_t length = Read1(in);

			// The high 4 bits of length are actually part of the offset.
			offset |= (length << 4) & 0xF00;
			// And there is an additional 0x12 bytes added to offset.
			offset = (offset + 0x12) & 0xFFF;
			// Also, a part of the offset is determined by the current output
			// position.
			size_t basedest = Dst.tellp();
			offset |= basedest & 0xF000;
			if (offset >= basedest) {
				// But don't overshoot the destination.
				offset -= 0x1000;
				offset &= 0xFFFF;
			}
			
			// Length is low 4 bits plus 3.
			length = (length & 0xF) + 3;

			if (offset < basedest) {
				// If the offset is before the current output position, we copy
				// bytes from the given location.
				for (size_t src = offset; src < offset + length; src++) {
					std::streampos Pointer = Dst.tellp();
					Dst.seekg(src);
					unsigned short Byte = Read1(Dst);
					Dst.seekp(Pointer);
					Write1(Dst, Byte);
				}
			} else {
				// Otherwise, it is a zero fill.
				for (size_t ii = 0; ii < length; ii++) {
					Write1(Dst, 0x00);
				}
			}
		}
	}
}

bool saxman::decode(std::istream &Src, std::iostream &Dst,
                    std::streampos Location, std::streamsize const BSize) {
	Src.seekg(Location);
	size_t size = BSize == 0 ? LittleEndian::Read2(Src) : BSize;

	std::stringstream in(std::ios::in | std::ios::out | std::ios::binary);
	in << Src.rdbuf();

	in.seekg(0);
	decode_internal(in, Dst, size);
	return true;
}

// NOTE: This has to be changed for other LZSS-based compression schemes.
struct SaxmanAdaptor {
	typedef unsigned char stream_t;
	typedef unsigned char descriptor_t;
	typedef littleendian<descriptor_t> descriptor_endian_t;
	enum {
		// Number of bits on descriptor bitfield.
		NumDescBits = sizeof(descriptor_t) * 8,
		// Number of bits used in descriptor bitfield to signal the end-of-file
		// marker sequence.
		NumTermBits = 0,
		// Flag that tells the compressor that new descriptor fields are needed
		// as soon as the last bit in the previous one is used up.
		NeedEarlyDescriptor = 0,
		// Flag that marks the descriptor bits as being in little-endian bit
		// order (that is, lowest bits come out first).
		DescriptorLittleEndianBits = 1
	};
	// Computes the cost of covering all of the "len" vertices starting from
	// "off" vertices ago.
	// A return of "std::numeric_limits<size_t>::max()" means "infinite",
	// or "no edge".
	static size_t calc_weight(size_t dist, size_t len) {
		// Preconditions:
		// len != 0 && len <= RecLen && dist != 0 && dist <= SlideWin
		if (len == 1)
			// Literal: 1-bit descriptor, 8-bit length.
			return 1 + 8;
		else if (len == 2)
			return std::numeric_limits<size_t>::max();	// "infinite"
		else
			// RLE: 1-bit descriptor, 12-bit offset, 4-bit length.
			return 1 + 12 + 4;
	}
	// Given an edge, computes how many bits are used in the descriptor field.
	static size_t desc_bits(AdjListNode const &edge) {
		// Saxman always uses a single bit descriptor.
		return 1;
	}
	// Saxman allows encoding of a sequence of zeroes with no previous match.
	static void extra_matches(stream_t const *data, size_t basenode,
	                          size_t ubound, size_t lbound,
	                          LZSSGraph<SaxmanAdaptor>::MatchVector &matches) {
		// Can't encode zero match after this point.
		if (basenode >= 0xFFF)
			return;
		// Try matching zeroes.
		size_t jj = 0;
		while (data[basenode + jj] == 0) {
			if (++jj >= ubound)
				break;
		}
		// Need at least 3 zeroes in sequence.
		if (jj >= 3) {
			// Got them, so add them to the list.
			matches[jj - 1] = AdjListNode(basenode + jj,
			                              std::numeric_limits<size_t>::max(),
			                              jj, 1 + 12 + 4);
		}
	}
};

typedef LZSSGraph<SaxmanAdaptor> SaxGraph;
typedef LZSSOStream<SaxmanAdaptor> SaxOStream;

void saxman::encode_internal(std::ostream &Dst, unsigned char const *&Buffer,
                             std::streamsize const BSize) {
	// Compute optimal Saxman parsing of input file.
	SaxGraph enc(Buffer, BSize, 0x1000, 0x12);
	SaxGraph::AdjList list = enc.find_optimal_parse();
	SaxOStream out(Dst);

	std::streamoff pos = 0;
	// Go through each edge in the optimal path.
	for (SaxGraph::AdjList::const_iterator it = list.begin();
	        it != list.end(); ++it) {
		AdjListNode const &edge = *it;
		size_t len = edge.get_length(), dist = edge.get_distance();
		// The weight of each edge uniquely identifies how it should be written.
		// NOTE: This needs to be changed for other LZSS schemes.
		if (edge.get_length() == 1) {
			// Literal.
			out.descbit(1);
			out.putbyte(Buffer[pos]);
		} else {
			// RLE.
			out.descbit(0);
			size_t low = pos - edge.get_distance(), high = edge.get_length();
			low = (low - 0x12) & 0xFFF;
			high = ((high - 3) & 0x0F) | ((low >> 4) & 0xF0);
			low &= 0xFF;
			out.putbyte(low);
			out.putbyte(high);
		}
		// Go to next position.
		pos = edge.get_dest();
	}
}

bool saxman::encode(std::istream &Src, std::ostream &Dst, bool WithSize) {
	Src.seekg(0, std::ios::end);
	std::streamsize BSize = Src.tellg();
	Src.seekg(0);
	unsigned char *const Buffer = new unsigned char[BSize];
	unsigned char const *ptr = Buffer;
	Src.read((char *)ptr, BSize);

	// Internal buffer.
	std::stringstream outbuff(std::ios::in | std::ios::out | std::ios::binary);
	encode_internal(outbuff, ptr, BSize);
	if (WithSize) {
		outbuff.seekg(0, std::ios::end);
		LittleEndian::Write2(Dst, outbuff.tellg());
	}
	outbuff.seekg(0);
	Dst << outbuff.rdbuf();

	delete [] Buffer;
	return true;
}