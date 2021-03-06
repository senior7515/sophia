
/*
 * sophia database
 * sphia.org
 *
 * Copyright (c) Dmitry Simonenko
 * BSD License
*/

#include <libsr.h>
#include <libsv.h>
#include <libsd.h>

int sd_mergeinit(sdmerge *m, sr *r, uint32_t parent,
                 sriter *i,
                 sdbuild *build,
                 uint64_t offset,
                 uint32_t size_key,
                 uint32_t size_stream,
                 uint64_t size_node,
                 uint32_t size_page, int save_delete,
                 uint64_t vlsn)
{
	m->r           = r;
	m->parent      = parent;
	m->build       = build;
	m->offset      = offset;
	m->size_key    = size_key;
	m->size_stream = size_stream;
	m->size_node   = size_node;
	m->size_page   = size_page;
	sd_indexinit(&m->index);
	m->merge       = i;
	sr_iterinit(&m->i, &sv_writeiter, r);
	sr_iteropen(&m->i, i, (uint64_t)size_page, sizeof(sdv), vlsn,
	            save_delete);
	return 0;
}

int sd_mergefree(sdmerge *m)
{
	sd_indexfree(&m->index, m->r);
	return 0;
}

int sd_merge(sdmerge *m)
{
	if (srunlikely(! sr_iterhas(&m->i)))
		return 0;
	sd_buildreset(m->build);

	sd_indexinit(&m->index);
	int rc = sd_indexbegin(&m->index, m->r, m->size_key, m->offset);
	if (srunlikely(rc == -1))
		return -1;

	uint32_t processed = sv_writeiter_total(&m->i); /* kv */
	uint32_t processed_last = 0;
	assert(processed <= m->size_stream);
	uint64_t left = (m->size_stream - processed);
	uint64_t limit;
	if (left >= (m->size_node * 2))
		limit = m->size_node;
	else
	if (left > (m->size_node))
		limit = m->size_node / 2;
	else
		limit = left;

	while (sr_iterhas(&m->i) && (processed_last <= limit))
	{
		rc = sd_buildbegin(m->build);
		if (srunlikely(rc == -1))
			return -1;
		while (sr_iterhas(&m->i)) {
			sv *v = sr_iterof(&m->i);
			rc = sd_buildadd(m->build, v, sv_mergeisdup(m->merge));
			if (srunlikely(rc == -1))
				return -1;
			sr_iternext(&m->i);
		}
		rc = sd_buildend(m->build);
		if (srunlikely(rc == -1))
			return -1;

		/* page offset is relative to index:
		 *
		 * m->offset + (index_size) + page->offset
		*/
		sdpageheader *h = sd_buildheader(m->build);
		rc = sd_indexadd(&m->index, m->r,
		                 sd_buildoffset(m->build),
		                 h->size + sizeof(sdpageheader),
		                 h->count,
		                 sd_buildminkey(m->build),
		                 sd_buildmin(m->build)->keysize,
		                 sd_buildmaxkey(m->build),
		                 sd_buildmax(m->build)->keysize,
		                 h->countdup,
		                 h->lsnmindup,
		                 h->lsnmin,
		                 h->lsnmax);
		if (srunlikely(rc == -1))
			return -1;
		sd_buildcommit(m->build);

		processed_last = sv_writeiter_total(&m->i) -
		                 processed;
		if (srunlikely(! sv_writeiter_resume(&m->i)))
			break;
	}
	return 1;
}

int sd_mergecommit(sdmerge *m, sdid *id)
{
	sd_indexcommit(&m->index, id);
	return 0;
}
