/**
 * Adapted from Ogre: https://github.com/OGRECave/ogre-next under the MIT license
 */

Texture2D u_srcTex : register(t0);
SamplerState bilinearSampler : register(s0);

RWTexture2D<float4> u_dstTex : register(u0);

cbuffer cb : register(b0) {
	uint2 u_offset;
	float2 u_projectionCenter;
	float2 u_invClusterResolution;
	float2 u_invResolution;
	float3 u_radius;
	int u_quality;
};

// FIXME: AMD/NVIDIA extensions?
#define anyInvocationARB(value) (value)
#define imageStore(outImage, iuv, value) outImage[uint2(iuv)] = value
#define texelFetch(srcImage, iuv, lod) srcImage.Load(int3(iuv, lod))
#define textureLod(srcTex, uv, lod) srcTex.SampleLevel(bilinearSampler, uv, lod)

/** Takes the pattern (low quality):
		ab xx ef xx
		cd xx gh xx
		xx ij xx mn
		xx kl xx op
	And outputs:
		ab ab ef ef
		cd cd gh gh
		ij ij mn mn
		kl kl op op
*/
void reconstructHalfResLow( int2 dstUV, uint2 uFragCoordHalf )
{
	int2 offset;
	if( (uFragCoordHalf.x & 0x01u) != (uFragCoordHalf.y & 0x01u) )
		offset.x = (uFragCoordHalf.y & 0x01u) == 0 ? -2 : 2;
	else
		offset.x = 0;
	offset.y = 0;

	int2 uv = dstUV + offset;
	float4 srcVal = texelFetch( u_srcTex, uv.xy, 0 );

	imageStore( u_dstTex, dstUV, srcVal );
}

/** For non-rendered pixels, it averages the closest neighbours eg.
		ab
		cd
	 ef xy ij
	 gh zw kl
		mn
		op
	x = avg( f, c )
	y = avg( d, i )
	z = avg( h, m )
	w = avg( n, k )

	For rendered samples, it averages diagonals:
	ef
	gh
		ab
		cd
	outputs:
	h' = avg( a, h )
	a' = avg( a, h )
*/
void reconstructHalfResMedium( int2 dstUV, uint2 uFragCoordHalf )
{
	if( (uFragCoordHalf.x & 0x01u) != (uFragCoordHalf.y & 0x01u) )
	{
		int2 offset0;
		int2 offset1;
		offset0.x = (dstUV.x & 0x01) == 0 ? -1 : 1;
		offset0.y = 0;

		offset1.x = 0;
		offset1.y = (dstUV.y & 0x01) == 0 ? -1 : 1;

		int2 uv0 = int2( int2( dstUV ) + offset0 );
		float4 srcVal0 = texelFetch( u_srcTex, uv0.xy, 0 );
		int2 uv1 = int2( int2( dstUV ) + offset1 );
		float4 srcVal1 = texelFetch( u_srcTex, uv1.xy, 0 );

		imageStore( u_dstTex, dstUV, (srcVal0 + srcVal1) * 0.5f );
	}
	else
	{
		int2 uv = int2( dstUV );
		float4 srcVal = texelFetch( u_srcTex, uv.xy, 0 );

		int2 offset0;
		offset0.x = (dstUV.x & 0x01) == 0 ? -1 : 1;
		offset0.y = (dstUV.y & 0x01) == 0 ? -1 : 1;

		int2 uv0 = int2( int2( dstUV ) + offset0 );
		float4 srcVal0 = texelFetch( u_srcTex, uv0.xy, 0 );

		imageStore( u_dstTex, dstUV, (srcVal + srcVal0) * 0.5f );
	}
}

/* Uses Valve's Alex Vlachos Advanced VR Rendering Performance technique
   (bilinear approximation) GDC 2016
*/
void reconstructHalfResHigh( int2 dstUV, uint2 uFragCoordHalf )
{
	if( (uFragCoordHalf.x & 0x01u) != (uFragCoordHalf.y & 0x01u) )
	{
		float2 offset0;
		float2 offset1;
		offset0.x = (dstUV.x & 0x01) == 0 ? -0.5f : 1.5f;
		offset0.y =	(dstUV.y & 0x01) == 0 ? 0.75f : 0.25f;

		offset1.x = (dstUV.x & 0x01) == 0 ? 0.75f : 0.25f;
		offset1.y = (dstUV.y & 0x01) == 0 ? -0.5f : 1.5f;

		float2 offset0N = offset0;
		offset0N.x = (dstUV.x & 0x01) == 0 ? 2.5f : -1.5f;
		float2 offset1N = offset1;
		offset1N.y = (dstUV.y & 0x01) == 0 ? 2.5f : -1.5f;

		float2 uv0 = ( float2( dstUV ) + offset0 ) * u_invResolution;
		float4 srcVal0 = textureLod( u_srcTex, uv0.xy, 0 );
		float2 uv1 = ( float2( dstUV ) + offset1 ) * u_invResolution;
		float4 srcVal1 = textureLod( u_srcTex, uv1.xy, 0 );
		float2 uv0N = ( float2( dstUV ) + offset0N ) * u_invResolution;
		float4 srcVal0N = textureLod( u_srcTex, uv0N.xy, 0 );
		float2 uv1N = ( float2( dstUV ) + offset1N ) * u_invResolution;
		float4 srcVal1N = textureLod( u_srcTex, uv1N.xy, 0 );

		float4 finalVal = srcVal0 * 0.375f + srcVal1 * 0.375f + srcVal0N * 0.125f + srcVal1N * 0.125f;
		imageStore( u_dstTex, dstUV, finalVal );
	}
	else
	{
		float2 uv = float2( dstUV );
		uv.x += (dstUV.x & 0x01) == 0 ? 0.75f : 0.25f;
		uv.y += (dstUV.y & 0x01) == 0 ? 0.75f : 0.25f;
		uv.xy *= u_invResolution;
		float4 srcVal = textureLod( u_srcTex, uv.xy, 0 );

		int2 uv0 = int2( uFragCoordHalf << 1u );
		float4 srcTL = texelFetch( u_srcTex, uv0 + int2( -1, -1 ), 0 );
		float4 srcTR = texelFetch( u_srcTex, uv0 + int2(  2, -1 ), 0 );
		float4 srcBL = texelFetch( u_srcTex, uv0 + int2( -1,  2 ), 0 );
		float4 srcBR = texelFetch( u_srcTex, uv0 + int2(  2,  2 ), 0 );

		float weights[4] = { 0.28125f, 0.09375f, 0.09375f, 0.03125f };

		int idx = (dstUV.x & 0x01) + ((dstUV.y & 0x01) << 1u);

		float4 finalVal =	srcVal * 0.5f +
							srcTL * weights[(idx + 0)] +
							srcTR * weights[(idx + 1) & 0x03] +
							srcBL * weights[(idx + 2) & 0x03] +
							srcBR * weights[(idx + 3) & 0x03];

		imageStore( u_dstTex, dstUV, finalVal );
	}
}

/** Takes the pattern:
		a b x x
		c d x x
		x x x x
		x x x x
	And outputs:
		a b a b
		c d c d
		a b a b
		c d c d
*/
void reconstructQuarterRes( int2 dstUV, uint2 uFragCoordHalf )
{
	int2 offset;
	offset.x = (uFragCoordHalf.x & 0x01u) == 0 ? 0 : -2;
	offset.y = (uFragCoordHalf.y & 0x01u) == 0 ? 0 : -2;

	int2 uv = int2( int2( dstUV ) + offset );
	float4 srcVal = texelFetch( u_srcTex, uv.xy, 0 );

	imageStore( u_dstTex, dstUV, srcVal );
}

/** Same as reconstructQuarterRes, but a lot more samples to repeat:
		a b x x x x x x
		c d x x x x x x
		x x x x x x x x
		x x x x x x x x
		x x x x x x x x
		x x x x x x x x
		x x x x x x x x
		x x x x x x x x
	And outputs:
		a b a b a b a b
		c d c d c d c d
		a b a b a b a b
		c d c d c d c d
		a b a b a b a b
		c d c d c d c d
		a b a b a b a b
		c d c d c d c d
*/
void reconstructSixteenthRes( int2 dstUV, uint2 uFragCoordHalf )
{
	int2 block = int2( uFragCoordHalf ) & 0x03;

	int2 offset;
	offset.x = block.x * -2;
	offset.y = block.y * -2;

	int2 uv = int2( int2( dstUV ) + offset );
	float4 srcVal = texelFetch( u_srcTex, uv.xy, 0 );

	imageStore( u_dstTex, dstUV, srcVal );
}

[numthreads(8, 8, 1)]
void main(uint3 globalInvocationID : SV_DispatchThreadID) {
	uint2 currentUV = uint2(globalInvocationID.xy) + u_offset;
	uint2 uFragCoordHalf = uint2(currentUV >> 1u);

	//We must work in blocks so the reconstruction filter can work properly
	float2 toCenter     = (currentUV >> 3u) * u_invClusterResolution - u_projectionCenter;
	float  distToCenter = 2 * length(toCenter);

	//We know for a fact distToCenter is in blocks of 8x8
	if( anyInvocationARB( distToCenter >= u_radius.x ) )
	{
		if( anyInvocationARB( distToCenter < u_radius.y ) )
		{
			if( anyInvocationARB( distToCenter + u_invClusterResolution.x < u_radius.y ) )
			{
				if (u_quality == 0) 
					reconstructHalfResLow( int2(currentUV), uFragCoordHalf );
				else if (u_quality == 1)
					reconstructHalfResMedium( int2(currentUV), uFragCoordHalf );
				else
					reconstructHalfResHigh( int2(currentUV), uFragCoordHalf );
			}
			else
			{
				//Right next to the border with lower res rendering.
				//We can't use anything else than low quality filter
				reconstructHalfResLow( int2( currentUV ), uFragCoordHalf );
			}
		}
		else if( anyInvocationARB( distToCenter < u_radius.z ) )
		{
			reconstructQuarterRes( int2( currentUV ), uFragCoordHalf );
		}
		else
		{
			reconstructSixteenthRes( int2( currentUV ), uFragCoordHalf );
		}
	}
	else
	{
		imageStore( u_dstTex, int2(currentUV), texelFetch( u_srcTex, int2( currentUV ), 0 ) );
	}
}
