struct VertexInput
{
    float4 Position : POSITION;
    float4 Color : COLOR;
};

struct VertexData
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR;
};

VertexData main(in VertexInput input)
{
    VertexData vertex;
    
    vertex.Position = input.Position;
    vertex.Color = input.Color;
 
    return vertex;
}