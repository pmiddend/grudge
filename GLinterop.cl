__kernel void init_vbo_kernel(__global float4 *vbo)
{
	vbo[get_global_id(0)] = 0.0f;
}
