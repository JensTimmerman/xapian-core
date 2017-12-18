
#include <config.h>

#include "honey_postlist.h"

#include "honey_cursor.h"
#include "honey_database.h"
#include "honey_positionlist.h"
#include "honey_postlist_encodings.h"
#include "wordaccess.h"

#include <string>

using namespace Honey;
using namespace std;

bool
HoneyPostList::update_reader()
{
    Xapian::docid first_did = docid_from_key(term, cursor->current_key);
    if (!first_did) return false;

    cursor->read_tag();
    const string& tag = cursor->current_tag;
    reader.assign(tag.data(), tag.size(), first_did);
    return true;
}

HoneyPostList::HoneyPostList(const HoneyDatabase* db_,
			     const string& term_,
			     HoneyCursor* cursor_)
    : LeafPostList(term_), cursor(cursor_), db(db_)
{
    if (!cursor->find_exact(make_postingchunk_key(term_))) {
	delete cursor;
	cursor = NULL;
	termfreq = 0;
	last_did = 0;
	return;
    }

    cursor->read_tag();
    const string& chunk = cursor->current_tag;

    const char* p = chunk.data();
    const char* pend = p + chunk.size();
    // FIXME: Make use of [first,last] ranges to calculate better estimates and
    // potentially to spot subqueries that can't match anything.
    Xapian::termcount cf;
    Xapian::docid first_did;
    if (!decode_initial_chunk_header(&p, pend, termfreq, cf,
				     first_did, last_did))
	throw Xapian::DatabaseCorruptError("Postlist initial chunk header");
    reader.assign(p, pend - p, first_did);
}

HoneyPostList::~HoneyPostList()
{
    delete cursor;
    delete position_list;
}

Xapian::doccount
HoneyPostList::get_termfreq() const
{
    return termfreq;
}

LeafPostList*
HoneyPostList::open_nearby_postlist(const string& term_) const
{
    Assert(!term_.empty());
    // FIXME: Once Honey supports writing, we need to return NULL here if the DB is writable.
    return new HoneyPostList(db, term_, new HoneyCursor(*cursor));
}

Xapian::docid
HoneyPostList::get_docid() const
{
    return reader.get_docid();
}

Xapian::termcount
HoneyPostList::get_wdf() const
{
    return reader.get_wdf();
}

bool
HoneyPostList::at_end() const
{
    return cursor == NULL;
}

PositionList*
HoneyPostList::read_position_list()
{
    if (rare(position_list == NULL))
	position_list = new HoneyPositionList();
    if (!position_list->read_data(db->position_table, get_docid(), term))
	return NULL;
    return position_list;
}

PositionList*
HoneyPostList::open_position_list() const
{
    return new HoneyPositionList(db->position_table, get_docid(), term);
}

PostList*
HoneyPostList::next(double)
{
    Assert(cursor);
    if (!reader.at_end()) {
	reader.next();
	if (!reader.at_end()) return NULL;
	cursor->next();
    }

    if (!cursor->after_end()) {
	if (update_reader()) {
	    if (!reader.at_end()) return NULL;
	}
    }

    // We've reached the end.
    delete cursor;
    cursor = NULL;
    return NULL;
}

PostList*
HoneyPostList::skip_to(Xapian::docid did, double)
{
    if (rare(!cursor)) {
	// No-op if already at_end.
	return NULL;
    }

    if (!reader.at_end()) {
	reader.skip_to(did);
	if (!reader.at_end()) return NULL;
    }

    if (did > last_did) goto set_at_end;

    if (!cursor->find_entry(make_postingchunk_key(term, did))) {
	if (update_reader()) {
	    reader.skip_to(did);
	    if (!reader.at_end()) return NULL;
	}
	// The requested docid is between two chunks.
	cursor->next();
    }

    // Either an exact match, or in a gap before the start of a chunk.
    if (!cursor->after_end()) {
	if (update_reader()) {
	    if (!reader.at_end()) return NULL;
	}
    }

set_at_end:
    // We've reached the end.
    delete cursor;
    cursor = NULL;
    return NULL;
}

PostList*
HoneyPostList::check(Xapian::docid did, double, bool& valid)
{
    if (rare(!cursor)) {
	// Already at_end.
	valid = true;
	return NULL;
    }

    if (!reader.at_end()) {
	// Check for the requested docid in the current block.
	reader.skip_to(did);
	if (!reader.at_end()) {
	    valid = true;
	    return NULL;
	}
    }

    if (did > last_did) goto set_at_end;

    // Try moving to the appropriate chunk.
    if (!cursor->find_entry(make_postingchunk_key(term, did))) {
	// We're in a chunk which might contain the docid.
	if (update_reader()) {
	    reader.skip_to(did);
	    if (!reader.at_end()) {
		valid = true;
		return NULL;
	    }
	}

set_at_end:
	// We've reached the end.
	delete cursor;
	cursor = NULL;
	valid = true;
	return NULL;
    }

    // We had an exact match for a chunk starting with specified docid.
    Assert(!cursor->after_end());
    if (!update_reader()) {
	// We found the exact key we built so it must be a posting chunk.
	// Therefore update_reader() "can't possibly fail".
	Assert(false);
    }

    valid = true;
    return NULL;
}

string
HoneyPostList::get_description() const
{
    string desc = "HoneyPostList(";
    desc += term;
    desc += ')';
    return desc;
}

namespace Honey {

void
PostingChunkReader::assign(const char * p_, size_t len, Xapian::docid did_)
{
    Xapian::docid last_did_in_chunk;
    const char* pend = p_ + len;
    if (!decode_delta_chunk_header(&p_, pend, did_, last_did_in_chunk)) {
	throw Xapian::DatabaseCorruptError("Postlist delta chunk header");
    }
    if ((pend - p_) % 8 != 4)
	throw Xapian::DatabaseCorruptError("Doclen data length not 4 more than a multiple of 8");
    if (rare(p_ == pend)) {
	p = NULL;
	return;
    }
    p = reinterpret_cast<const unsigned char*>(p_);
    end = reinterpret_cast<const unsigned char*>(pend);
    did = did_;
    last_did = last_did_in_chunk;
    // FIXME: Alignment guarantees?  Hard with header.
    wdf = unaligned_read4(p);
    p += 4;
}

void
PostingChunkReader::next()
{
    if (p == end) {
	p = NULL;
	return;
    }

    // FIXME: Alignment guarantees?  Hard with header.
    did += unaligned_read4(p) + 1;
    wdf = unaligned_read4(p + 4);
    p += 8;
}

void
PostingChunkReader::skip_to(Xapian::docid target)
{
    if (p == NULL || target <= did)
	return;

    if (target > last_did) {
	p = NULL;
	return;
    }

    // FIXME: Special case target == last_did to just decode the wdf from the
    // end?

    do {
	if (rare(p == end)) {
	    // FIXME: Shouldn't happen unless last_did was wrong.
	    p = NULL;
	    return;
	}

	// FIXME: Alignment guarantees?  Hard with header.
	did += unaligned_read4(p) + 1;
	p += 8;
    } while (target > did);
    wdf = unaligned_read4(p - 4);
}

}
