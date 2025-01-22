[[vk::push_constant]]
cbuffer PushConstants {
    float scale;
    float offset;
};

uniform float scale2;
uniform float offset2;

RWStructuredBuffer<float> data;

[numthreads(32,1,1)]
[shader("compute")]
void main(uint3 index: SV_DispatchThreadID) {
    float v = data[index.x];
    v = v * scale  + offset;
    v = v * scale2 + offset2;
    data[index.x] = v;


    if (index.x == 0) {
		printf("scale = %0.2f\n", scale);
        printf("offset = %0.2f\n", offset);

        printf("scale2 = %0.2f\n", scale2);
        printf("offset2 = %0.2f\n", offset2);
    }
}