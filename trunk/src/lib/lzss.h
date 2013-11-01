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

#ifndef _LZSS_H_
#define _LZSS_H_

#include <iosfwd>
#include <limits>
#include <string>
#include "bigendian_io.h"
#include "bitstream.h"

/*
 * Class representing an edge in the LZSS-compression graph. An edge (u, v)
 * indicates that there is a sliding window match that covers all the characters
 * in the [u, v) range (half-open) -- that is, node v is not part of the match.
 * Each node is a character in the file, and is represented by its position.
 */
class AdjListNode {
private:
	// The first character after the match ends.
	size_t destnode;
	// Cost, in bits, of "covering" all of the characters in the match.
	size_t weight;
	// How many characters back does the match begin at.
	size_t distance;
	// How long the match is.
	size_t length;
public:
	// Constructors.
	AdjListNode()
		: destnode(1), distance(1), length(1),
		  weight(std::numeric_limits<size_t>::max()) {
	}
	AdjListNode(size_t dest, size_t dist, size_t len,
	            size_t wgt)
		: destnode(dest), distance(dist), length(len), weight(wgt) {
	}
	// Getters.
	size_t get_dest() const {
		return destnode;
	}
	size_t get_weight() const {
		return weight;
	}
	size_t get_distance() const {
		return distance;
	}
	size_t get_length() const {
		return length;
	}
	// Comparison operator. Lowest weight first, on tie, break by shortest
	// length, on further tie break by distance. Used only on the multimap.
	bool operator<(AdjListNode const &other) const {
		if (weight < other.weight)
			return true;
		else if (weight > other.weight)
			return false;
		if (length < other.length)
			return true;
		else if (length > other.length)
			return false;
		else
			return distance < other.distance;
	}
};

/*
 * Graph structure for optimal LZSS encoding. This graph is a directed acyclic
 * graph (DAG) by construction, and is automatically sorted topologically.
 * The template parameter is an adaptor class/structure with the following
 * members:
 * struct LZSSAdaptor {
 * 	enum {
 * 		// Number of bits on descriptor bitfield.
 * 		NumDescBits = 16,
 * 		// Number of bits used in descriptor bitfield to signal the end-of-file
 * 		// marker sequence.
 * 		NumTermBits = 2
 * 	};
 * 	// Computes the cost of covering all of the "len" vertices starting from
 * 	// "off" vertices ago.
 *  // A return of "std::numeric_limits<size_t>::max()" means "infinite",
 *  // or "no edge".
 * 	static size_t calc_weight(size_t dist, size_t len);
 * 	// Given an edge, computes how many bits are used in the descriptor field.
 * 	static size_t desc_bits(AdjListNode const &edge);
 * };
 */
template<typename Adaptor>
class LZSSGraph {
public:
	typedef std::list<AdjListNode> AdjList;
private:
	typedef std::vector<AdjListNode> MatchVector;
	// Source file data and its size; one node per character in source file.
	unsigned char const *data;
	size_t const nlen;
	// Parameters for LZSS encoder: sliding window size and maximum record
	// length to use when encoding.
	size_t const SlideWin;
	size_t const RecLen;
	// Adjacency lists for all the nodes in the graph.
	std::vector<AdjList> adjs;

	/*
	 * TODO: Improve speed with a smarter way to perform matches.
	 * This is the main workhorse and bottleneck: it finds the least costly way
	 * to reach all possible nodes reachable from the basenode and inserts them
	 * into a map.
	 */
	MatchVector find_matches(size_t basenode) {
		// Upper and lower bounds for sliwing window, starting node.
		size_t ubound = std::min(RecLen, nlen - basenode),
		       lbound = basenode > SlideWin ? basenode - SlideWin : 0,
		       ii = basenode - 1;
		// This is what we produce.
		MatchVector matches(ubound);
		// Start with the "literal" match of the current node into the next.
		size_t wgt = Adaptor::calc_weight(0, 1);
		matches[0] = AdjListNode(basenode + 1, 0, 1, wgt);
		do {
			// Keep looking for matches.
			size_t jj = 0;
			while (data[ii + jj] == data[basenode + jj]) {
				++jj;
				// We have found a match that links (basenode) with
				// (basenode + jj) with length (jj) and distance (basenode-ii).
				// Add it to the list.
				size_t wgt = Adaptor::calc_weight(basenode - ii, jj);
				if (wgt != std::numeric_limits<size_t>::max()) {
					AdjListNode &best = matches[jj - 1];
					if (wgt < best.get_weight()) {
						best = AdjListNode(basenode + jj, basenode - ii, jj,
						                   wgt);
					}
				}
				// We can find no more matches with the current starting node.
				if (jj >= ubound)
					break;
			}
		} while (ii-- > lbound);

		return matches;
	}
public:
	// Constructor: creates the graph from the input file.
	LZSSGraph(unsigned char const *dt, size_t const size,
	          size_t const win, size_t const rec)
		: data(dt), nlen(size), SlideWin(win), RecLen(rec) {
		// Making space for all nodes.
		adjs.resize(nlen);
		// First node is special.
		adjs[0].push_front(AdjListNode(1, 1, 1, Adaptor::calc_weight(1, 1)));
		for (size_t ii = 1; ii < nlen; ii++) {
			// Find all matches for all subsequent nodes.
			MatchVector const matches = find_matches(ii);
			for (MatchVector::const_iterator it = matches.begin();
			     it != matches.end(); ++it) {
				// Insert the best (lowest cost) edge linking these two nodes.
				if (it->get_weight() != std::numeric_limits<size_t>::max()) {
					adjs[ii].push_back(*it);
				}
			}
		}
	}
	/*
	 * This function returns the shortest path through the file.
	 */
	AdjList find_optimal_parse() const {
		// Auxiliary data structures:
		// * The parent of a node is the node that reaches that node with the
		//   lowest cost from the start of the file.
		std::vector<size_t> parents(nlen + 1);
		// * This is the edge used to go from the parent of a node to said node.
		std::vector<AdjListNode> pedges(nlen + 1);
		// * This is the total cost to reach the edge. They start as high as
		//   possible for all nodes but the first, which starts at 0.
		std::vector<size_t> costs(nlen + 1, std::numeric_limits<size_t>::max());
		costs[0] = 0;
		// * And this is a vector that tallies up the amount of unused bits in
		//   the descriptor bitfield for the shortest path up to this node.
		//   After tallying up the ending node, the end-of-file marker may cause
		//   an additional dummy descriptor bitfield to be emitted; this vector
		//   is used to counteract that.
		std::vector<size_t> desccosts(nlen + 1, 0);

		// Since the LZSS graph is a topologically-sorted DAG by construction,
		// computing the shortest distance is very quick and easy: just go
		// through the nodes in order and update the distances.
		for (size_t ii = 0; ii < nlen; ii++) {
			// Get the adjacency list for this node.
			AdjList const &list = adjs[ii];
			// Get remaining unused descriptor bits up to this node.
			size_t basedesc = desccosts[ii];
			for (AdjList::const_iterator it = list.begin();
			        it != list.end(); ++it) {
				// Need destination ID and edge weight.
				size_t nextnode = it->get_dest(), wgt = it->get_weight();
				// Compute remaining unused bits from using this edge.
				size_t desccost = basedesc + Adaptor::desc_bits(*it);
				desccost %= Adaptor::NumDescBits;
				if (nextnode == nlen) {
					// This is the ending node. Add the descriptor bits for the
					// end-of-file marker and wrap the descriptor.
					desccost += Adaptor::NumTermBits;
					desccost %= Adaptor::NumDescBits;
					// If the descriptor bitfield had exactly 0 bits left after
					// this, we need to emit a new descriptor bitfield (the full
					// Adaptor::NumDescBits bits). Otherwise, we need to pads
					// the last descriptor bitfield to full size. This line
					// accomplishes both.
					wgt += (Adaptor::NumDescBits - desccost);
				}
				// Is the cost to reach the target node through this edge less
				// than the current cost?
				if (costs[nextnode] > costs[ii] + wgt) {
					// If so, update the data structures with new best edge.
					costs[nextnode] = costs[ii] + wgt;
					parents[nextnode] = ii;
					pedges[nextnode] = *it;
					desccosts[nextnode] = desccost;
				}
			}
		}

		// This is what we will produce.
		AdjList parselist;
		for (size_t ii = nlen; ii != 0;) {
			// Insert the edge up front...
			parselist.push_front(pedges[ii]);
			// ... and switch to parent node.
			ii = parents[ii];
		}

		// We are done: this is the optimal parsing of the input file, giving
		// *the* best possible compressed file size.
		return parselist;
	}
};

/*
 * This class abstracts away an LZSS output stream composed of one or more bytes
 * in a descriptor bitfield, followed by byte parameters. It manages the output
 * by buffering the bytes until a descriptor field is full, at which point it
 * writes the descriptor field and flushes the output buffer.
 */
template <typename T, typename BitWriter = bigendian<T>,
          bool little_endian_bits = false>
class LZSSOStream {
private:
	// Where we will output to.
	std::ostream &out;
	// Internal bitstream output buffer.
	obitstream<T, BitWriter> bits;
	// Internal parameter buffer.
	std::string buffer;
	bool putbit(T bit) {
		if (little_endian_bits)
			return bits.push(bit);
		else
			return bits.put(bit);
	}
public:
	// Constructor.
	LZSSOStream(std::ostream &Dst) : out(Dst), bits(out) {
	}
	// Destructor: writes anything that hasn't been written.
	~LZSSOStream() {
		// We need a dummy descriptor field if we have exactly zero bits left
		// on the previous descriptor field; this is because the decoder will
		// immediately fetch a new descriptor field when the previous one has
		// expired, and we don't want it to be the terminating sequence.
		// First, save current state.
		bool needdummydesc = !bits.have_waiting_bits();
		// Now, flush the queue if needed.
		bits.flush(little_endian_bits);
		if (needdummydesc) {
			// We need to add a dummy descriptor field; so add it.
			for (size_t ii = 0; ii < sizeof(T); ii++) {
				out.put(0x00);
			}
		}
		// Now write the terminating sequence if it wasn't written already.
		out.write(buffer.c_str(), buffer.size());
	}
	// Writes a bit to the descriptor bitfield. When the descriptor field is
	// full, outputs it and the output parameter buffer.
	void descbit(T bit) {
		if (putbit(bit)) {
			out.write(buffer.c_str(), buffer.size());
			buffer.clear();
		}
	}
	// Puts a byte in the output buffer.
	void putbyte(size_t c) {
		Write1(buffer, c);
	}
};

#endif // _LZSS_H_