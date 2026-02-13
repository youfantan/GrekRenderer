struct CharInfo {
    uint ascii;
    uint x;
    uint y;
    uint size;
};

struct SDFCoord {
    float tex_u0;
    float tex_v0;
    float tex_u1;
    float tex_v1;
};

Texture2D Tex : register(t0);
StructuredBuffer<SDFCoord> UVData : register(t1);
StructuredBuffer<CharInfo> Characters : register(t2);

cbuffer UI : register(b0)
{
    float ScreenWidth;
    float ScreenHeight;
};

struct VSIn {
    float2 pos : POSITION;
    float2 uv  : TEXCOORD;
};

struct PSIn {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD;
};

PSIn main(VSIn v, uint instance : SV_InstanceID) {
    PSIn o;
    CharInfo info = Characters[instance];
    float scaleX = info.size / ScreenWidth / 2.0f;
    float scaleY = info.size / ScreenHeight / 2.0f;
    float transX = info.x / ScreenWidth - 1.0f;
    float transY = 1.0f - info.y / ScreenHeight;
    o.pos = float4(
        v.pos.x * scaleX + transX,
        v.pos.y * scaleY + transY,
        0.0f,
        1.0f
    );
    SDFCoord tex_uv = UVData[info.ascii];
    float uv_width = tex_uv.tex_u1 - tex_uv.tex_u0;
    float uv_height = tex_uv.tex_v1 - tex_uv.tex_v0;
    float2 transUV = float2(uv_width * v.uv.x, uv_height * v.uv.y);
    o.uv = float2(tex_uv.tex_u0 + transUV.x, tex_uv.tex_v0 + transUV.y);
    return o;
}