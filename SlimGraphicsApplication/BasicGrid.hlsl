#include "ShaderSharedStructures.h"

cbuffer ConstantBufferData : register(b0)
{
    CameraData camera;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float3 coords : COLOR0;
};

//https://dev.to/javiersalcedopuyo/simple-infinite-grid-shader-5fah

static const float grid_size = 80.0;
static const float cell_size = grid_size / 8.0;
static const float half_cell_size = cell_size / 2.0;

static const float subcell_size = 0.4f;
static const float half_subcell_size = subcell_size * 0.5f;

PS_INPUT VSMain(uint id : SV_VertexID)
{
    PS_INPUT psOut;

	if (id == 1)
	{
		psOut.position = float4(-1, 0, -1, 1);
		//psOut.UVs = float2(0, 0);
	}
	else if (id == 0)
	{
		psOut.position = float4(1, 0, -1, 1);
		//psOut.UVs = float2(1, 0);
	}
	else if (id == 2)
	{
		psOut.position = float4(1, 0, 1, 1);
		//psOut.UVs = float2(1, 1);
	}
	else if (id == 3)
	{
		psOut.position = float4(1, 0, 1, 1);
		//psOut.UVs = float2(1, 1);
	}
	else if (id == 4)
	{
		psOut.position = float4(-1, 0, -1, 1);
		//psOut.UVs = float2(0, 0);
	}
	else //id = 5
	{
		psOut.position = float4(-1, 0, 1, 1);
		//psOut.UVs = float2(0, 1);
	}

	psOut.position.x *= grid_size;
	psOut.position.z *= grid_size;

	psOut.coords = psOut.position.xyz;

	psOut.position = mul(psOut.position, camera.view_projection_matrix);

return psOut;
}



template<typename T, typename U>
T mod(T x, U y) { return x - y * floor( x / y ); }


float4 PSMain(PS_INPUT input) : SV_TARGET
{
	float2 col = input.coords.xz / grid_size.xx;
	col += 1.0.xx;
	col *= 0.5.xx;

	float2 cell_coords    = mod( input.coords.xz  + half_cell_size,    cell_size    );
	float2 subcell_coords = mod( input.coords.xz  + half_subcell_size, subcell_size );

	float2 distance_to_cell    = abs( cell_coords    - half_cell_size    );
	float2 distance_to_subcell = abs( subcell_coords - half_subcell_size );

static const float cell_line_thickness    = 0.02f;
static const float subcell_line_thickness = 0.002f;

static const float4 cell_colour    = float4( 0.75, 0.75, 0.75, 0.75 );
static const float4 subcell_colour = float4(  0.5,  0.5,  0.5, 0.75 );

float d = fwidth(input.coords.xz);
float adjusted_cell_line_thickness    = 0.5 * ( cell_line_thickness    + d );
float adjusted_subcell_line_thickness = 0.5 * ( subcell_line_thickness + d );

// In the fragment shader
float4 color = 0.0.xxxx;
if ( any( distance_to_subcell < adjusted_subcell_line_thickness * 0.5 ) ) color = subcell_colour;
if ( any( distance_to_cell    < adjusted_cell_line_thickness    * 0.5 ) ) color = cell_colour;

static const float max_fade_distance = 50.0f;
// In the fragment shader
float opacity_falloff;
{
    float distance_to_camera = length(input.coords.xz - camera.camera_position.xz);
    opacity_falloff = smoothstep(1.0, 0.0, distance_to_camera / max_fade_distance);
}

return color * opacity_falloff;

   // return float4(col, 0.0,1.0);
}