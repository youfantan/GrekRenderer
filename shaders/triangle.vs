cbuffer Scene : register(b0)
{
    float4 u_GlobalColor;
    float4 u_LightPos;
    float4 u_LightColor;
    float4 u_Ambient;
    float4 u_CameraPos;
    row_major float4x4 ViewMatrix;
};

cbuffer World: register(b1) {
    row_major float4x4 WorldMatrix[2];
}

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
    uint iid : ID;
};

PSIn main(VSIn v, uint instance : SV_InstanceID)
{
    PSIn o;
    row_major float4x4 wm = WorldMatrix[instance];
    row_major float4x4 wvp = mul(wm, ViewMatrix);
    o.pos = mul(float4(v.pos, 1.0), wvp);
    o.rpos = mul(float4(v.pos, 1.0), wm);
    o.normal = mul(float4(v.normal, 0.0), wm);
    o.uv = v.uv;
    o.iid = instance;
    return o;
}