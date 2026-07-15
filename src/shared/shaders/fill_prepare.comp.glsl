#version 450 core
// SPDX-License-Identifier: MPL-2.0

layout(local_size_x = 1) in;

layout(std430, binding = 3) buffer CounterBuffer {
    uint currentCount;
    uint nextCount;
    uint pixelsFilled;
    uint minX;
    uint minY;
    uint maxX;
    uint maxY;
    uint touchesBoundary;
    uint touchesWindowBoundary;
};

layout(std430, binding = 4) buffer DispatchBuffer {
    uint groupsX;
    uint groupsY;
    uint groupsZ;
};

uniform int uWorkGroupSize;

void main() {
    currentCount = nextCount;
    nextCount = 0u;

    uint workGroupSize = max(uint(uWorkGroupSize), 1u);
    groupsX = max((currentCount + workGroupSize - 1u) / workGroupSize, 1u);
    groupsY = 1u;
    groupsZ = 1u;
}
