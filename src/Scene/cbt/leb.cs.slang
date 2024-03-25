import leb;

enum SubdivisionMode {
	eTriangle,
	eSquare
};
static SubdivisionMode gMode = SubdivisionMode::eTriangle;

[[vk::push_constant]]
cbuffer PushConstants {
	int u_CbtID;
	float2 u_TargetPosition;
};

float Wedge(float2 a, float2 b) {
    return a.x * b.y - a.y * b.x;
}
bool ShouldSplit(const cbt_Node node) {
    float3x2 faceVertices = float3x2( 0, 1, 0, 0, 1, 0 );
    if (gMode == SubdivisionMode::eTriangle)
        faceVertices = leb_DecodeNodeAttributeArray(node, faceVertices);
    else
        faceVertices = leb_DecodeNodeAttributeArray_Square(node, faceVertices);
    float2 v1 = faceVertices[0];
    float2 v2 = faceVertices[1];
    float2 v3 = faceVertices[2];
    float w1 = Wedge(v2 - v1, u_TargetPosition - v1);
    float w2 = Wedge(v3 - v2, u_TargetPosition - v2);
    float w3 = Wedge(v1 - v3, u_TargetPosition - v3);
    return all(float3(w1, w2, w3) >= 0);
}

[shader("compute")]
[numthreads(32, 1, 1)]
void Split(uint3 threadId: SV_DispatchThreadID) {
    const int cbtID = u_CbtID;
    uint threadID = threadId.x;

    uint count = cbt_NodeCount(cbtID);
    if (threadID < count) {
        let node = cbt_DecodeNode(cbtID, threadID);
        if (ShouldSplit(node)) {
			if (gMode == SubdivisionMode::eSquare)
				leb_SplitNode_Square(cbtID, node);
			else
    	        leb_SplitNode(cbtID, node);
        }
    }
}

[shader("compute")]
[numthreads(32, 1, 1)]
void Merge(uint3 threadId: SV_DispatchThreadID) {
    const int cbtID = u_CbtID;
    uint threadID = threadId.x;

    uint count = cbt_NodeCount(cbtID);
    if (threadID < count) {
        let node = cbt_DecodeNode(cbtID, threadID);

        leb_DiamondParent diamondParent;
		if (gMode == SubdivisionMode::eSquare)
			diamondParent = leb_DecodeDiamondParent_Square(node);
		else
        	diamondParent = leb_DecodeDiamondParent(node);

        if (!ShouldSplit(diamondParent.base) && !ShouldSplit(diamondParent.top)) {
			if (gMode == SubdivisionMode::eSquare)
            	leb_MergeNode_Square(cbtID, node, diamondParent);
			else
				leb_MergeNode(cbtID, node, diamondParent);
        }
    }
}
