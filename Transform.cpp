#include "Transform.h"
#include "Native.h"

#include "logging.h"

#include <math.h>

void Transform::SetPosition(Vector3 &pos) {
    spatial_data.pos = pos;
}

float Transform::GetRotation() {
    return atan2f(*(float*)((uintptr_t)this + 0x28), *(float*)((uintptr_t)this + 0x20));
}

void Transform::SetRotation(float radians) {
    *(float*)((uintptr_t)this + 0x20) = cosf(radians);
    *(float*)((uintptr_t)this + 0x28) = sinf(radians);
}