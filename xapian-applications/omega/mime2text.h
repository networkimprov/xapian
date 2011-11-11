/* mime2text.h: convert common-format files to text for indexing
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

#ifndef OMEGA_INCLUDED_MIME2TEXT_H
#define OMEGA_INCLUDED_MIME2TEXT_H

#include <string>
#include <map>

namespace Xapian {

/** Extracts text from common-format files for indexing
 *  
 *  TODO support stream input
 */
class Mime2Text {
public:
    /** Constructor
     *  
     *  @param noexcl    Ignore_exclusions flag (default: false)
     *  @param sampsize  Max size of sample output (default: 512)
     */
    explicit Mime2Text(bool noexcl=false, int sampsize=512);

    /** Set the command to execute for a mimetype
     *  
     *  @param key    Mime-type
     *  @param value  External command to execute
     */
    void set_command(const char* key, const char* value) { commands[key] = value; }
    // FIXME should tolower() inputs

    /** Set the mimetype for a filename extension
     *  
     *  @param key    Extension
     *  @param value  Mime-type to associate with extension
     */
    void set_mimetype(const char* key, const char* value) { mime_map[key] = value; }
    // FIXME should tolower() inputs

    /// return values for convert()
    enum Status {
        /// conversion succeeded
        Status_OK,

        /// extension or mimetype not known
        Status_TYPE,

        /// type is not convertible
        Status_IGNORE,

        /// content is protected by a meta tag
        Status_METATAG,

        /// filename is invalid
        Status_FILENAME,

        /// external filter invoked by command was not found
        Status_FILTER,

        /// command failed
        Status_COMMAND,

        /// md5 checksum generation failed
        Status_MD5,

        /// accessing a temporary directory failed
        Status_TMPDIR
    };

    /// Conversion output
    class Fields {
    public:
        /// Title text, if any
        std::string& get_title()    { return title; }

        /// Author text, if any
        std::string& get_author()   { return author; }

        /// Keywords, if any
        std::string& get_keywords() { return keywords; }

        /// Sample of document, if derivable
        std::string& get_sample()   { return sample; }

        /// Body text, if any
        std::string& get_body()     { return dump; }

        /// md5 checksum
        std::string& get_md5()      { return md5; }

        /// Mime-type used in conversion
        std::string& get_mimetype() { return mimetype; }

        /// Command used in conversion, if any
        std::string& get_command()  { return command; }

    private:
        friend class Mime2Text;
        std::string author, title, sample, keywords, dump;
        std::string md5;
        std::string mimetype, command;
    };

    /** Extract Fields from a file
     *  
     *  @param filepath    file to open
     *  @param type        mimetype; if NULL check file ext; if starts with . find in mime_map
     *  @param out_fields  pointer to a Fields object
     */
    Status convert(const char* filepath, const char* type, Fields* out_fields);

private:
    std::string shell_protect(const std::string& file);
    std::string file_to_string(const std::string& file);
    void get_pdf_metainfo(const std::string& safefile, std::string& command, std::string& author, std::string& title, std::string& keywords);
    void parse_pdfinfo_field(const char* p, const char* end, std::string& out, const char* field, size_t len);
    void generate_sample_from_csv(const std::string& csv_data, std::string& sample);

    bool ignore_exclusions;
    int sample_size;
    std::map<std::string, std::string> mime_map;
    std::map<std::string, std::string> commands;
};

}

#endif // OMEGA_INCLUDED_MIME2TEXT_H

