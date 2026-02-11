cbuffer Scene : register(b0)
{
    float4 u_GlobalColor;
    float4 u_LightPos;
    float4 u_LightColor;
    float4 u_Ambient;
    row_major float4x4 u_WorldMatrix;
    row_major float4x4 u_ViewMatrix;
};

struct VSIn {
    float3 pos : POSITION;
    float2 uv  : TEXCOORD;
    float3 normal : NORMAL;
};

struct PSIn {
    float4 pos : SV_POSITION;
    float4 rpos : POSITION;
    float2 uv  : TEXCOORD;
    float3 normal : NORMAL;
};

PSIn main(VSIn v)
{
    PSIn o;
    o.pos = mul(float4(v.pos, 1.0), u_ViewMatrix);
    o.rpos = mul(float4(v.pos, 1.0), u_WorldMatrix);
    o.normal = mul(float4(v.normal, 0.0), u_WorldMatrix);
    o.uv = v.uv;
    return o;
}