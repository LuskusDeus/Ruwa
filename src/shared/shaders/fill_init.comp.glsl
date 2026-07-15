#version 450 core
// SPDX-License-Identifier: MPL-2.0

layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0) uniform sampler2D uSourceLayer;
layout(binding = 1) uniform sampler2D uSelectionMask;
layout(r32ui, binding = 0) uniform coherent uimage2D uVisited;

layout(std430, binding = 1) buffer CurrentFrontierBuffer {
    uint currentFrontier[];
};

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

uniform ivec2 uCanvasSize;
uniform ivec2 uSeedPixel;
uniform ivec2 uWindowOrigin;
uniform ivec2 uDomainOrigin;
uniform ivec2 uDomainSize;
uniform vec4 uSeedColor;
uniform int uThreshold;
uniform int uSeedMode;
uniform int uFillMode;
uniform int uUseSelectionMask;

const ivec2 kNeighborOffsets[4] = ivec2[](
    ivec2(0, -1),
    ivec2(-1, 0),
    ivec2(1, 0),
    ivec2(0, 1)
);

bool insideWindow(ivec2 pixelCoord) {
    return pixelCoord.x >= 0 && pixelCoord.y >= 0
        && pixelCoord.x < uCanvasSize.x && pixelCoord.y < uCanvasSize.y;
}

ivec2 globalCoord(ivec2 pixelCoord) {
    return pixelCoord + uWindowOrigin;
}

bool insideDomain(ivec2 coord) {
    ivec2 localCoord = coord - uDomainOrigin;
    return localCoord.x >= 0 && localCoord.y >= 0
        && localCoord.x < uDomainSize.x && localCoord.y < uDomainSize.y;
}

bool isAllowed(ivec2 pixelCoord) {
    if (!insideWindow(pixelCoord)) {
        return false;
    }
    if (uUseSelectionMask == 0) {
        return true;
    }
    return texelFetch(uSelectionMask, pixelCoord, 0).a > 0.0;
}

uint premultipliedDistanceToSeed(ivec2 pixelCoord) {
    vec4 src = texelFetch(uSourceLayer, pixelCoord, 0);
    vec4 scaled = abs(src - uSeedColor) * 255.0;
    float distanceValue = max(max(scaled.r, scaled.g), max(scaled.b, scaled.a));
    return uint(round(distanceValue));
}

vec3 straightRgb(vec4 px) {
    if (px.a <= 0.0) {
        return vec3(0.0);
    }
    return clamp(px.rgb / px.a, 0.0, 1.0);
}

uint materialDistanceToSeed(ivec2 pixelCoord) {
    vec4 src = texelFetch(uSourceLayer, pixelCoord, 0);
    vec3 srcStraight = straightRgb(src);
    vec3 seedStraight = straightRgb(uSeedColor);
    vec3 colorScaled = abs(srcStraight - seedStraight) * 255.0;
    float colorDistance = max(colorScaled.r, max(colorScaled.g, colorScaled.b));
    float alphaDistance = abs(src.a - uSeedColor.a) * 255.0 / 64.0;
    return uint(round(max(colorDistance, alphaDistance)));
}

uint distanceToSeed(ivec2 pixelCoord) {
    vec4 src = texelFetch(uSourceLayer, pixelCoord, 0);
    if (uFillMode == 1) {
        if (src.a <= 0.0) {
            return 255u;
        }
        return materialDistanceToSeed(pixelCoord);
    }
    vec4 scaled = abs(src - uSeedColor) * 255.0;
    float distanceValue = max(max(scaled.r, scaled.g), max(scaled.b, scaled.a));
    return uint(round(distanceValue));
}

bool isEscapeBoundary(ivec2 pixelCoord) {
    if (!insideWindow(pixelCoord) || !isAllowed(pixelCoord)) {
        return false;
    }

    ivec2 globalPixel = globalCoord(pixelCoord);
    ivec2 localPixel = globalPixel - uDomainOrigin;
    if (localPixel.x == 0 || localPixel.y == 0
        || localPixel.x + 1 == uDomainSize.x || localPixel.y + 1 == uDomainSize.y) {
        return true;
    }

    if (uUseSelectionMask == 0) {
        return false;
    }

    for (int i = 0; i < 4; ++i) {
        ivec2 neighbor = pixelCoord + kNeighborOffsets[i];
        ivec2 globalNeighbor = globalCoord(neighbor);
        if (!insideDomain(globalNeighbor)) {
            return true;
        }
        if (insideWindow(neighbor) && !isAllowed(neighbor)) {
            return true;
        }
    }

    return false;
}

bool isPreserveBoundary(ivec2 pixelCoord) {
    if (!insideWindow(pixelCoord) || !isAllowed(pixelCoord) || uUseSelectionMask == 0) {
        return false;
    }

    for (int i = 0; i < 4; ++i) {
        ivec2 neighbor = pixelCoord + kNeighborOffsets[i];
        ivec2 globalNeighbor = globalCoord(neighbor);
        if (!insideDomain(globalNeighbor)) {
            return true;
        }
        if (insideWindow(neighbor) && !isAllowed(neighbor)) {
            return true;
        }
    }

    return false;
}

bool isWindowBoundary(ivec2 pixelCoord) {
    return pixelCoord.x == 0 || pixelCoord.y == 0
        || pixelCoord.x + 1 == uCanvasSize.x || pixelCoord.y + 1 == uCanvasSize.y;
}

void enqueueSeed(ivec2 pixelCoord) {
    if (!isAllowed(pixelCoord)) {
        return;
    }
    if (distanceToSeed(pixelCoord) > uint(max(uThreshold, 0))) {
        return;
    }

    uint previous = imageAtomicCompSwap(uVisited, pixelCoord, 0u, 1u);
    if (previous != 0u) {
        return;
    }

    uint packedIndex = uint(pixelCoord.y * uCanvasSize.x + pixelCoord.x);
    uint writeIndex = atomicAdd(currentCount, 1u);
    currentFrontier[writeIndex] = packedIndex;

    atomicAdd(pixelsFilled, 1u);
    atomicMin(minX, uint(pixelCoord.x));
    atomicMin(minY, uint(pixelCoord.y));
    atomicMax(maxX, uint(pixelCoord.x));
    atomicMax(maxY, uint(pixelCoord.y));
    if (isEscapeBoundary(pixelCoord)) {
        atomicOr(touchesBoundary, 1u);
    }
    if (isWindowBoundary(pixelCoord)) {
        atomicOr(touchesWindowBoundary, 1u);
    }
}

void main() {
    ivec2 pixelCoord = ivec2(gl_GlobalInvocationID.xy);
    if (!insideWindow(pixelCoord)) {
        return;
    }

    if (uSeedMode == 0) {
        if (pixelCoord == uSeedPixel) {
            enqueueSeed(pixelCoord);
        }
        return;
    }

    if (isPreserveBoundary(pixelCoord)) {
        enqueueSeed(pixelCoord);
    }
}
