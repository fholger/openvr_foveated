struct PSInput {
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD0;
};

cbuffer cb : register(b0) {
	float4 radius;
	float4 invClusterResolution;
};

float4 main(PSInput input) : SV_TARGET {
	float2 eyeCenter = input.uv.x >= 0.5f ? float2(0.7f, 0.5f) : float2(0.3f, 0.5f);

	// working in blocks of 8x8 pixels
	float2 toCenter = trunc(input.position.xy * 0.125f) * invClusterResolution.xy - eyeCenter;
	// x must be multiplied by 2 since we have both eyes in the same texture, occupying half of the texture
	toCenter.x *= 2;
	float distToCenter = length(toCenter) * 2;

	uint2 iFragCoordHalf = uint2( input.position.xy * 0.5f );

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
