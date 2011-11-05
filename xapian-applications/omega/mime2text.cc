/* mime2text.cc: convert common-format files to text for indexing
 *
 * Copyright 1999,2000,2001 BrightStation PLC
 * Copyright 2001,2005 James Aylett
 * Copyright 2001,2002 Ananova Ltd
 * Copyright 2002,2003,2004,2005,2006,2007,2008,2009,2010,2011 Olly Betts
 * Copyright 2009 Frank J Bruzzaniti
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

#include <cstring>

#include <xapian.h>

#include "runfilter.h"
#include "myhtmlparse.h"
#include "md5wrap.h"
#include "stringutils.h"
#include "utf8convert.h"
#include "tmpdir.h"
#include "xmlparse.h"
#include "metaxmlparse.h"
#include "xpsxmlparse.h"
#include "pkglibbindir.h"
#include "svgparse.h"
#include "loadfile.h"

#include "mime2text.h"

Mime2Text::Mime2Text(bool noexcl, int sampsize)
{
    ignore_exclusions = noexcl;
    sample_size = sampsize;

    // Plain text:
    mime_map["txt"] = "text/plain";
    mime_map["text"] = "text/plain";

    // HTML:
    mime_map["html"] = "text/html";
    mime_map["htm"] = "text/html";
    mime_map["shtml"] = "text/html";
    mime_map["php"] = "text/html"; // Our HTML parser knows to ignore PHP code.

    // Comma-Separated Values:
    mime_map["csv"] = "text/csv";

    // PDF:
    mime_map["pdf"] = "application/pdf";

    // PostScript:
    mime_map["ps"] = "application/postscript";
    mime_map["eps"] = "application/postscript";
    mime_map["ai"] = "application/postscript";

    // OpenDocument:
    // FIXME: need to find sample documents to test all of these.
    mime_map["odt"] = "application/vnd.oasis.opendocument.text";
    mime_map["ods"] = "application/vnd.oasis.opendocument.spreadsheet";
    mime_map["odp"] = "application/vnd.oasis.opendocument.presentation";
    mime_map["odg"] = "application/vnd.oasis.opendocument.graphics";
    mime_map["odc"] = "application/vnd.oasis.opendocument.chart";
    mime_map["odf"] = "application/vnd.oasis.opendocument.formula";
    mime_map["odb"] = "application/vnd.oasis.opendocument.database";
    mime_map["odi"] = "application/vnd.oasis.opendocument.image";
    mime_map["odm"] = "application/vnd.oasis.opendocument.text-master";
    mime_map["ott"] = "application/vnd.oasis.opendocument.text-template";
    mime_map["ots"] = "application/vnd.oasis.opendocument.spreadsheet-template";
    mime_map["otp"] = "application/vnd.oasis.opendocument.presentation-template";
    mime_map["otg"] = "application/vnd.oasis.opendocument.graphics-template";
    mime_map["otc"] = "application/vnd.oasis.opendocument.chart-template";
    mime_map["otf"] = "application/vnd.oasis.opendocument.formula-template";
    mime_map["oti"] = "application/vnd.oasis.opendocument.image-template";
    mime_map["oth"] = "application/vnd.oasis.opendocument.text-web";

    // OpenOffice/StarOffice documents:
    mime_map["sxc"] = "application/vnd.sun.xml.calc";
    mime_map["stc"] = "application/vnd.sun.xml.calc.template";
    mime_map["sxd"] = "application/vnd.sun.xml.draw";
    mime_map["std"] = "application/vnd.sun.xml.draw.template";
    mime_map["sxi"] = "application/vnd.sun.xml.impress";
    mime_map["sti"] = "application/vnd.sun.xml.impress.template";
    mime_map["sxm"] = "application/vnd.sun.xml.math";
    mime_map["sxw"] = "application/vnd.sun.xml.writer";
    mime_map["sxg"] = "application/vnd.sun.xml.writer.global";
    mime_map["stw"] = "application/vnd.sun.xml.writer.template";

    // MS Office 2007 formats:
    mime_map["docx"] = "application/vnd.openxmlformats-officedocument.wordprocessingml.document"; // Word 2007
    mime_map["dotx"] = "application/vnd.openxmlformats-officedocument.wordprocessingml.template"; // Word 2007 template
    mime_map["xlsx"] = "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"; // Excel 2007
    mime_map["xltx"] = "application/vnd.openxmlformats-officedocument.spreadsheetml.template"; // Excel 2007 template
    mime_map["pptx"] = "application/vnd.openxmlformats-officedocument.presentationml.presentation"; // PowerPoint 2007 presentation
    mime_map["ppsx"] = "application/vnd.openxmlformats-officedocument.presentationml.slideshow"; // PowerPoint 2007 slideshow
    mime_map["potx"] = "application/vnd.openxmlformats-officedocument.presentationml.template"; // PowerPoint 2007 template
    mime_map["xps"] = "application/vnd.ms-xpsdocument";

    // Macro-enabled variants - these appear to be the same formats as the
    // above.  Currently we just treat them as the same mimetypes to avoid
    // having to check for twice as many possible content-types.
    // MS say: application/vnd.ms-word.document.macroEnabled.12
    mime_map["docm"] = "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
    // MS say: application/vnd.ms-word.template.macroEnabled.12
    mime_map["dotm"] = "application/vnd.openxmlformats-officedocument.wordprocessingml.template";
    // MS say: application/vnd.ms-excel.sheet.macroEnabled.12
    mime_map["xlsm"] = "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";
    // MS say: application/vnd.ms-excel.template.macroEnabled.12
    mime_map["xltm"] = "application/vnd.openxmlformats-officedocument.spreadsheetml.template";
    // MS say: application/vnd.ms-powerpoint.presentation.macroEnabled.12
    mime_map["pptm"] = "application/vnd.openxmlformats-officedocument.presentationml.presentation";
    // MS say: application/vnd.ms-powerpoint.slideshow.macroEnabled.12
    mime_map["ppsm"] = "application/vnd.openxmlformats-officedocument.presentationml.slideshow";
    // MS say: application/vnd.ms-powerpoint.presentation.macroEnabled.12
    mime_map["potm"] = "application/vnd.openxmlformats-officedocument.presentationml.template";

    // Some other word processor formats:
    mime_map["doc"] = "application/msword";
    mime_map["dot"] = "application/msword"; // Word template
    mime_map["wpd"] = "application/vnd.wordperfect";
    mime_map["wps"] = "application/vnd.ms-works";
    mime_map["wpt"] = "application/vnd.ms-works"; // Works template
    mime_map["abw"] = "application/x-abiword"; // AbiWord
    mime_map["zabw"] = "application/x-abiword-compressed"; // AbiWord compressed
    mime_map["rtf"] = "text/rtf";

    // Other MS formats:
    mime_map["xls"] = "application/vnd.ms-excel";
    mime_map["xlb"] = "application/vnd.ms-excel";
    mime_map["xlt"] = "application/vnd.ms-excel"; // Excel template
    mime_map["xlr"] = "application/vnd.ms-excel"; // Later Microsoft Works produced XL format but with a different extension.
    mime_map["ppt"] = "application/vnd.ms-powerpoint";
    mime_map["pps"] = "application/vnd.ms-powerpoint"; // Powerpoint slideshow
    mime_map["msg"] = "application/vnd.ms-outlook"; // Outlook .msg email

    // Perl:
    mime_map["pl"] = "text/x-perl";
    mime_map["pm"] = "text/x-perl";
    mime_map["pod"] = "text/x-perl";

    // TeX DVI:
    mime_map["dvi"] = "application/x-dvi";

    // DjVu:
    mime_map["djv"] = "image/vnd.djvu";
    mime_map["djvu"] = "image/vnd.djvu";

    // SVG:
    mime_map["svg"] = "image/svg+xml";

    // Debian packages:
    mime_map["deb"] = "application/x-debian-package";
    mime_map["udeb"] = "application/x-debian-package";

    // RPM packages:
    mime_map["rpm"] = "application/x-redhat-package-manager";

    // Extensions to quietly ignore:
    mime_map["a"] = "ignore";
    mime_map["dll"] = "ignore";
    mime_map["dylib"] = "ignore";
    mime_map["exe"] = "ignore";
    mime_map["lib"] = "ignore";
    mime_map["o"] = "ignore";
    mime_map["obj"] = "ignore";
    mime_map["so"] = "ignore";
    mime_map["css"] = "ignore";
    mime_map["js"] = "ignore";

    commands["application/msword"] = "antiword -mUTF-8.txt ";
    commands["application/vnd.ms-powerpoint"] = "catppt -dutf-8 ";
    // Looking at the source of wpd2html and wpd2text I think both output
    // UTF-8, but it's hard to be sure without sample Unicode .wpd files
    // as they don't seem to be at all well documented.
    commands["application/vnd.wordperfect"] = "wpd2text ";
    // wps2text produces UTF-8 output from the sample files I've tested.
    commands["application/vnd.ms-works"] = "wps2text ";
    // Output is UTF-8 according to "man djvutxt".  Generally this seems to
    // be true, though some examples from djvu.org generate isolated byte
    // 0x95 in a context which suggests it might be intended to be a bullet
    // (as it is in CP1250).
    commands["image/vnd.djvu"] = "djvutxt ";
}

Mime2Text::Status Mime2Text::convert(const char* filepath, const char* type, Mime2Text::Fields* outFields)
{
    if (!type) {
        type = strrchr(filepath, '.');
        if (!type)
            return Status_TYPE;
    }
    outFields->mimetype = type+(*type == '.');
    for (std::string::iterator a = outFields->mimetype.begin(); a != outFields->mimetype.end(); ++a)
        *a = tolower(*a);
    if (*type == '.') {
        std::map<std::string,std::string>::const_iterator aMapRow = mime_map.find(outFields->mimetype);
        if (aMapRow == mime_map.end())
            return Status_TYPE;
        outFields->mimetype = aMapRow->second;
    }
    if (outFields->mimetype == "ignore")
        return Status_IGNORE;

    try {
        std::string file(filepath);
        std::map<std::string, std::string>::const_iterator cmd_it = commands.find(outFields->mimetype);
        if (cmd_it != commands.end()) {
            // Easy "run a command and read UTF-8 text from stdout" cases.
            outFields->command = cmd_it->second;
            if (outFields->command.empty())
                return Status_FILTER;
            outFields->command += shell_protect(file);
            outFields->dump = stdout_to_string(outFields->command);
        } else if (outFields->mimetype == "text/html") {
            std::string text = file_to_string(file);
            MyHtmlParser p;
            if (ignore_exclusions) p.ignore_metarobots();
            try {
                // Default HTML character set is latin 1, though not specifying
                // one is deprecated these days.
                p.parse_html(text, "iso-8859-1", false);
            } catch (const std::string & newcharset) {
                p.reset();
                if (ignore_exclusions) p.ignore_metarobots();
                p.parse_html(text, newcharset, true);
            }
            if (!p.indexing_allowed)
                return Status_METATAG;
            outFields->dump = p.dump;
            outFields->title = p.title;
            outFields->keywords = p.keywords;
            outFields->sample = p.sample;
            outFields->author = p.author;
            md5_string(text, outFields->md5);
        } else if (outFields->mimetype == "text/plain") {
            // Currently we assume that text files are UTF-8 unless they have a
            // byte-order mark.
            outFields->dump = file_to_string(file);
            md5_string(outFields->dump, outFields->md5);
            // Look for Byte-Order Mark (BOM).
            if (startswith(outFields->dump, "\xfe\xff") || startswith(outFields->dump, "\xff\xfe")) {
                // UTF-16 in big-endian/little-endian order - we just convert
                // it as "UTF-16" and let the conversion handle the BOM as that
                // way we avoid the copying overhead of erasing 2 bytes from
                // the start of dump.
                convert_to_utf8(outFields->dump, "UTF-16");
            } else if (startswith(outFields->dump, "\xef\xbb\xbf")) {
                // UTF-8 with stupid Windows not-the-byte-order mark.
                outFields->dump.erase(0, 3);
            } else {
                // FIXME: What charset is the file?  Look at contents?
            }
        } else if (outFields->mimetype == "application/pdf") {
            std::string safefile = shell_protect(file);
            outFields->command = "pdftotext -enc UTF-8 " + safefile + " -";
            outFields->dump = stdout_to_string(outFields->command);
            get_pdf_metainfo(safefile, outFields->author, outFields->title, outFields->keywords);
        } else if (outFields->mimetype == "application/postscript") {
            // There simply doesn't seem to be a Unicode capable PostScript to
            // text converter (e.g. pstotext always outputs ISO-8859-1).  The
            // only solution seems to be to convert via PDF using ps2pdf and
            // then pdftotext.  This gives plausible looking UTF-8 output for
            // some Chinese PostScript files I found using Google.  It also has
            // the benefit of allowing us to extract meta information from
            // PostScript files.
            std::string tmpfile = get_tmpdir();
            if (tmpfile.empty()) // FIXME: should this be fatal?  Or disable indexing postscript?
                return Status_TMPDIR;
            tmpfile += "/tmp.pdf";
            std::string safetmp = shell_protect(tmpfile);
            outFields->command = "ps2pdf " + shell_protect(file) + " " + safetmp;
            try {
                (void)stdout_to_string(outFields->command);
                outFields->command = "pdftotext -enc UTF-8 " + safetmp + " -";
                outFields->dump = stdout_to_string(outFields->command);
            } catch (...) {
                unlink(tmpfile.c_str());
                throw;
            }
            get_pdf_metainfo(safetmp, outFields->author, outFields->title, outFields->keywords);
            unlink(tmpfile.c_str());
        } else if (startswith(outFields->mimetype, "application/vnd.sun.xml.")
                || startswith(outFields->mimetype, "application/vnd.oasis.opendocument.")) {
            // Inspired by http://mjr.towers.org.uk/comp/sxw2text
            std::string safefile = shell_protect(file);
            outFields->command = "unzip -p " + safefile + " content.xml styles.xml";
            XmlParser xmlparser;
            xmlparser.parse_html(stdout_to_string(outFields->command));
            outFields->dump = xmlparser.dump;
            outFields->command = "unzip -p " + safefile + " meta.xml";
            try {
                MetaXmlParser metaxmlparser;
                metaxmlparser.parse_html(stdout_to_string(outFields->command));
                outFields->title = metaxmlparser.title;
                outFields->keywords = metaxmlparser.keywords;
                outFields->sample = metaxmlparser.sample;
                outFields->author = metaxmlparser.author;
            } catch (ReadError) {
                // It's probably best to index the document even if this fails.
            }
        } else if (outFields->mimetype == "application/vnd.ms-excel") {
            outFields->command = "xls2csv -c' ' -q0 -dutf-8 " + shell_protect(file);
            outFields->dump = stdout_to_string(outFields->command);
        } else if (startswith(outFields->mimetype, "application/vnd.openxmlformats-officedocument.")) {
            const char * args = NULL;
            std::string tail(outFields->mimetype, 46);
            if (startswith(tail, "wordprocessingml.")) {
                // unzip returns exit code 11 if a file to extract wasn't found
                // which we want to ignore, because there may be no headers or
                // no footers.
                args = " word/document.xml word/header\\*.xml word/footer\\*.xml 2>/dev/null||test $? = 11";
            } else if (startswith(tail, "spreadsheetml.")) {
                args = " xl/sharedStrings.xml";
            } else if (startswith(tail, "presentationml.")) {
                // unzip returns exit code 11 if a file to extract wasn't found
                // which we want to ignore, because there may be no notesSlides
                // or comments.
                args = " ppt/slides/slide\\*.xml ppt/notesSlides/notesSlide\\*.xml ppt/comments/comment\\*.xml 2>/dev/null||test $? = 11";
            } else {
                // Don't know how to index this type.
                return Status_TYPE;
            }
            std::string safefile = shell_protect(file);
            outFields->command = "unzip -p " + safefile + args;
            XmlParser xmlparser;
            xmlparser.parse_html(stdout_to_string(outFields->command));
            outFields->dump = xmlparser.dump;
            outFields->command = "unzip -p " + safefile + " docProps/core.xml";
            try {
                MetaXmlParser metaxmlparser;
                metaxmlparser.parse_html(stdout_to_string(outFields->command));
                outFields->title = metaxmlparser.title;
                outFields->keywords = metaxmlparser.keywords;
                outFields->sample = metaxmlparser.sample;
                outFields->author = metaxmlparser.author;
            } catch (ReadError) {
                // It's probably best to index the document even if this fails.
            }
        } else if (outFields->mimetype == "application/x-abiword") {
            // FIXME: Implement support for metadata.
            XmlParser xmlparser;
            std::string text = file_to_string(file);
            xmlparser.parse_html(text);
            outFields->dump = xmlparser.dump;
            md5_string(text, outFields->md5);
        } else if (outFields->mimetype == "application/x-abiword-compressed") {
            // FIXME: Implement support for metadata.
            outFields->command = "gzip -dc " + shell_protect(file);
            XmlParser xmlparser;
            xmlparser.parse_html(stdout_to_string(outFields->command));
            outFields->dump = xmlparser.dump;
        } else if (outFields->mimetype == "text/rtf") {
            // The --text option unhelpfully converts all non-ASCII characters
            // to "?" so we use --html instead, which produces HTML entities.
            outFields->command = "unrtf --nopict --html 2>/dev/null " + shell_protect(file);
            MyHtmlParser p;
            p.ignore_metarobots();
            // No point going looking for charset overrides as unrtf doesn't produce them.
            p.parse_html(stdout_to_string(outFields->command), "iso-8859-1", true);
            outFields->dump = p.dump;
            outFields->title = p.title;
            outFields->keywords = p.keywords;
            outFields->sample = p.sample;
        } else if (outFields->mimetype == "text/x-perl") {
            // pod2text's output character set doesn't seem to be documented,
            // but from inspecting the source it looks like it's probably iso-8859-1.
            outFields->command = "pod2text " + shell_protect(file);
            outFields->dump = stdout_to_string(outFields->command);
            convert_to_utf8(outFields->dump, "ISO-8859-1");
        } else if (outFields->mimetype == "application/x-dvi") {
            // FIXME: -e0 means "UTF-8", but that results in "fi", "ff", "ffi",
            // etc appearing as single ligatures.  For European languages, it's
            // actually better to use -e2 (ISO-8859-1) and then convert, so
            // let's do that for now until we handle Unicode "compatibility decompositions".
            outFields->command = "catdvi -e2 -s " + shell_protect(file);
            outFields->dump = stdout_to_string(outFields->command);
            convert_to_utf8(outFields->dump, "ISO-8859-1");
        } else if (outFields->mimetype == "application/vnd.ms-xpsdocument") {
            std::string safefile = shell_protect(file);
            outFields->command = "unzip -p " + safefile + " Documents/1/Pages/\\*.fpage";
            XpsXmlParser xpsparser;
            outFields->dump = stdout_to_string(outFields->command);
            // Look for Byte-Order Mark (BOM).
            if (startswith(outFields->dump, "\xfe\xff") || startswith(outFields->dump, "\xff\xfe")) {
                // UTF-16 in big-endian/little-endian order - we just
                // convert it as "UTF-16" and let the conversion handle the
                // BOM as that way we avoid the copying overhead of erasing
                // 2 bytes from the start of dump.
                convert_to_utf8(outFields->dump, "UTF-16");
            }
            xpsparser.parse_html(outFields->dump);
            outFields->dump = xpsparser.dump;
        } else if (outFields->mimetype == "text/csv") {
            // Currently we assume that text files are UTF-8 unless they have a
            // byte-order mark.
            outFields->dump = file_to_string(file);
            md5_string(outFields->dump, outFields->md5);
            // Look for Byte-Order Mark (BOM).
            if (startswith(outFields->dump, "\xfe\xff") || startswith(outFields->dump, "\xff\xfe")) {
                // UTF-16 in big-endian/little-endian order - we just convert
                // it as "UTF-16" and let the conversion handle the BOM as that
                // way we avoid the copying overhead of erasing 2 bytes from
                // the start of dump.
                convert_to_utf8(outFields->dump, "UTF-16");
            } else if (startswith(outFields->dump, "\xef\xbb\xbf")) {
                // UTF-8 with stupid Windows not-the-byte-order mark.
                outFields->dump.erase(0, 3);
            } else {
                // FIXME: What charset is the file?  Look at contents?
            }
            generate_sample_from_csv(outFields->dump, outFields->sample);
        } else if (outFields->mimetype == "application/vnd.ms-outlook") {
            outFields->command = get_pkglibbindir() + "/outlookmsg2html " + shell_protect(file);
            MyHtmlParser p;
            p.ignore_metarobots();
            outFields->dump = stdout_to_string(outFields->command);
            try {
                // FIXME: what should the default charset be?
                p.parse_html(outFields->dump, "iso-8859-1", false);
            } catch (const std::string & newcharset) {
                p.reset();
                p.ignore_metarobots();
                p.parse_html(outFields->dump, newcharset, true);
            }
            outFields->dump = p.dump;
            outFields->title = p.title;
            outFields->keywords = p.keywords;
            outFields->sample = p.sample;
            outFields->author = p.author;
        } else if (outFields->mimetype == "image/svg+xml") {
            SvgParser svgparser;
            svgparser.parse_html(file_to_string(file));
            outFields->dump = svgparser.dump;
            outFields->title = svgparser.title;
            outFields->keywords = svgparser.keywords;
            outFields->author = svgparser.author;
        } else if (outFields->mimetype == "application/x-debian-package") {
            outFields->command = "dpkg-deb -f ";
            outFields->command += shell_protect(file);
            outFields->command += " Description";
            const std::string & desc = stdout_to_string(outFields->command);
            // First line is short description, which we use as the title.
            std::string::size_type idx = desc.find('\n');
            outFields->title.assign(desc, 0, idx);
            if (idx != std::string::npos) {
                outFields->dump.assign(desc, idx + 1, std::string::npos);
            }
        } else if (outFields->mimetype == "application/x-redhat-package-manager") {
            outFields->command = "rpm -q --qf '%{SUMMARY}\\n%{DESCRIPTION}' -p ";
            outFields->command += shell_protect(file);
            const std::string & desc = stdout_to_string(outFields->command);
            // First line is summary, which we use as the title.
            std::string::size_type idx = desc.find('\n');
            outFields->title.assign(desc, 0, idx);
            if (idx != std::string::npos) {
                outFields->dump.assign(desc, idx + 1, std::string::npos);
            }
        } else {
            // Don't know how to index this type.
            return Status_TYPE;
        }

        // Compute the MD5 of the file if we haven't already.
        if (outFields->md5.empty() && md5_file(file, outFields->md5, true) == 0)
            return Status_MD5;

    } catch (ReadError) {
        return Status_COMMAND;
    } catch (NoSuchFilter) {
        commands[outFields->mimetype] = "";
        return Status_FILTER;
    } catch (const std::string& err) {
        outFields->command = err;
        return Status_FILENAME;
    }
    return Status_OK;
}

std::string Mime2Text::file_to_string(const std::string& file)
  // I suspect the overhead incurred when O_NOATIME causes a 2nd open() syscall is <50us
  // and therefore not noticeable, given all the disk I/O an indexing pass does. -Liam
{
    std::string output;
    if (!load_file(file, output, NOCACHE | NOATIME))
      throw ReadError();
    return output;
}

std::string Mime2Text::shell_protect(const std::string& file)
{
    std::string safefile = file;
#ifdef __WIN32__
    bool need_to_quote = false;
    for (std::string::iterator i = safefile.begin(); i != safefile.end(); ++i) {
        unsigned char ch = *i;
        if (!isalnum(ch) && ch < 128) {
            if (ch == '/') {
                // Convert Unix path separators to backslashes.  C library
                // functions understand "/" in paths, but external commands
                // generally don't, and also may interpret a leading '/' as
                // introducing a command line option.
                *i = '\\';
            } else if (ch == ' ') {
                need_to_quote = true;
            } else if (ch < 32 || strchr("<>\"|*?", ch)) {
                // Check for invalid characters in the filename.
                std::string m("Invalid character '");
                m += ch;
                m += "' in filename \"";
                m += file;
                m += '"';
                throw m;
            }
        }
    }
    if (safefile[0] == '-') {
        // If the filename starts with a '-', protect it from being treated as
        // an option by prepending ".\".
        safefile.insert(0, ".\\");
    }
    if (need_to_quote) {
        safefile.insert(0, "\"");
        safefile += '"';
    }
#else
    std::string::size_type p = 0;
    if (!safefile.empty() && safefile[0] == '-') {
        // If the filename starts with a '-', protect it from being treated as
        // an option by prepending "./".
        safefile.insert(0, "./");
        p = 2;
    }
    while (p < safefile.size()) {
        // Don't escape some safe characters which are common in filenames.
        unsigned char ch = safefile[p];
        if (!isalnum(ch) && strchr("/._-", ch) == NULL) {
            safefile.insert(p, "\\");
            ++p;
        }
        ++p;
    }
#endif
    return safefile;
}

void Mime2Text::parse_pdfinfo_field(const char * p, const char * end, std::string & out, const char * field, size_t len)
{
    if (size_t(end - p) > len && memcmp(p, field, len) == 0) {
        p += len;
        while (p != end && *p == ' ')
            ++p;
        if (p != end && (end[-1] != '\r' || --end != p))
            out.assign(p, end - p);
    }
}

#define PARSE_PDFINFO_FIELD(P, END, OUT, FIELD) \
    parse_pdfinfo_field((P), (END), (OUT), FIELD":", CONST_STRLEN(FIELD) + 1)

void Mime2Text::get_pdf_metainfo(const std::string & safefile, std::string &author, std::string &title, std::string &keywords)
{
    try {
        std::string pdfinfo = stdout_to_string("pdfinfo -enc UTF-8 " + safefile);

        const char * p = pdfinfo.data();
        const char * end = p + pdfinfo.size();
        while (p != end) {
            const char * start = p;
            p = static_cast<const char *>(memchr(p, '\n', end - p));
            const char * eol;
            if (p) {
                eol = p;
                ++p;
            } else {
                p = eol = end;
            }
            switch (*start) {
                case 'A': PARSE_PDFINFO_FIELD(start, eol, author,   "Author");   break;
                case 'K': PARSE_PDFINFO_FIELD(start, eol, keywords, "Keywords"); break;
                case 'T': PARSE_PDFINFO_FIELD(start, eol, title,    "Title");    break;
            }
        }
    } catch (ReadError) {
        // It's probably best to index the document even if pdfinfo fails.
    }
}

void Mime2Text::generate_sample_from_csv(const std::string & csv_data, std::string & sample)
{
    // Add 3 to allow for a 4 byte utf-8 sequence being appended when
    // output is SAMPLE_SIZE - 1 bytes long.
    sample.reserve(sample_size + 3);
    size_t last_word_end = 0;
    bool in_space = true;
    bool in_quotes = false;
    Xapian::Utf8Iterator i(csv_data);
    for ( ; i != Xapian::Utf8Iterator(); ++i) {
        unsigned ch = *i;

        if (!in_quotes) {
            // If not already in double quotes, '"' starts quoting and
            // ',' starts a new field.
            if (ch == '"') {
                in_quotes = true;
                continue;
            }
            if (ch == ',')
                ch = ' ';
        } else if (ch == '"') {
            // In double quotes, '"' either ends double quotes, or
            // if followed by another '"', means a literal '"'.
            if (++i == Xapian::Utf8Iterator())
                break;
            ch = *i;
            if (ch != '"') {
                in_quotes = false;
                if (ch == ',')
                    ch = ' ';
            }
        }

        if (ch <= ' ' || ch == 0xa0) {
            // FIXME: if all the whitespace characters between two
            // words are 0xa0 (non-breaking space) then perhaps we
            // should output 0xa0.
            if (in_space)
                continue;
            last_word_end = sample.size();
            sample += ' ';
            in_space = true;
        } else {
            Xapian::Unicode::append_utf8(sample, ch);
            in_space = false;
        }

        if (sample.size() >= sample_size) {
            // Need to truncate sample.
            if (last_word_end <= sample_size / 2) {
                // Monster word!  We'll have to just split it.
                sample.replace(sample_size - 3, std::string::npos, "...", 3);
            } else {
                sample.replace(last_word_end, std::string::npos, " ...", 4);
            }
            break;
        }
    }
}


