// @varying varying.def.sc

$input a_position
$output v_texcoord0

#include <bgfx_shader.sh>

uniform vec4 u_quadInfo;

void main()
{
	vec2 relPos = vec2(a_position.x * u_quadInfo.x, a_position.y * u_quadInfo.y);
	gl_Position = mul(u_modelViewProj, vec4(relPos, 0.0, 1.0));
	v_texcoord0.x = a_position.x + 0.5;
	v_texcoord0.y = (a_position.y + 0.5) * u_quadInfo.z;
}
