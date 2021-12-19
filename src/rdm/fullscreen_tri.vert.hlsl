struct PSInput {
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD0;
};

cbuffer cb : register(b0) {
	float depthOut;
	float3 radius;
	float2 invClusterResolution;
	float2 projectionCenter;
};

PSInput main(uint vertexId: SV_VERTEXID) {
	PSInput result;
	result.uv.x = (vertexId == 2) ? 2.0 : 0.0;
	result.uv.y = (vertexId == 1) ? 2.0 : 0.0;
	result.position = float4(result.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), depthOut, 1.0);
	return result;
}
