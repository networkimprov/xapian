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

/* Building
 *
 * ar rcs libmime2text.a \
 *   htmlparse.o  md5.o      metaxmlparse.o  pkglibbindir.o  svgparse.o   utf8convert.o  xpsxmlparse.o \
 *   loadfile.o   md5wrap.o  mime2text.o     myhtmlparse.o   runfilter.o  tmpdir.o       xmlparse.o
 * g++ -o mime2text-test mime2text-test.cc diritor.cc libmime2text.a -lxapian \
 *   -I. -I../../xapian-core -I../../xapian-core/common
 *
 * External Dependencies (filters)
 * FIXME add these
 */

#include <iostream>
#include <string>

#include <config.h>

#include "mime2text.h"
#include "diritor.h"

using Xapian::Mime2Text;

static Mime2Text converter;

static void read_dir(std::string& path) {
    path += '/';
    DirectoryIterator d(false);
    try {
        d.start(path);
        while (d.next()) {
            std::string file = path+d.leafname();
            switch (d.get_type()) {
            case DirectoryIterator::REGULAR_FILE: {
                Mime2Text::Fields aFields;
                Mime2Text::Status aStat = converter.convert(file.c_str(), NULL, &aFields);

                std::cout << aStat
                << ", author: "   << aFields.get_author()
                << ", title: "    << aFields.get_title()
                << ", sample: "   << aFields.get_sample()
                << ", keywords: " << aFields.get_keywords()
                << ", dump: "     << aFields.get_body()
                //<< ", md5: "      << aFields.get_md5()
                << ", mimetype: " << aFields.get_mimetype()
                << ", command: "  << aFields.get_command()
                << std::endl;
            }
            break;
            case DirectoryIterator::DIRECTORY:
                read_dir(file);
            }
        }
    } catch (const std::string& err) {
        std::cerr << err << std::endl;
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
      std::cerr << "specify directory to read" << std::endl;
      return 1;
    }
    std::string dir(argv[1]);
    read_dir(dir);
    return 0;
}
