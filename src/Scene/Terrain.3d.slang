import GBuffer;
import Transform;
import cbt.leb;

enum SubdivisionMode {
    eTriangle,
    eSquare
};
static SubdivisionMode gMode = SubdivisionMode::eTriangle;

using namespace RoseEngine;

uniform Transform transform;

[shader("vertex")]
float4 vertexMain(out uint nodeId: TEXCOORD0, uint instanceId: SV_InstanceID, uint vertexId: SV_VertexID) : SV_Position {
    let node = cbt_DecodeNode(0, instanceId);

	nodeId = node.id;

    float3x2 faceVertices = float3x2(0, 1, 0, 0, 1, 0);
    if (gMode == SubdivisionMode::eTriangle)
        faceVertices = leb_DecodeNodeAttributeArray(node, faceVertices);
    else
        faceVertices = leb_DecodeNodeAttributeArray_Square(node, faceVertices);
    return float4((2 * faceVertices[vertexId] - 1), 0.5, 1);
}

[shader("fragment")]
GBuffer fragmentMain(nointerpolation uint nodeId: TEXCOORD0) {
    const uint3 c = uint3(nodeId * 4589029, (nodeId + 897563) * 1273, (nodeId + 61284523) * 34125) % 255;
    GBuffer o = {};
    o.color = float4(float3(c) / 255, 1);
    o.visibility = uint4(0, nodeId, 0, 0);
    return o;
}
