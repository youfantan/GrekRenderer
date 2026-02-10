cbuffer Scene : register(b0)
{
    float4 u_GlobalColor[6];
    row_major float4x4 u_Rotation;
};

struct VSIn {
    float3 pos : POSITION;
    uint face : BLENDINDICES;
};

struct PSIn {
    float4 pos : SV_POSITION;
    float4 col : COLOR;
};

PSIn main(VSIn v)
{
    PSIn o;
    o.pos = mul(float4(v.pos, 1.0), u_Rotation);
    o.col = u_GlobalColor[v.face];
    return o;
}