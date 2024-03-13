uniform float scale2;

[numthreads(32,1,1)]
[shader("compute")]
void main(
    RWStructuredBuffer<float> data,
    uniform float scale,
    uniform float offset,
	uint3 index: SV_DispatchThreadID) {
    float v = data[index.x] * scale + offset;
    data[index.x] = v * scale2;
}