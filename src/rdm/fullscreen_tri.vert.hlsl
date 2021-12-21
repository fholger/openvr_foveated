cbuffer cb : register(b0) {
	float depthOut;
	float3 radius;
	float2 invClusterResolution;
	float2 projectionCenter;
};

float4 main(uint vertexId: SV_VERTEXID) : SV_POSITION {
	float4 position;
	position.x = (vertexId == 2) ? 3.0 : -1.0;
	position.y = (vertexId == 1) ? -3.0 : 1.0;
	position.zw = float2(1.0, 1.0);
	return position;
}
