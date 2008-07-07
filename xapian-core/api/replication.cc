/** @file replication.cc
 * @brief Replication support for Xapian databases.
 */
/* Copyright (C) 2008 Lemur Consulting Ltd
 * Copyright (C) 2008 Olly Betts
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <config.h>

#include "xapian/replication.h"

#include "xapian/base.h"
#include "xapian/dbfactory.h"
#include "xapian/error.h"

#include "database.h"
#include "fileutils.h"
#ifdef __WIN32__
# include "msvc_posix_wrapper.h"
#endif
#include "omassert.h"
#include "omdebug.h"
#include "omtime.h"
#include "remoteconnection.h"
#include "replicationprotocol.h"
#include "safeerrno.h"
#include "safesysstat.h"
#include "safeunistd.h"
#include "serialise.h"
#include "utils.h"

#include <algorithm>
#include <cstdio> // For rename().
#include <fstream>
#include <map>
#include <string>

using namespace std;
using namespace Xapian;

void
DatabaseMaster::write_changesets_to_fd(int fd,
				       const string & start_revision,
				       ReplicationInfo * info) const
{
    DEBUGAPICALL(void, "Xapian::DatabaseMaster::write_changesets_to_fd",
		 fd << ", " << start_revision << ", " << info);
    if (info != NULL)
	info->clear();
    Database db;
    try {
	db = Database(path);
    } catch (Xapian::DatabaseError & e) {
	RemoteConnection conn(-1, fd, "");
	OmTime end_time;
	conn.send_message(REPL_REPLY_FAIL,
			  "Can't open database: " + e.get_msg(),
			  end_time);
	return;
    }
    if (db.internal.size() != 1) {
	throw Xapian::InvalidOperationError("DatabaseMaster needs to be pointed at exactly one subdatabase");
    }

    // Extract the UUID from start_revision and compare it to the database.
    bool need_whole_db = false;
    string revision(start_revision);
    if (revision.empty()) {
	need_whole_db = true;
    } else {
	const char * ptr = revision.data();
	const char * end = ptr + revision.size();
	size_t uuid_length = decode_length(&ptr, end, true);
	string request_uuid(ptr, uuid_length);
	string db_uuid = db.internal[0]->get_uuid();
	if (request_uuid != db_uuid) {
	    need_whole_db = true;
	}

	revision.erase(0, ptr + uuid_length - revision.data());
    }

    db.internal[0]->write_changesets_to_fd(fd, revision, need_whole_db, info);
}

string
DatabaseMaster::get_description() const
{
    return "DatabaseMaster(" + path + ")";
}

/// Internal implementation of DatabaseReplica
class DatabaseReplica::Internal : public Xapian::Internal::RefCntBase {
    /// Don't allow assignment.
    void operator=(const Internal &);

    /// Don't allow copying.
    Internal(const Internal &);

    /// The path to the replica directory.
    string path;

    /// The name of the currently live database in the replica.
    string live_name;

    /// The live database being replicated.
    WritableDatabase live_db;

    /** The name of the secondary database being built.
     *
     *  This is used when we're building a new copy of the database, which
     *  can't yet be made live.
     */
    string offline_name;

    /** The revision that the secondary database has been updated to.
     */
    string offline_revision;

    /** The UUID of the secondary database.
     */
    string offline_uuid;

    /** The revision that the secondary database must reach before it can be
     *  made live.
     */
    string offline_needed_revision;

    /// The parameters stored for this replica.
    map<string, string> parameters;

    /// The remote connection we're using.
    RemoteConnection * conn;

    /// Read the parameters from a file in the replica.
    void read_parameters();

    /// Write the parameters to a file in the replica.
    void write_parameters() const;

    /** Update the stub database which points to a single flint database.
     *
     *  The stub database file is created at a separate path, and then atomically
     *  moved into place to replace the old stub database.  This should allow
     *  searches to continue uninterrupted.
     *
     *  @param flint_path The path to the flint database.
     */
    void update_stub_database(const string & flint_path) const;

    /** If there's an offline database, discard it.
     */
    void remove_offline_db();

    /** Apply a set of DB copy messages from the connection.
     */
    void apply_db_copy(const OmTime & end_time);

    /** Check that a message type is as expected.
     *
     *  Throws a NetworkError if the type is not the expected one.
     */
    void check_message_type(char type, char expected);

    /** Check if the offline database has reached the required version.
     *
     *  If so, make it live, and remove the old live database.
     *
     *  @return true iff the offline database is made live
     */
    bool possibly_make_offline_live();
  public:
    /// Open a new DatabaseReplica::Internal for the specified path.
    Internal(const string & path_);

    /// Destructor.
    ~Internal() { delete conn; }

    /// Set a parameter for the replica.
    void set_parameter(const string & name, const string & value);

    /// Get a parameter from the replica.
    string get_parameter(const string & name) const;

    /// Get a string describing the current revision of the replica.
    string get_revision_info() const;

    /// Set the file descriptor to read changesets from.
    void set_read_fd(int fd);

    /// Read and apply the next changeset.
    bool apply_next_changeset(ReplicationInfo * info);

    /// Return a string describing this object.
    string get_description() const { return path; }
};

// Methods of DatabaseReplica

DatabaseReplica::DatabaseReplica(const DatabaseReplica & other)
	: internal(other.internal)
{
    DEBUGAPICALL(void, "Xapian::DatabaseReplica::DatabaseReplica", other);
}

void
DatabaseReplica::operator=(const DatabaseReplica & other)
{
    DEBUGAPICALL(void, "Xapian::DatabaseReplica::operator=", other);
    internal = other.internal;
}

DatabaseReplica::DatabaseReplica()
	: internal(0)
{
    DEBUGAPICALL(void, "Xapian::DatabaseReplica::DatabaseReplica", "");
}

DatabaseReplica::DatabaseReplica(const string & path)
	: internal(new DatabaseReplica::Internal(path))
{
    DEBUGAPICALL(void, "Xapian::DatabaseReplica::DatabaseReplica", path);
}

DatabaseReplica::~DatabaseReplica()
{
    DEBUGAPICALL(void, "Xapian::DatabaseReplica::~DatabaseReplica", "");
}

void
DatabaseReplica::set_parameter(const string & name, const string & value)
{
    DEBUGAPICALL(void, "Xapian::DatabaseReplica::set_parameter",
		 name << ", " << value);
    if (internal.get() == NULL)
	throw Xapian::InvalidOperationError("Attempt to call DatabaseReplica::set_parameter on a closed replica.");
    internal->set_parameter(name, value);
}

string
DatabaseReplica::get_parameter(const string & name) const
{
    DEBUGAPICALL(string, "Xapian::DatabaseReplica::get_parameter", name);
    if (internal.get() == NULL)
	throw Xapian::InvalidOperationError("Attempt to call DatabaseReplica::get_parameter on a closed replica.");
    RETURN(internal->get_parameter(name));
}

string
DatabaseReplica::get_revision_info() const
{
    DEBUGAPICALL(string, "Xapian::DatabaseReplica::get_revision_info", "");
    if (internal.get() == NULL)
	throw Xapian::InvalidOperationError("Attempt to call DatabaseReplica::get_revision_info on a closed replica.");
    RETURN(internal->get_revision_info());
}

void 
DatabaseReplica::set_read_fd(int fd)
{
    DEBUGAPICALL(void, "Xapian::DatabaseReplica::set_read_fd", fd);
    if (internal.get() == NULL)
	throw Xapian::InvalidOperationError("Attempt to call DatabaseReplica::set_read_fd on a closed replica.");
    internal->set_read_fd(fd);
}

bool 
DatabaseReplica::apply_next_changeset(ReplicationInfo * info)
{
    DEBUGAPICALL(bool, "Xapian::DatabaseReplica::apply_next_changeset", info);
    if (info != NULL)
	info->clear();
    if (internal.get() == NULL)
	throw Xapian::InvalidOperationError("Attempt to call DatabaseReplica::apply_next_changeset on a closed replica.");
    RETURN(internal->apply_next_changeset(info));
}

void 
DatabaseReplica::close()
{
    DEBUGAPICALL(bool, "Xapian::DatabaseReplica::close", "");
    internal = NULL;
}

string
DatabaseReplica::get_description() const
{
    return "DatabaseReplica(" + internal->get_description() + ")";
}

// Methods of DatabaseReplica::Internal

void
DatabaseReplica::Internal::read_parameters()
{
    parameters.clear();

    string param_path = join_paths(path, "params");
    if (file_exists(param_path)) {
	ifstream p_in(param_path.c_str());
	string line;
	while (getline(p_in, line)) {
	    string::size_type eq = line.find('=');
	    if (eq != string::npos) {
		string key = line.substr(0, eq);
		line.erase(0, eq + 1);
		parameters[key] = line;
	    }
	}
    }
}

void
DatabaseReplica::Internal::write_parameters() const
{
    string param_path = join_paths(path, "params");
    ofstream p_out(param_path.c_str());

    map<string, string>::const_iterator i;
    for (i = parameters.begin(); i != parameters.end(); ++i) {
	p_out << i->first << "=" << i->second << endl;
    }
}

void
DatabaseReplica::Internal::update_stub_database(const string & flint_path) const
{
    string tmp_path = join_paths(path, "XAPIANDB.tmp");
    string stub_path = join_paths(path, "XAPIANDB");
    {
	ofstream stub(tmp_path.c_str());
	stub << "# This file was automatically generated by DatabaseReplica.\n"
		"# If may be rewritten after each replication operation.\n"
		"# You should not manually edit it.\n"
		"flint " << flint_path << endl;
    }
    int result;
#ifdef __WIN32__
    result = msvc_posix_rename(tmp_path.c_str(), stub_path.c_str());
#else
    result = rename(tmp_path.c_str(), stub_path.c_str());
#endif
    if (result == -1) {
	string msg("Failed to update stub db file for replica: ");
	msg += path;
	throw Xapian::DatabaseOpeningError(msg);
    }
}

DatabaseReplica::Internal::Internal(const string & path_)
	: path(path_), live_name(), live_db(), 
	  offline_name(), offline_revision(), offline_needed_revision(),
	  parameters(), conn(NULL)
{
    DEBUGCALL(API, void, "DatabaseReplica::Internal::Internal", path_);
    if (file_exists(path)) {
	throw InvalidOperationError("Replica path should not be a file");
    }
#ifndef XAPIAN_HAS_FLINT_BACKEND
    throw FeatureUnavailableError("Flint backend is not enabled, and needed for database replication");
#endif
    if (!dir_exists(path)) {
	// The database doesn't already exist - make a directory, containing a
	// stub database, and point it to a new flint database.
	mkdir(path, 0777);
	live_name = "replica_0";
	string live_path = join_paths(path, live_name);
	live_db.add_database(Flint::open(live_path, Xapian::DB_CREATE));
	update_stub_database(live_name);
    } else {
	// The database already exists as a stub database - open it.  We can't
	// just use the standard opening routines, because we want to open it
	// for writing.  We enforce that the stub database points to a single
	// flint database here.
	string stub_path = join_paths(path, "XAPIANDB");
	ifstream stub(stub_path.c_str());
	string line;
	while (getline(stub, line)) {
	    if (line.empty() || line[0] == '#')
		continue;
	    string::size_type space = line.find(' ');
	    if (space == string::npos)
		continue;
	    string type = line.substr(0, space);
	    line.erase(0, space + 1);
	    live_name = line;
	    if (type == "flint") {
		string live_path = join_paths(path, live_name);
		live_db.add_database(Flint::open(live_path, Xapian::DB_OPEN));
	    } else {
		throw FeatureUnavailableError(
		    "Database replication only works with flint databases.");
	    }
	}
	if (live_db.internal.size() != 1) {
	    throw Xapian::InvalidOperationError(
		"DatabaseReplica needs to be reference exactly one subdatabase"
		" - found " + om_tostring(live_db.internal.size()) +
		" subdatabases.");
	}
    }

    read_parameters();
}

void
DatabaseReplica::Internal::set_parameter(const string & name,
					 const string & value)
{
    DEBUGCALL(API, void, "DatabaseReplica::Internal::set_parameter",
	      name << ", " << value);
    if (value.empty()) {
	parameters.erase(name);
    } else {
	parameters[name] = value;
    }
    write_parameters();
}

string
DatabaseReplica::Internal::get_parameter(const string & name) const
{
    DEBUGCALL(API, string, "DatabaseReplica::Internal::get_parameter", name);
    map<string, string>::const_iterator i = parameters.find(name);
    if (i == parameters.end()) RETURN(string(""));
    RETURN(i->second);
}

string
DatabaseReplica::Internal::get_revision_info() const
{
    DEBUGCALL(API, string, "DatabaseReplica::Internal::get_revision_info", "");
    if (live_db.internal.size() != 1) {
	throw Xapian::InvalidOperationError("DatabaseReplica needs to be pointed at exactly one subdatabase");
    }
    string buf;
    string uuid = hex_decode(get_parameter("uuid"));
    // FIXME - when uuids are actually stored in databases, use the following:
    // string uuid = (live_db.internal[0])->get_uuid();
    buf += encode_length(uuid.size());
    buf += uuid;
    buf += (live_db.internal[0])->get_revision_info();
    RETURN(buf);
}

void
DatabaseReplica::Internal::remove_offline_db()
{
    if (offline_name.empty())
	return;
    string offline_path = join_paths(path, offline_name);
    // Close and then delete the database.
    if (dir_exists(offline_path)) {
	removedir(offline_path);
    }
    offline_name.resize(0);
}

void
DatabaseReplica::Internal::apply_db_copy(const OmTime & end_time)
{
    // If there's already an offline database, discard it.  This happens if one
    // copy of the database was sent, but further updates were needed before it
    // could be made live, and the remote end was then unable to send those
    // updates (probably due to not having changesets available, or the remote
    // database being replaced by a new database).
    remove_offline_db();

    // Work out new path to make an offline database at.
    if (live_name.size() < 2 || live_name[live_name.size() - 2] != '_') {
	offline_name = live_name + "_0";
    } else if (live_name[live_name.size() - 1] == '0') {
	offline_name = live_name.substr(0, live_name.size() - 1) + "1";
    } else {
	offline_name = live_name.substr(0, live_name.size() - 1) + "0";
    }
    string offline_path = join_paths(path, offline_name);
    if (dir_exists(offline_path)) {
	removedir(offline_path);
    }
    if (mkdir(offline_path, 0777)) {
	throw Xapian::DatabaseError("Cannot make directory '" +
				    offline_path + "'", errno);
    }

    string buf;
    char type = conn->get_message(buf, end_time);
    check_message_type(type, REPL_REPLY_DB_HEADER);
    {
	const char * ptr = buf.data();
	const char * end = ptr + buf.size();
	size_t uuid_length = decode_length(&ptr, end, true);
	offline_uuid.assign(ptr, uuid_length);
	buf.erase(0, ptr + uuid_length - buf.data());
    }
    offline_revision = buf;

    // Now, read the files for the database from the connection and create it.
    while (true) {
	string filename;
	type = conn->sniff_next_message_type(end_time);
	if (type == REPL_REPLY_FAIL)
	    return;
	if (type == REPL_REPLY_DB_FOOTER)
	    break;

	type = conn->get_message(filename, end_time);
	check_message_type(type, REPL_REPLY_DB_FILENAME);

	// Check that the filename doesn't contain '..'.  No valid database
	// file contains .., so we don't need to check that the .. is a path.
	if (filename.find("..") != string::npos) {
	    throw NetworkError("Filename in database contained '..'");
	}

	type = conn->sniff_next_message_type(end_time);
	if (type == REPL_REPLY_FAIL)
	    return;

	string filepath = offline_path + "/" + filename;
	type = conn->receive_file(filepath, end_time);
	check_message_type(type, REPL_REPLY_DB_FILEDATA);
    }
    type = conn->get_message(offline_needed_revision, end_time);
    check_message_type(type, REPL_REPLY_DB_FOOTER);
}

void
DatabaseReplica::Internal::check_message_type(char type, char expected)
{
    if (type != expected) {
	throw NetworkError("Unexpected replication protocol message type (got "
			   + om_tostring(type) + ", expected "
			   + om_tostring(expected) + ")");
    }
}

bool
DatabaseReplica::Internal::possibly_make_offline_live()
{
    if (!(live_db.internal[0])->check_revision_at_least(offline_revision,
							offline_needed_revision))
	return false;
    string offline_path = join_paths(path, offline_name);
    live_db = WritableDatabase();
    live_db.add_database(Flint::open(offline_path, Xapian::DB_OPEN));
    update_stub_database(offline_name);
    set_parameter("uuid", hex_encode(offline_uuid));
    swap(live_name, offline_name);
    remove_offline_db();
    return true;
}

void
DatabaseReplica::Internal::set_read_fd(int fd)
{
    delete(conn);
    conn = NULL;
    conn = new RemoteConnection(fd, -1, "");
}

bool 
DatabaseReplica::Internal::apply_next_changeset(ReplicationInfo * info)
{
    DEBUGCALL(API, bool,
	      "DatabaseReplica::Internal::apply_next_changeset", info);
    if (live_db.internal.size() != 1) {
	throw Xapian::InvalidOperationError("DatabaseReplica needs to be pointed at exactly one subdatabase");
    }
    OmTime end_time;

    while (true) {
	char type;
	type = conn->sniff_next_message_type(end_time);
	switch(type)
	{
	    case REPL_REPLY_END_OF_CHANGES:
		RETURN(false);
	    case REPL_REPLY_DB_HEADER:
		// Apply the copy - remove offline db in case of any error.
		try {
		    apply_db_copy(end_time);
		    if (info != NULL)
			++(info->fullcopy_count);
		} catch (...) {
		    remove_offline_db();
		    throw;
		}
		if (possibly_make_offline_live()) {
		    if (info != NULL)
			info->changed = true;
		}
		break;
	    case REPL_REPLY_CHANGESET:
		if (offline_name.empty()) {
		    offline_needed_revision = (live_db.internal[0])->
			    apply_changeset_from_conn(*conn, end_time);
		    if (info != NULL) {
			++(info->changeset_count);
			info->changed = true;
		    }
		    live_db = WritableDatabase();
		    string livedb_path = join_paths(path, live_name);
		    live_db.add_database(Flint::open(livedb_path, Xapian::DB_OPEN));
		} else {
		    {
			string offline_path = join_paths(path, offline_name);
			WritableDatabase offline_db;
			offline_db.add_database(Flint::open(offline_path, Xapian::DB_OPEN));
			offline_needed_revision = (offline_db.internal[0])->
				apply_changeset_from_conn(*conn, end_time);
		    }
		    if (possibly_make_offline_live()) {
			if (info != NULL)
			    info->changed = true;
		    }
		}
		RETURN(true);
	    case REPL_REPLY_FAIL:
		{
		    string buf;
		    (void) conn->get_message(buf, end_time);
		    throw NetworkError("Unable to fully synchronise: " + buf);
		}
	    default:
		throw NetworkError("Unknown replication protocol message ("
				   + om_tostring(type) + ")");
	}
    }
}