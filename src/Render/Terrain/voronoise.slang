[Differentiable]
float hash(float a) {
	return frac(sin(a) * 12345.0);
}
[Differentiable]
float3 hash(float3 x ) {
    x = float3(
		dot(x,float3(127.1, 311.7, 74.7)),
		dot(x,float3(269.5, 183.3, 246.1)),
		dot(x,float3(113.5, 271.9, 124.6)));
	return frac(sin(x)*43758.5453123);
}


[Differentiable]
float voronoise( float3 x ) {
    float3 p = no_diff floor( x );
    float3 f = frac( x );

	float id = 0.0;
    float2 res = 1e12;
    for( int k=-1; k<=1; k++ )
    for( int j=-1; j<=1; j++ )
    for( int i=-1; i<=1; i++ )
    {
        float3 b = float3( float(i), float(j), float(k) );
        float3 r = float3( b ) - f + hash( p + b );
        float d = dot( r, r );

        if( d < res.x )
        {
			id = dot( p + b, float3(1.0, 57.0, 113.0 ) );
            res = float2( d, res.x );
        }
        else if( d < res.y )
        {
            res.y = d;
        }
    }

    // return float3( sqrt( res ), abs(id) );
    return sqrt(res.x);
}

[Differentiable]
float noise(float3 x, float c1, float c2) {
    float3 p = no_diff floor(x);
    float3 f = frac(x);
    f = f*f*(3.0-2.0*f);
    float n = p.x + p.y*c2+ c1*p.z;
    return lerp(
        lerp(
            lerp(hash(n + 0.0), hash(n + 1.0), f.x),
            lerp(hash(n + c2), hash(n + c2 + 1.0), f.x),
            f.y),
        lerp(
            lerp(hash(n + c1), hash(n + c1 + 1.0), f.x),
            lerp(hash(n + c1 + c2), hash(n + c1 + c2 + 1.0), f.x),
            f.y),
        f.z);
}
[Differentiable]
float noise(float3 p) {
	float a = noise(p, 883.0, 971.0);
	float b = noise(p + 0.5, 113.0, 157.0);
    return (a + b) * 0.5;
}
[Differentiable]
float map4( in float3 p ) {
	float f;
    f  = 0.50000*noise( p ); p = p*2.02;
    f += 0.25000*noise( p ); p = p*2.03;
    f += 0.12500*noise( p ); p = p*2.01;
    f += 0.06250*noise( p );
	return clamp(f, 0.0, 1.0);
}
