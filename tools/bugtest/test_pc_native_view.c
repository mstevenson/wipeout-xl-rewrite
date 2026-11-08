#include "../../game/src/portfar.c"
#include <assert.h>
#include <stdio.h>

TrackData *track;

int main(void)
{
    TrackSection sections[4] = {0};
    TrackData course = {.sectionCount = 4, .sections = sections};
    track = &course;
    sections[3].nextSection.ptr = &sections[0];
    sections[0].nextSection.ptr = &sections[2];
    sections[2].nextSection.ptr = &sections[1];
    short near[] = {0, 1}, middle[] = {3, 1}, far[] = {3, 4};
    short *spans[] = {near, middle, far};
    const short sizes[] = {2, 2, 2};
    unsigned char details[4] = {0};

    PcViewRuns(spans, sizes, 2, details);
    assert(details[0] == 3 && details[1] == 0 && details[2] == 0 && details[3] == 2);
    PcViewRuns(spans, sizes, 65535, details);
    assert(details[0] == 3 && details[1] == 1 && details[2] == 1 && details[3] == 2);
    puts("Native PC view runs cap distant sections, follow section links and keep the highest detail: OK");
    return 0;
}
