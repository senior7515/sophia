
/*
 * sophia database
 * sphia.org
 *
 * Copyright (c) Dmitry Simonenko
 * BSD License
*/

#include <libsr.h>
#include <libsv.h>
#include <libsx.h>
#include <libsl.h>
#include <libsd.h>
#include <libsi.h>
#include <libse.h>
#include <libso.h>

static int
so_dbctl_init(sodbctl *c, char *name, void *db)
{
	memset(c, 0, sizeof(*c));
	sodb *o = db;
	so *e = so_of(&o->o);
	c->name = sr_strdup(&e->a, name);
	if (srunlikely(c->name == NULL)) {
		sr_error(&e->error, "%s", "memory allocation failed");
		return -1;
	}
	c->parent  = db;
	c->created = 0;
	c->sync    = 1;
	sr_cmpset(&c->cmp, "string");
	return 0;
}

static int
so_dbctl_free(sodbctl *c)
{
	sodb *o = c->parent;
	so *e = so_of(&o->o);
	if (so_dbactive(o))
		return -1;
	if (c->name) {
		sr_free(&e->a, c->name);
		c->name = NULL;
	}
	if (c->path) {
		sr_free(&e->a, c->path);
		c->path = NULL;
	}
	return 0;
}

static int
so_dbctl_validate(sodbctl *c)
{
	sodb *o = c->parent;
	so *e = so_of(&o->o);
	if (c->path)
		return 0;
	char path[1024];
	snprintf(path, sizeof(path), "%s/%s", e->ctl.path, c->name);
	c->path = sr_strdup(&e->a, path);
	if (srunlikely(c->path == NULL)) {
		sr_error(&e->error, "%s", "memory allocation failed");
		return -1;
	}
	return 0;
}

static int
so_dbopen(soobj *obj, va_list args srunused)
{
	sodb *o = (sodb*)obj;
	so *e = so_of(&o->o);
	int status = so_status(&o->status);
	if (status == SO_RECOVER)
		goto online;
	if (status != SO_OFFLINE)
		return -1;
	o->r.cmp = &o->ctl.cmp;
	sx_indexset(&o->coindex, o->ctl.id, o->r.cmp);
	int rc = so_dbctl_validate(&o->ctl);
	if (srunlikely(rc == -1))
		return -1;
	rc = so_recoverbegin(o);
	if (srunlikely(rc == -1))
		return -1;
	if (so_status(&e->status) == SO_RECOVER)
		return 0;
online:
	so_recoverend(o);
	rc = so_scheduler_add(&e->sched, o);
	if (srunlikely(rc == -1))
		return -1;
	return 0;
}

static int
so_dbdestroy(soobj *obj, va_list args srunused)
{
	sodb *o = (sodb*)obj;
	so *e = so_of(&o->o);
	so_statusset(&o->status, SO_SHUTDOWN);
	int rcret = 0;
	int rc;
	rc = so_scheduler_del(&e->sched, o);
	if (srunlikely(rc == -1))
		rcret = -1;
	rc = so_objindex_destroy(&o->cursor);
	if (srunlikely(rc == -1))
		rcret = -1;
	sx_indexfree(&o->coindex, &e->xm);
	rc = si_close(&o->index, &o->r);
	if (srunlikely(rc == -1))
		rcret = -1;
	so_dbctl_free(&o->ctl);
	sd_cfree(&o->dc, &o->r);
	so_statusfree(&o->status);
	so_objindex_unregister(&e->db, &o->o);
	sr_free(&e->a_db, o);
	return rcret;
}

static int
so_dberror(soobj *obj, va_list args srunused)
{
	sodb *o = (sodb*)obj;
	int status = so_status(&o->status);
	if (status == SO_MALFUNCTION)
		return 1;
	return 0;
}

static int
so_dbset(soobj *obj, va_list args)
{
	sodb *o = (sodb*)obj;
	return so_txdbset(o, SVSET, args);
}

static void*
so_dbget(soobj *obj, va_list args)
{
	sodb *o = (sodb*)obj;
	return so_txdbget(o, 0, args);
}

static int
so_dbdel(soobj *obj, va_list args)
{
	sodb *o = (sodb*)obj;
	return so_txdbset(o, SVDELETE, args);
}

static void*
so_dbcursor(soobj *o, va_list args)
{
	sodb *db = (sodb*)o;
	return so_cursornew(db, 0, args);
}

static void*
so_dbobj(soobj *obj, va_list args srunused)
{
	sodb *o = (sodb*)obj;
	so *e = so_of(&o->o);
	return so_vnew(e, obj);
}

static void*
so_dbtype(soobj *o srunused, va_list args srunused) {
	return "database";
}

static soobjif sodbif =
{
	.ctl      = NULL,
	.open     = so_dbopen,
	.destroy  = so_dbdestroy,
	.error    = so_dberror,
	.set      = so_dbset,
	.get      = so_dbget,
	.del      = so_dbdel,
	.drop     = NULL,
	.begin    = NULL,
	.prepare  = NULL,
	.commit   = NULL,
	.cursor   = so_dbcursor,
	.object   = so_dbobj,
	.type     = so_dbtype
};

soobj *so_dbnew(so *e, char *name)
{
	sodb *o = sr_malloc(&e->a_db, sizeof(sodb));
	if (srunlikely(o == NULL)) {
		sr_error(&e->error, "%s", "memory allocation failed");
		return NULL;
	}
	memset(o, 0, sizeof(*o));
	so_objinit(&o->o, SODB, &sodbif, &e->o);
	so_objindex_init(&o->cursor);
	so_statusinit(&o->status);
	so_statusset(&o->status, SO_OFFLINE);
	o->r     = e->r;
	o->r.cmp = &o->ctl.cmp;
	int rc = so_dbctl_init(&o->ctl, name, o);
	if (srunlikely(rc == -1)) {
		sr_free(&e->a_db, o);
		return NULL;
	}
	rc = si_init(&o->index, &o->r, &e->quota);
	if (srunlikely(rc == -1)) {
		sr_free(&e->a_db, o);
		so_dbctl_free(&o->ctl);
		return NULL;
	}
	o->ctl.id = sr_seq(&e->seq, SR_DSNNEXT);
	sx_indexinit(&o->coindex, o);
	sd_cinit(&o->dc, &o->r);
	return &o->o;
}

soobj *so_dbmatch(so *e, char *name)
{
	srlist *i;
	sr_listforeach(&e->db.list, i) {
		sodb *db = (sodb*)srcast(i, soobj, link);
		if (strcmp(db->ctl.name, name) == 0)
			return &db->o;
	}
	return NULL;
}

soobj *so_dbmatch_id(so *e, uint32_t id)
{
	srlist *i;
	sr_listforeach(&e->db.list, i) {
		sodb *db = (sodb*)srcast(i, soobj, link);
		if (db->ctl.id == id)
			return &db->o;
	}
	return NULL;
}

int so_dbmalfunction(sodb *o)
{
	so_statusset(&o->status, SO_MALFUNCTION);
	return -1;
}
