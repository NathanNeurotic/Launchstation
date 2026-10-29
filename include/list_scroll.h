#ifndef LIST_SCROLL_H
#define LIST_SCROLL_H

/* Adapted from saildot4k/Launchstation b169f80.
 * Match the file browser: keep focus on the upper middle row, clamping the
 * viewport at either end. Headers count as rows even when not selectable. */
static inline int listCenteredTop(int selected, int count, int rows)
{
	int top, max_top;

	if (rows <= 0 || count <= rows)
		return 0;
	if (selected < 0)
		selected = 0;
	else if (selected >= count)
		selected = count - 1;
	top = selected - (rows - 1) / 2;
	max_top = count - rows;
	return top < 0 ? 0 : top > max_top ? max_top : top;
}

#endif
