// @varying varying.def.sc

$input a_position
$output v_texcoord0

#include <bgfx_shader.sh>

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 0.0, 1.0));
	v_texcoord0 = a_position + vec2_splat(0.5);
}
