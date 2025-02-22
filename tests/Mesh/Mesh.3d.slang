struct v2f {
    float4 pos: SV_POSITION;
    float3 color: COLOR0;
};

[shader("vertex")]
v2f vertexMain(float3 pos: POSITION, float3 color: COLOR0) {
    v2f o = {};
    o.pos = float4(pos.xy, 0.5, 1);
    o.color = color;
    return o;
}

[shader("fragment")]
float4 fragmentMain(v2f i) : SV_Target0 {
    return float4(i.color, 1);
}