struct VertexData
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR;
};

struct FragmentData
{
    float4 Color : SV_TARGET;
};

FragmentData main(VertexData input)
{
    FragmentData fragment;
    fragment.Color = input.Color;
    return fragment;
}