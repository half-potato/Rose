namespace RoseEngine {

struct GBuffer {
    float4 color: SV_Target0;
    uint4  visibility: SV_Target1;
};

}