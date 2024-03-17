[[vk::push_constant]]
cbuffer PushConstants {
    float scale;
    float offset;
};

uniform float scale2;
uniform float offset2;

struct Block {
    float scale3;
    float offset3;
};
ParameterBlock<Block> gBlock;

cbuffer Block2 {
    float scale4;
    float offset4;
}

RWStructuredBuffer<float> data;
RWStructuredBuffer<float> datas[2];

[numthreads(32,1,1)]
[shader("compute")]
void main(uint3 index: SV_DispatchThreadID) {
    float v = data[index.x];
    v = v * scale  + offset;
    v = v * scale2 + offset2;
    v = v * gBlock.scale3 + gBlock.offset3;
    v = v * scale4 + offset4;
    data[index.x] = v;

    if (index.x == 0) {
		printf("scale = %0.2f\n", scale);
        printf("offset = %0.2f\n", offset);

        printf("scale2 = %0.2f\n", scale2);
        printf("offset2 = %0.2f\n", offset2);

        printf("scale3 = %0.2f\n", gBlock.scale3);
        printf("offset3 = %0.2f\n", gBlock.offset3);

        printf("scale4 = %0.2f\n", scale4);
        printf("offset4 = %0.2f\n", offset4);
    }
}