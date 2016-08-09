// @varying varying.def.sc

$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(u_texture, 0);

void main()
{
	gl_FragColor = texture2D(u_texture, vec2(v_texcoord0.x, v_texcoord0.y));
}
