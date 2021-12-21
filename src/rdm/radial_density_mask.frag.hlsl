cbuffer cb : register(b0) {
	float depthOut;
	float3 radius;
	float2 invClusterResolution;
	float2 projectionCenter;
};

float4 main(float4 position : SV_POSITION) : SV_TARGET {
	// working in blocks of 8x8 pixels
	float2 toCenter = trunc(position.xy * 0.125f) * invClusterResolution.xy - projectionCenter;
	float distToCenter = length(toCenter) * 2;

	uint2 iFragCoordHalf = uint2( position.xy * 0.5f );

	if( distToCenter < radius.x )
		discard;
	if( (iFragCoordHalf.x & 0x01u) == (iFragCoordHalf.y & 0x01u) && distToCenter < radius.y )
		discard;
	if( !((iFragCoordHalf.x & 0x01u) != 0u || (iFragCoordHalf.y & 0x01u) != 0u) && distToCenter < radius.z )
		discard;
	if( !((iFragCoordHalf.x & 0x03u) != 0u || (iFragCoordHalf.y & 0x03u) != 0u) )
		discard;

	return float4(0, 0, 0, 0);	
}
