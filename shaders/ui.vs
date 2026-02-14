struct CharInfo {
    uint ascii;
    uint x;
    uint y;
    uint size;
};

struct SDFMeta {
    uint character;
    float tex_u0;
    float tex_v0;
    float tex_u1;
    float tex_v1;
    float width;
    float height;
    float bearingX;
    float bearingY;
    float advance;
};

Texture2D Tex : register(t0);
StructuredBuffer<SDFMeta> UVData : register(t1);
StructuredBuffer<CharInfo> Characters : register(t2);

cbuffer UI : register(b0)
{
    float ScreenWidth;
    float ScreenHeight;
    float AtlasFontSize;
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
    SDFMeta coord = UVData[info.ascii];
    float relativeScale = float(info.size) / AtlasFontSize;
    float tX = v.pos.x + 0.5f;
    float tY = 0.5f - v.pos.y;
    float scaledWidth  = float(coord.width) * relativeScale;
    float scaledHeight = float(coord.height) * relativeScale;
    float scaledBX = float(coord.bearingX) * relativeScale;
    float scaledBY = float(coord.bearingY) * relativeScale;
    float startX = (float(info.x) + scaledBX) / ScreenWidth;
    float startY = (float(info.y) - scaledBY) / ScreenHeight;

    float sizeW = scaledWidth / ScreenWidth;
    float sizeH = scaledHeight / ScreenHeight;

    o.pos.x = (startX + tX * sizeW) * 2.0f - 1.0f;
    o.pos.y = 1.0f - (startY + tY * sizeH) * 2.0f;
    o.pos.z = 0.0f;
    o.pos.w = 1.0f;

    o.uv.x = coord.tex_u0 + v.uv.x * (coord.tex_u1 - coord.tex_u0);
    o.uv.y = coord.tex_v0 + v.uv.y * (coord.tex_v1 - coord.tex_v0);

    return o;
}