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

/* Fractional viewport position for drawing. Keep the integer top for paging
 * and selection; draw the edge rows too while they enter/leave the viewport.
 * A negative current starts a fresh list. Large jumps (pages/wrapping) snap
 * so focus cannot disappear while crossing an entire viewport. */
#ifdef __GNUC__
__attribute__((unused))
#endif
static inline float listSlideTop(float current, int target, int selected, int count, int rows, float blend)
{
	int maximum = count > rows ? count - rows : 0;
	float delta;

	if (rows <= 0 || count <= 0)
		return 0.0f;
	if (target < 0) target = 0;
	if (target > maximum) target = maximum;
	delta = target - current;
	if (current < 0.0f || current > maximum || selected < current || selected >= current + rows ||
	    delta >= rows || delta <= -rows)
		return (float)target;
	if (blend < 0.0f) blend = 0.0f;
	if (blend > 1.0f) blend = 1.0f;
	current += delta * blend;
	delta = target - current;
	return delta > -0.001f && delta < 0.001f ? (float)target : current;
}

#endif
