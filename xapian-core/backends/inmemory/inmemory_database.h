/* inmemory_database.h: C++ class definition for multiple database access
 *
 * ----START-LICENCE----
 * Copyright 1999,2000 Dialog Corporation
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 * -----END-LICENCE-----
 */

#ifndef OM_HGUARD_INMEMORY_DATABASE_H
#define OM_HGUARD_INMEMORY_DATABASE_H

#include "omassert.h"
#include "utils.h"
#include "dbpostlist.h"
#include "termlist.h"
#include "database.h"
#include "indexer.h"
#include <stdlib.h>
#include <map>
#include <vector>
#include <list>
#include <algorithm>


// Class representing a posting (a term/doc pair, and
// all the relevant positional information, is a single posting)
class InMemoryPosting {
    public:
	om_docid did;
	om_termname tname;
	vector<om_termpos> positions; // Sorted list of positions

	// Merge two postings (same term/doc pair, new positional info)
	void merge(const InMemoryPosting & post) {
	    Assert(did == post.did);
	    Assert(tname == post.tname);

	    positions.insert(positions.end(),
			     post.positions.begin(),
			     post.positions.end());
	    // FIXME - inefficient - use merge (and list<>)?
	    sort(positions.begin(), positions.end());
	}
};

// Compare by document ID
class InMemoryPostingLessByDocId {
    public:
	int operator() (const InMemoryPosting &p1, const InMemoryPosting &p2)
	{
	    return p1.did < p2.did;
	}
};

// Compare by term ID
class InMemoryPostingLessByTermName {
    public:
	int operator() (const InMemoryPosting &p1, const InMemoryPosting &p2)
	{
	    return p1.tname < p2.tname;
	}
};

// Class representing a term and the documents indexing it
class InMemoryTerm {
    public:
	vector<InMemoryPosting> docs;// Sorted list of documents indexing term
	void add_posting(const InMemoryPosting & post) {
	    // Add document to right place in list
	    vector<InMemoryPosting>::iterator p;
	    p = lower_bound(docs.begin(), docs.end(),
			    post,
			    InMemoryPostingLessByDocId());
	    if(p == docs.end() || InMemoryPostingLessByDocId()(post, *p)) {
		docs.insert(p, post);
	    } else {
		(*p).merge(post);
	    }
	}
};

// Class representing a document and the terms indexing it
class InMemoryDoc {
    public:
	vector<InMemoryPosting> terms;// Sorted list of terms indexing document
	void add_posting(const InMemoryPosting & post) {
	    // Add document to right place in list
	    vector<InMemoryPosting>::iterator p;
	    p = lower_bound(terms.begin(), terms.end(),
			    post,
			    InMemoryPostingLessByTermName());
	    if(p == terms.end() || InMemoryPostingLessByTermName()(post, *p)) {
		terms.insert(p, post);
	    } else {
		(*p).merge(post);
	    }
	}
};




// Post List
class InMemoryPostList : public virtual DBPostList {
    friend class InMemoryDatabase;
    private:
	vector<InMemoryPosting>::const_iterator pos;
	vector<InMemoryPosting>::const_iterator end;
	om_termname tname;
	om_doccount termfreq;
	bool started;

	const InMemoryDatabase * this_db;

	InMemoryPostList(const InMemoryDatabase * db,
			 const InMemoryTerm & term);
    public:
	om_doccount get_termfreq() const;

	om_docid  get_docid() const;     // Gets current docid
	om_weight get_weight() const;    // Gets current weight
	PostList *next(om_weight w_min); // Moves to next docid

	PostList *skip_to(om_docid did, om_weight w_min);// Moves to next docid >= specified docid

	bool   at_end() const;        // True if we're off the end of the list

	string intro_term_description() const;
};


// Term List
class InMemoryTermList : public virtual DBTermList {
    friend class InMemoryDatabase;
    private:
	vector<InMemoryPosting>::const_iterator pos;
	vector<InMemoryPosting>::const_iterator end;
	om_termcount terms;
	bool started;

	const InMemoryDatabase * this_db;
	om_doclength norm_len;

	InMemoryTermList(const InMemoryDatabase * db,
			 const InMemoryDoc & doc,
			 om_doclength len);
    public:
	om_termcount get_approx_size() const;

	OmExpandBits get_weighting() const;
	const om_termname get_termname() const;
	om_termcount get_wdf() const; // Number of occurences of term in current doc
	om_doccount get_termfreq() const;  // Number of docs indexed by term
	TermList * next();
	bool   at_end() const;
};


// Database
class InMemoryDatabase : public virtual IRDatabase {
    friend class DatabaseBuilder;
    private:
	map<om_termname, InMemoryTerm> postlists;
	vector<InMemoryDoc> termlists;
	vector<string> doclists;

	vector<om_doclength> doclengths;

	om_totlength totlen;

	bool opened; // Whether we have opened the database
	bool indexing; // Whether we have started to index to the database

	// Stop copy / assignment being allowed
	InMemoryDatabase& operator=(const InMemoryDatabase &);
	InMemoryDatabase(const InMemoryDatabase &);

	InMemoryDatabase();
	void open(const DatabaseBuilderParams & params);
    public:
	~InMemoryDatabase();

	void set_root(IRDatabase *);

	om_doccount  get_doccount() const;
	om_doclength get_avlength() const;
	om_doclength get_doclength(om_docid did) const;

	om_doccount get_termfreq(const om_termname & tname) const;
	bool term_exists(const om_termname & tname) const;

	DBPostList * open_post_list(const om_termname & tname, RSet * rset) const;
	DBTermList * open_term_list(om_docid did) const;
	OmDocument * open_document(om_docid did) const;

	void make_term(const om_termname & tname);
	om_docid make_doc(const om_docname & dname);
	void make_posting(const om_termname & tname,
			  om_docid did,
			  om_termpos position);
};




//////////////////////////////////////////////
// Inline function definitions for postlist //
//////////////////////////////////////////////

inline
InMemoryPostList::InMemoryPostList(const InMemoryDatabase * db,
				   const InMemoryTerm & term)
	: pos(term.docs.begin()),
	  end(term.docs.end()),
	  tname(pos->tname),
	  termfreq(term.docs.size()),
	  started(false),
	  this_db(db)
{
    // InMemoryPostLists cannot be empty
    Assert(pos != end);
}

inline om_doccount
InMemoryPostList::get_termfreq() const
{
    return termfreq;
}

inline om_docid
InMemoryPostList::get_docid() const
{   
    //DebugMsg(tname << ".get_docid()");
    Assert(started);
    Assert(!at_end());
    //DebugMsg(" = " << (*pos).did << endl);
    return (*pos).did;
}

inline PostList *
InMemoryPostList::next(om_weight w_min)
{
    //DebugMsg(tname << ".next()" << endl);
    if(started) {
	Assert(!at_end());
	pos++;
    } else {
	started = true;
    }
    return NULL;
}

inline PostList *
InMemoryPostList::skip_to(om_docid did, om_weight w_min)
{
    //DebugMsg(tname << ".skip_to(" << did << ")" << endl);
    // FIXME - see if we can make more efficient, perhaps using better
    // data structure.  Note, though, that a binary search of
    // the remaining list may NOT be a good idea (search time is then
    // O(log {length of list}), as opposed to O(distance we want to skip)
    // Since we will frequently only be skipping a short distance, this
    // could well be worse.
    Assert(!at_end());
    started = true;
    while (!at_end() && (*pos).did < did) {
	(void) next(w_min);
    }
    return NULL;
}

inline bool
InMemoryPostList::at_end() const
{
    if(pos != end) return false;
    return true;
}


inline string
InMemoryPostList::intro_term_description() const
{
    return tname + ":" + inttostring(termfreq);
}


//////////////////////////////////////////////
// Inline function definitions for termlist //
//////////////////////////////////////////////

inline InMemoryTermList::InMemoryTermList(const InMemoryDatabase * db,
					  const InMemoryDoc & doc,
					  om_doclength len)
	: pos(doc.terms.begin()),
	  end(doc.terms.end()),
	  terms(doc.terms.size()),
	  started(false),
	  this_db(db)
{
    norm_len = len / this_db->get_avlength();
    return;
}

inline om_termcount InMemoryTermList::get_approx_size() const
{
    return terms;
}

inline OmExpandBits InMemoryTermList::get_weighting() const
{
    Assert(started);
    Assert(!at_end());
    Assert(wt != NULL);

    return wt->get_bits(InMemoryTermList::get_wdf(),
			norm_len,
			InMemoryTermList::get_termfreq(),
			this_db->get_doccount());
}

inline const om_termname InMemoryTermList::get_termname() const
{
    Assert(started);
    Assert(!at_end());
    return (*pos).tname;
}

inline om_termcount InMemoryTermList::get_wdf() const
{
    Assert(started);
    Assert(!at_end());
    return (*pos).positions.size();
}

inline om_doccount InMemoryTermList::get_termfreq() const
{
    Assert(started);
    Assert(!at_end());

    return this_db->get_termfreq((*pos).tname);
}

inline TermList * InMemoryTermList::next()
{
    if(started) {
	Assert(!at_end());
	pos++;
    } else {
	started = true;
    }
    return NULL;
}

inline bool InMemoryTermList::at_end() const
{
    Assert(started);
    if(pos != end) return false;
    return true;
}




//////////////////////////////////////////////
// Inline function definitions for database //
//////////////////////////////////////////////

inline om_doccount
InMemoryDatabase::get_doccount() const
{
    Assert(opened);
    return postlists.size();
}

inline om_doclength
InMemoryDatabase::get_avlength() const
{
    Assert(opened);
    om_doccount docs = InMemoryDatabase::get_doccount();
    Assert(docs != 0);
    return ((om_doclength) totlen) / docs;
}

inline om_doccount
InMemoryDatabase::get_termfreq(const om_termname & tname) const
{
    Assert(opened);

    map<om_termname, InMemoryTerm>::const_iterator i = postlists.find(tname);
    if(i == postlists.end()) return 0;
    return i->second.docs.size();
}

inline om_doclength
InMemoryDatabase::get_doclength(om_docid did) const
{
    Assert(opened);
    Assert(did > 0 && did <= termlists.size());

    return doclengths[did - 1];
}

#endif /* OM_HGUARD_INMEMORY_DATABASE_H */
