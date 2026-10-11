// SPDX-License-Identifier: Apache-2.0
#include <assert.h>
#include <stdio.h>
#include "dev_face_core.h"

int main(void)
{
    // Test bounding box clamp to 480x480 view
    dev_face_box_t b1 = {-10, 50, 100, 100};
    dev_face_box_t c1 = dev_face_box_clamp(b1, 480, 480);
    assert(c1.x == 0 && c1.y == 50 && c1.w == 90 && c1.h == 100);

    // Test box with coordinates overflowing screen
    dev_face_box_t b2 = {400, 420, 120, 100};
    dev_face_box_t c2 = dev_face_box_clamp(b2, 480, 480);
    assert(c2.x == 400 && c2.y == 420 && c2.w == 80 && c2.h == 60);

    // Test aspect center
    dev_face_box_t b3 = {100, 100, 200, 200};
    int cx = 0, cy = 0;
    dev_face_box_center(b3, &cx, &cy);
    assert(cx == 200 && cy == 200);

    printf("dev_face_core: all checks passed\n");
    return 0;
}
