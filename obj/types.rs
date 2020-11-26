use cgmath::Matrix4;

#[derive(Default, Copy, Clone)]
pub struct Vertex {
    pub pos: [f32; 3],
    pub texture: [f32; 2],
    pub normal: [f32; 3],
}
vulkano::impl_vertex!(Vertex, pos, texture, normal);

#[allow(dead_code)] // read by GPU
pub struct UniformBufferObject {
    pub model: Matrix4<f32>,
    pub view: Matrix4<f32>,
    pub proj: Matrix4<f32>,
}
