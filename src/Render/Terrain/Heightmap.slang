static const float HASH_SCALE = .1031;
static const float2x2 rotate2D = float2x2(1.5623, 1.7531, -1.8131, 1.8623);
static const float2 add = float2(1, 0);

[Differentiable]
float hash12(in float2 p) {
	float3 p3  = frac(float3(p.xyx) * HASH_SCALE);
    p3 += dot(p3, p3.yzx + 19.19);
    return frac((p3.x + p3.y) * p3.z);
}

[Differentiable]
float noise(in float2 xz) {
	float2 f = frac(xz);
	xz = no_diff floor(xz);
	float2 u = f * f * (3.0 - 2.0 * f);
	return	lerp(lerp(hash12(xz), hash12(xz + add.xy), u.x),
	lerp(hash12(xz + float2(0.0, 1.0)), hash12(xz + add.xx), u.x), u.y);
}

[Differentiable]
float3 noiseD(in float2 x) {
	x += 4.2;
    float2 p = no_diff floor(x);
    float2 f = frac(x);

    float2 u = f*f*(1.5-f)*2.0;;

    float a = hash12(p);
    float b = hash12(p + add.xy);
    float c = hash12(p + add.yx);
    float d = hash12(p + add.xx);
	return float3(a+(b-a)*u.x+(c-a)*u.y+(a-b-c+d)*u.x*u.y,
				6.0*f*(f-1.0)*(float2(b-a,c-a)+(a-b-c+d)*u.yx));
}

[Differentiable]
float fbm2(in float2 p) {
    float a = 3.8;
    float o = -.1;
    float2 d = float2(0);

    for (int i = 0; i < 8; i++) {
        float v;

        float3 nD = noiseD(p);
        d += nD.yz;
        v = nD.x/(1.+dot(d,d));

        v = pow(1.0-abs(v-.5)*2., 1.5);
        o += v * a;


       	v = noise(p*.1);
        o-= v*a*.3;

        a *= -.43;
        p = mul(rotate2D, p);
    }
    return (1 - o) * (1 - o) / 10;
}

[Differentiable]
float hash(float a) {
	return frac(sin(a) * 12345.0);
}
[Differentiable]
float noise(float3 x, float c1, float c2) {
    float3 p = no_diff floor(x);
    float3 f = frac(x);
    f = f*f*(3.0-2.0*f);
    float n = p.x + p.y*c2+ c1*p.z;
    return lerp(
        lerp(
            lerp(hash(n+0.0),hash(n+1.0),f.x),
            lerp(hash(n+c2),hash(n+c2+1.0),f.x),
            f.y),
        lerp(
            lerp(hash(n+c1),hash(n+c1+1.0),f.x),
            lerp(hash(n+c1+c2),hash(n+c1+c2+1.0),f.x),
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
    return saturate(f);
}
[Differentiable]
float fbm(float3 dir) {
	return map4(dir * 3.0 + map4(dir * 5.0) * 1.5);
}