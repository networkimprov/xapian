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

/* Dependencies (combine these into mime2text.a library)
 *
 * htmlparse.cc  md5.cc      metaxmlparse.cc  pkglibbindir.cc  svgparse.cc   utf8convert.cc  xpsxmlparse.cc
 * loadfile.cc   md5wrap.cc  mime2text.cc     myhtmlparse.cc   runfilter.cc  tmpdir.cc       xmlparse.cc
 *
 * External Dependencies (filters)
 * FIXME add these
 */

int main() {
    Mime2Text converter;
    Mime2Text::Fields aFields;

    Mime2Text::Status aStat = converter.convert("./mime-test.html", NULL, &aFields);

    std::cout << aStat
    << " author " << aFields.author
    << " title " << aFields.title
    << " sample " << aFields.sample
    << " keywords " << aFields.keywords
    << " dump " << aFields.dump
    //<< " md5 " << aFields.md5
    << " mimetype " << aFields.mimetype
    << " command " << aFields.command
    << std::endl;

    return 0;
}
