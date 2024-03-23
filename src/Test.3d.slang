struct v2f {
    float4 pos: SV_POSITION;
    float3 color: COLOR0;
};

#include "Scene/Transform.h"
using namespace RoseEngine;

uniform Transform objectToWorld;
uniform Transform worldToCamera;
uniform Transform projection;

[shader("vertex")]
v2f vertexMain(float3 pos: POSITION, float3 color: COLOR0) {
    v2f o = {};

    const float3 cameraPos = (worldToCamera * objectToWorld) * pos;

    o.pos = projection * float4(cameraPos, 1);
    o.pos.y = -o.pos.y;
    o.color = color;
    return o;
}

[shader("fragment")]
float4 fragmentMain(v2f i) : SV_Target0 {
    return float4(i.color, 1);
}