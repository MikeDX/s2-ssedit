/* -*- Mode: C++; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * Copyright (C) Flamewing 2011-2013 <flamewing.sonic@gmail.com>
 * Copyright (C) 2002-2004 The KENS Project Development Team
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

#ifndef _KOSINSKI_H_
#define _KOSINSKI_H_

class istream;
class ostream;

class kosinski {
private:
	template <typename Adaptor>
	static void decode_internal(std::istream &in, std::iostream &Dst, size_t &DecBytes);
	template <typename Adaptor>
	static void encode_internal(std::ostream &Dst, unsigned char const *&Buffer,
	                            std::streamoff SlideWin, std::streamoff RecLen,
	                            std::streamsize const BSize);
public:
	static bool decode(std::istream &Src, std::iostream &Dst,
	                   std::streampos Location = 0, bool Moduled = false);
	static bool encode(std::istream &Src, std::ostream &Dst,
	                   std::streamoff SlideWin = 8192, std::streamoff RecLen = 256,
	                   bool Moduled = false, std::streamoff ModuleSize = 0x1000);
};

#endif // _KOSINSKI_H_
