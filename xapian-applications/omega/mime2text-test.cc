/* mime2text-test.cc: exercise Mime2Text converter
 *
 * Copyright 2011 Liam Breck
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */

#include <iostream>

#include "mime2text.h"

using Xapian::Mime2Text;

/* Building
 *
 * ar rcs libmime2text.a \
 *   htmlparse.o  md5.o      metaxmlparse.o  pkglibbindir.o  svgparse.o   utf8convert.o  xpsxmlparse.o \
 *   loadfile.o   md5wrap.o  mime2text.o     myhtmlparse.o   runfilter.o  tmpdir.o       xmlparse.o
 * g++ -o mime2text-test mime2text-test.cc libmime2text.a -lxapian
 *
 * External Dependencies (filters)
 * FIXME add these
 */

int main() {
    Mime2Text converter;
    Mime2Text::Fields aFields;

    Mime2Text::Status aStat = converter.convert("./mime-test.html", NULL, &aFields);

    std::cout << aStat
    << " author "   << aFields.get_author()
    << " title "    << aFields.get_title()
    << " sample "   << aFields.get_sample()
    << " keywords " << aFields.get_keywords()
    << " dump "     << aFields.get_body()
    //<< " md5 "      << aFields.get_md5()
    << " mimetype " << aFields.get_mimetype()
    << " command "  << aFields.get_command()
    << std::endl;

    return 0;
}
