/*
 * Copyright 2006 Timo Hirvonen
 */

#include "track.h"
#include "iter.h"
#include "track_info.h"
#include "search_mode.h"
#include "window.h"
#include "options.h"
#include "comment.h"
#include "uchar.h"
#include "xmalloc.h"

#include <string.h>

int xstrcasecmp(const char *a, const char *b)
{
	if (a == NULL) {
		if (b == NULL)
			return 0;
		return -1;
	} else if (b == NULL) {
		return 1;
	}
	return u_strcasecmp(a, b);
}

void simple_track_init(struct simple_track *track, struct track_info *ti)
{
	track->info = ti;
	track->marked = 0;
	track->disc = comments_get_int(ti->comments, "discnumber");
	track->num = comments_get_int(ti->comments, "tracknumber");
}

struct simple_track *simple_track_new(struct track_info *ti)
{
	struct simple_track *t = xnew(struct simple_track, 1);

	track_info_ref(ti);
	simple_track_init(t, ti);
	return t;
}

GENERIC_ITER_PREV(simple_track_get_prev, struct simple_track, node)
GENERIC_ITER_NEXT(simple_track_get_next, struct simple_track, node)

int simple_track_search_get_current(void *data, struct iter *iter)
{
	return window_get_sel(data, iter);
}

int simple_track_search_matches(void *data, struct iter *iter, const char *text)
{
	unsigned int flags = TI_MATCH_TITLE;
	struct simple_track *track = iter_to_simple_track(iter);

	if (!search_restricted)
		flags |= TI_MATCH_ARTIST | TI_MATCH_ALBUM;

	if (!track_info_matches(track->info, text, flags))
		return 0;

	window_set_sel(data, iter);
	return 1;
}

struct shuffle_track *shuffle_list_get_next(struct list_head *head, struct shuffle_track *cur,
		int (*filter)(const struct simple_track *))
{
	struct list_head *item;

	if (cur == NULL)
		return to_shuffle_track(head->next);

	item = cur->node.next;
again:
	while (item != head) {
		struct shuffle_track *track = to_shuffle_track(item);

		if (filter((struct simple_track *)track))
			return track;
		item = item->next;
	}
	if (repeat) {
		if (auto_reshuffle)
			reshuffle(head);
		item = head->next;
		goto again;
	}
	return NULL;
}

struct shuffle_track *shuffle_list_get_prev(struct list_head *head, struct shuffle_track *cur,
		int (*filter)(const struct simple_track *))
{
	struct list_head *item;

	if (cur == NULL)
		return to_shuffle_track(head->next);

	item = cur->node.prev;
again:
	while (item != head) {
		struct shuffle_track *track = to_shuffle_track(item);

		if (filter((struct simple_track *)track))
			return track;
		item = item->prev;
	}
	if (repeat) {
		if (auto_reshuffle)
			reshuffle(head);
		item = head->prev;
		goto again;
	}
	return NULL;
}

struct simple_track *simple_list_get_next(struct list_head *head, struct simple_track *cur,
		int (*filter)(const struct simple_track *))
{
	struct list_head *item;

	if (cur == NULL)
		return to_simple_track(head->next);

	item = cur->node.next;
again:
	while (item != head) {
		struct simple_track *track = to_simple_track(item);

		if (filter(track))
			return track;
		item = item->next;
	}
	item = head->next;
	if (repeat)
		goto again;
	return NULL;
}

struct simple_track *simple_list_get_prev(struct list_head *head, struct simple_track *cur,
		int (*filter)(const struct simple_track *))
{
	struct list_head *item;

	if (cur == NULL)
		return to_simple_track(head->next);

	item = cur->node.prev;
again:
	while (item != head) {
		struct simple_track *track = to_simple_track(item);

		if (filter(track))
			return track;
		item = item->prev;
	}
	item = head->prev;
	if (repeat)
		goto again;
	return NULL;
}

int simple_track_cmp(const struct list_head *a_head, const struct list_head *b_head, char **keys)
{
	const struct simple_track *a = to_simple_track(a_head);
	const struct simple_track *b = to_simple_track(b_head);
	int i, res = 0;

	for (i = 0; keys[i]; i++) {
		const char *key = keys[i];
		const char *av, *bv;
		const struct track_info *ai, *bi;

		/* numeric compare for tracknumber and discnumber */
		if (strcmp(key, "tracknumber") == 0) {
			res = a->num - b->num;
			if (res)
				break;
		}
		if (strcmp(key, "discnumber") == 0) {
			res = a->disc - b->disc;
			if (res)
				break;
		}
		ai = a->info;
		bi = b->info;
		if (strcmp(key, "filename") == 0) {
			/* NOTE: filenames are not necessarily UTF-8 */
			res = strcasecmp(ai->filename, bi->filename);
			if (res)
				break;
		}
		av = comments_get_val(ai->comments, key);
		bv = comments_get_val(bi->comments, key);
		res = xstrcasecmp(av, bv);
		if (res)
			break;
	}
	return res;
}

void sorted_list_add_track(struct list_head *head, struct simple_track *track, char **keys)
{
	struct list_head *item;

	/* It is _much_ faster to iterate in reverse order because playlist
	 * file is usually sorted.
	 */
	item = head->prev;
	while (item != head) {
		if (simple_track_cmp(&track->node, item, keys) >= 0)
			break;
		item = item->prev;
	}
	/* add after item */
	list_add(&track->node, item);
}

void shuffle_list_add_track(struct list_head *head, struct list_head *node, int nr)
{
	struct list_head *item;
	int pos;

	pos = rand() % (nr + 1);
	item = head;
	if (pos <= nr / 2) {
		while (pos) {
			item = item->next;
			pos--;
		}
		/* add after item */
		list_add(node, item);
	} else {
		pos = nr - pos;
		while (pos) {
			item = item->prev;
			pos--;
		}
		/* add before item */
		list_add_tail(node, item);
	}
}

void reshuffle(struct list_head *head)
{
	struct list_head *item, *last;
	int i = 0;

	if (list_empty(head))
		return;

	last = head->prev;
	item = head->next;
	list_init(head);

	while (1) {
		struct list_head *next = item->next;

		shuffle_list_add_track(head, item, i++);
		if (item == last)
			break;
		item = next;
	}
}

int simple_list_for_each_marked(struct list_head *head,
		int (*cb)(void *data, struct track_info *ti), void *data, int reverse)
{
	struct simple_track *t;
	int rc = 0;

	if (reverse) {
		list_for_each_entry_reverse(t, head, node) {
			if (t->marked) {
				rc = cb(data, t->info);
				if (rc)
					break;
			}
		}
	} else {
		list_for_each_entry(t, head, node) {
			if (t->marked) {
				rc = cb(data, t->info);
				if (rc)
					break;
			}
		}
	}
	return rc;
}
