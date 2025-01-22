uniform float scale;
uniform float offset;
uniform uint  blurRadius;
uniform uint  dataSize;

RWStructuredBuffer<float> data;
RWStructuredBuffer<float> data2;

[numthreads(32, 1, 1)]
[shader("compute")]
void applyScaleOffset(uint3 index: SV_DispatchThreadID) {
    if (index.x >= dataSize) return;
    float v = data[index.x];
    v = v * scale  + offset;
    data[index.x] = v;
}

[numthreads(32, 1, 1)]
[shader("compute")]
void blur(uint3 index: SV_DispatchThreadID) {
    if (index.x >= dataSize) return;
    float v = 0;
    float wsum = 0;
    for (int i = -(int)blurRadius; i <= (int)blurRadius; i++) {
        int p = (int)index.x + i;
        if (p >= 0 && p < (int)dataSize) {
            float w = 1 / float(1 << (1 + abs(i)));
            wsum += w;
        	v += data[p] * w;
		}
    }
    data2[index.x] = wsum > 0 ? v/wsum : 0;
}